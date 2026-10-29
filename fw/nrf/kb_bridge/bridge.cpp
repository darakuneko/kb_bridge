// KB Bridge nRF: USB (via CH559) -> BLE translation.
#include "bridge.h"
#include "ble.h"
#include "hid_parser.h"
#include <algorithm>
#include <vector>

namespace {

struct Iface {
  uint8_t num = 0, subclass = 0, protocol = 0, ep_in = 0, ep_out = 0;
  uint16_t desc_len = 0;
  std::vector<uint8_t> desc;
  HidDescriptor hid;
  bool vial = false;
};

// keys held by one (interface, report id) source
struct KeySource {
  uint16_t key;                         // (iface << 8) | report id
  uint8_t mods;
  std::vector<uint8_t> keys;
};

std::vector<Iface> ifaces;
bool enumerated = false;
Ch559Hello hello;

std::vector<KeySource> key_sources;
std::vector<uint8_t> key_order;         // held keys, oldest first (6KRO takes the first six)

// Current state as last queued for the host (what the host will see once the queue drains).
uint8_t cur_mods = 0, cur_keys[6] = {0};
uint16_t cur_consumer = 0;
uint8_t cur_system = 0;
uint8_t cur_buttons = 0;
Ch559Status status;
uint32_t last_activity_ms = 0;
uint32_t seen_connect_count = 0;

// Outgoing BLE reports, in order. A report that does not go out (queue full in the SoftDevice,
// notifications not enabled yet) stays at the head and is sent again, so a tap (press + release)
// is never lost and a release never overtakes its press. On overflow the oldest entry is dropped:
// the newest state is always the last entry, so nothing stays stuck.
enum : uint8_t { OUT_KEYBOARD, OUT_CONSUMER, OUT_SYSTEM, OUT_MOUSE, OUT_VIAL };
struct OutReport {
  uint8_t kind;
  uint8_t len;
  uint8_t data[32];
};
constexpr size_t OUT_QUEUE = 32;
OutReport out_queue[OUT_QUEUE];
size_t out_head = 0, out_count = 0;

void enqueue(uint8_t kind, const uint8_t *data, uint8_t len) {
  if (out_count == OUT_QUEUE) {                  // drop the oldest
    out_head = (out_head + 1) % OUT_QUEUE;
    out_count--;
  }
  OutReport &r = out_queue[(out_head + out_count) % OUT_QUEUE];
  r.kind = kind;
  r.len = len;
  memcpy(r.data, data, len);
  out_count++;
}

bool send_one(const OutReport &r) {
  switch (r.kind) {
    case OUT_KEYBOARD: return ble_keyboard(r.data[0], r.data + 1);
    case OUT_CONSUMER: return ble_consumer(r.data[0] | (r.data[1] << 8));
    case OUT_SYSTEM: return ble_system(r.data[0]);
    case OUT_MOUSE: return ble_mouse(r.data[0], (int8_t)r.data[1], (int8_t)r.data[2], (int8_t)r.data[3], (int8_t)r.data[4]);
    case OUT_VIAL: return ble_vial_input(r.data, r.len);
  }
  return true;
}

void flush() {
  while (out_count && send_one(out_queue[out_head])) {
    out_head = (out_head + 1) % OUT_QUEUE;
    out_count--;
  }
}

void queue_keyboard() {
  uint8_t d[7] = {cur_mods};
  memcpy(d + 1, cur_keys, 6);
  enqueue(OUT_KEYBOARD, d, 7);
}
void queue_consumer() {
  uint8_t d[2] = {(uint8_t)cur_consumer, (uint8_t)(cur_consumer >> 8)};
  enqueue(OUT_CONSUMER, d, 2);
}
void queue_system() { enqueue(OUT_SYSTEM, &cur_system, 1); }
void queue_mouse(uint8_t buttons, int8_t x, int8_t y, int8_t w, int8_t p) {
  uint8_t d[5] = {buttons, (uint8_t)x, (uint8_t)y, (uint8_t)w, (uint8_t)p};
  enqueue(OUT_MOUSE, d, 5);
}

// Fresh connection (or after a disconnect): whatever was queued while nobody listened is not
// replayed; the host gets the current state once.
void requeue_current_state() {
  out_head = out_count = 0;
  queue_keyboard();
  queue_consumer();
  queue_system();
  queue_mouse(cur_buttons, 0, 0, 0, 0);
}

Iface *find_iface(uint8_t num) {
  for (auto &i : ifaces)
    if (i.num == num) return &i;
  return nullptr;
}

KeySource &source(uint16_t key) {
  for (auto &s : key_sources)
    if (s.key == key) return s;
  key_sources.push_back({key, 0, {}});
  return key_sources.back();
}

void send_keyboard_if_changed() {
  uint8_t mods = 0;
  std::vector<uint8_t> held;
  for (auto &s : key_sources) {
    mods |= s.mods;
    for (auto k : s.keys)
      if (std::find(held.begin(), held.end(), k) == held.end()) held.push_back(k);
  }
  // keep the press order: drop released keys, append new ones
  key_order.erase(std::remove_if(key_order.begin(), key_order.end(),
                                 [&](uint8_t k) { return std::find(held.begin(), held.end(), k) == held.end(); }),
                  key_order.end());
  for (auto k : held)
    if (std::find(key_order.begin(), key_order.end(), k) == key_order.end()) key_order.push_back(k);

  uint8_t keys[6] = {0};
  for (size_t i = 0; i < 6 && i < key_order.size(); i++) keys[i] = key_order[i];
  if (mods != cur_mods || memcmp(keys, cur_keys, 6) != 0) {
    cur_mods = mods;
    memcpy(cur_keys, keys, 6);
    queue_keyboard();
  }
}

void release_all() {
  key_sources.clear();
  key_order.clear();
  send_keyboard_if_changed();
  if (cur_consumer) { cur_consumer = 0; queue_consumer(); }
  if (cur_system) { cur_system = 0; queue_system(); }
  if (cur_buttons) { cur_buttons = 0; queue_mouse(0, 0, 0, 0, 0); }
  flush();
}

void forget_device() {
  release_all();
  ifaces.clear();
  enumerated = false;
}

int8_t clamp8(int32_t v) { return v > 127 ? 127 : v < -127 ? -127 : (int8_t)v; }

// Decode one input report of an interface.
void process_report(Iface &it, const uint8_t *data, uint8_t len) {
  if (it.vial) {
    uint8_t buf[32] = {0};
    memcpy(buf, data, len > 32 ? 32 : len);
    enqueue(OUT_VIAL, buf, 32);
    last_activity_ms = millis();
    flush();
    return;
  }
  uint8_t rid = 0;
  if (it.hid.uses_ids) {
    if (!len) return;
    rid = data[0];
    data++;
    len--;
  }

  bool kb = false, consumer = false, system = false, mouse = false;
  uint8_t mods = 0;
  std::vector<uint8_t> keys;
  uint16_t consumer_usage = 0;
  uint8_t system_usage = 0;
  uint8_t buttons = 0;
  int32_t x = 0, y = 0, wheel = 0, pan = 0;

  for (const auto &f : it.hid.fields) {
    if (f.report_type != HID_INPUT || f.report_id != rid) continue;
    // A report that carries a category also releases it (an array value of 0 is out of range and
    // yields no usage), so note the category from the field before looking at the values.
    if (f.usage_page == 0x07) kb = true;
    else if (f.usage_page == 0x0C && !(f.variable && f.bit_size > 1)) consumer = true;
    else if (f.usage_page == 0x01 && f.app_usage == 0x80) system = true;
    for (uint8_t i = 0; i < f.count; i++) {
      uint32_t raw = hid_get_bits(data, len, f.bit_offset + (uint16_t)i * f.bit_size, f.bit_size);
      uint16_t usage;
      if (f.variable) {
        usage = f.usage_at(i);
      } else {
        int32_t v = f.logical_min < 0 ? hid_sign_extend(raw, f.bit_size) : (int32_t)raw;
        if (v < f.logical_min || v > f.logical_max) continue;      // out of range = nothing
        usage = f.n_usages ? f.usage_at((uint8_t)(v - f.logical_min)) : (uint16_t)(f.usage_min + (v - f.logical_min));
        if (usage == 0) continue;
        raw = 1;
      }
      switch (f.usage_page) {
        case 0x07:                                                 // keyboard
          kb = true;
          if (!raw) break;
          if (usage >= 0xE0 && usage <= 0xE7) mods |= 1 << (usage - 0xE0);
          else if (usage >= 0x04 && usage <= 0xDF && keys.size() < 32) keys.push_back((uint8_t)usage);
          break;
        case 0x0C:                                                 // consumer
          if (usage == 0x0238 && f.variable && f.bit_size > 1) {    // AC Pan: horizontal scroll
            mouse = true;
            pan = hid_sign_extend(raw, f.bit_size);
          } else {
            consumer = true;
            if (raw && !consumer_usage) consumer_usage = usage;
          }
          break;
        case 0x01:                                                 // generic desktop
          if (f.app_usage == 0x80) {                               // system control
            system = true;
            if (raw && !system_usage && usage <= 0xB7) system_usage = (uint8_t)usage;
          } else if (usage == 0x30 || usage == 0x31 || usage == 0x38) {
            mouse = true;
            int32_t v = f.logical_min < 0 ? hid_sign_extend(raw, f.bit_size) : (int32_t)raw;
            if (usage == 0x30) x = v;
            else if (usage == 0x31) y = v;
            else wheel = v;
          }
          break;
        case 0x09:                                                 // buttons
          mouse = true;
          if (raw && usage >= 1 && usage <= 8) buttons |= 1 << (usage - 1);
          break;
        default:
          break;
      }
    }
  }

  bool active = false;
  if (kb) {
    KeySource &s = source(((uint16_t)it.num << 8) | rid);
    active |= s.mods != mods || s.keys != keys || mods || !keys.empty();
    s.mods = mods;
    s.keys = keys;
    send_keyboard_if_changed();
  }
  if (consumer && consumer_usage != cur_consumer) {
    cur_consumer = consumer_usage;
    queue_consumer();
    active = true;
  }
  if (system && system_usage != cur_system) {
    cur_system = system_usage;
    queue_system();
    active = true;
  }
  if (mouse && (buttons != cur_buttons || x || y || wheel || pan)) {
    // relative motion is sent in pieces the BLE report can carry
    do {
      int8_t sx = clamp8(x), sy = clamp8(y), sw = clamp8(wheel), sp = clamp8(pan);
      queue_mouse(buttons, sx, sy, sw, sp);
      x -= sx; y -= sy; wheel -= sw; pan -= sp;
    } while (x || y || wheel || pan);
    cur_buttons = buttons;
    active = true;
  }
  if (active) last_activity_ms = millis();
  flush();
}

void enum_done() {
  for (auto &it : ifaces) {
    it.hid.parse(it.desc.data(), it.desc.size());
    it.vial = it.hid.has_app_collection(0xFF60, 0x61);
  }
  enumerated = true;
  last_activity_ms = millis();                 // the idle (suspend) timer starts at enumeration
}

}  // namespace

void bridge_handle(const LinkFrame &f) {
  const uint8_t *d = f.data;
  switch (f.type) {
    case MSG_HELLO:
      // HELLO comes only after a CH559 reset: whatever it was enumerating is gone
      forget_device();
      if (f.len >= 7) {
        hello.valid = true;
        hello.proto = d[0];
        hello.major = d[1];
        hello.minor = d[2];
        hello.build_id = d[3] | (d[4] << 8) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 24);
      }
      break;
    case MSG_DEV_ATTACH:
      forget_device();
      break;
    case MSG_IFACE:
      if (f.len >= 10) {
        Iface it;
        it.num = d[0]; it.subclass = d[1]; it.protocol = d[2];
        it.ep_in = d[3]; it.ep_out = d[6];
        it.desc_len = d[8] | (d[9] << 8);
        it.desc.reserve(it.desc_len);
        ifaces.push_back(std::move(it));
      }
      break;
    case MSG_DESC:
      if (f.len >= 3) {
        Iface *it = find_iface(d[0]);
        uint16_t off = d[1] | (d[2] << 8);
        uint8_t n = f.len - 3;
        if (it && off + n <= it->desc_len && off <= it->desc.size()) {
          if (it->desc.size() < off + n) it->desc.resize(off + n);
          memcpy(it->desc.data() + off, d + 3, n);
        }
      }
      break;
    case MSG_ENUM_DONE:
      enum_done();
      break;
    case MSG_DEV_DETACH:
      forget_device();
      break;
    case MSG_STATUS:
      if (f.len >= 5) {
        status.valid = true;
        status.state = d[0];
        status.suspended = d[1];
        status.in_errors = d[2] | (d[3] << 8);
        status.enum_failures = d[4];
      }
      break;
    case MSG_REPORT:
      // f.len == 1: a zero-length packet. QMK ends a full-size transfer with one (a 32-byte Vial
      // reply is followed by a ZLP); passing it on would queue an all-zero reply after every
      // real one and put the host one reply behind.
      if (f.len >= 2 && enumerated) {
        Iface *it = find_iface(d[0]);
        if (it) process_report(*it, d + 1, f.len - 1);
      }
      break;
    default:
      break;
  }
}

void bridge_host_leds(uint8_t leds) {
  if (!enumerated) return;
  for (auto &it : ifaces) {
    // every output report id that carries LED usages gets its own report
    std::vector<uint8_t> done;
    for (const auto &f : it.hid.fields) {
      if (f.report_type != HID_OUTPUT || f.usage_page != 0x08) continue;
      if (std::find(done.begin(), done.end(), f.report_id) != done.end()) continue;
      done.push_back(f.report_id);
      uint16_t n = it.hid.report_bytes(HID_OUTPUT, f.report_id);
      if (!n || n > 32) continue;
      uint8_t msg[3 + 32] = {it.num, HID_OUTPUT, f.report_id};
      uint8_t *out = msg + 3;
      for (const auto &g : it.hid.fields) {
        if (g.report_type != HID_OUTPUT || g.report_id != f.report_id || g.usage_page != 0x08) continue;
        for (uint8_t i = 0; i < g.count; i++) {
          uint16_t usage = g.usage_at(i);
          bool on = usage >= 1 && usage <= 5 && (leds & (1 << (usage - 1)));
          hid_put_bits(out, n, g.bit_offset + (uint16_t)i * g.bit_size, g.bit_size, on ? 1 : 0);
        }
      }
      link_send(MSG_OUT_REPORT, msg, 3 + n);
    }
  }
}

void bridge_host_vial(const uint8_t data[32]) {
  if (!enumerated) return;
  last_activity_ms = millis();
  for (auto &it : ifaces) {
    if (!it.vial) continue;
    uint8_t head[3] = {it.num, HID_OUTPUT, 0};
    link_send2(MSG_OUT_REPORT, head, 3, data, 32);
    return;
  }
}

bool bridge_keyboard_present() { return enumerated && !ifaces.empty(); }

bool bridge_has_vial() {
  for (auto &it : ifaces)
    if (it.vial) return true;
  return false;
}
const Ch559Hello &bridge_hello() { return hello; }
void bridge_clear_hello() { hello = Ch559Hello(); }
const Ch559Status &bridge_status() { return status; }
void bridge_forget_device() { forget_device(); }
uint32_t bridge_last_activity_ms() { return last_activity_ms; }
size_t bridge_pending_reports() { return out_count; }

void bridge_task(bool connected, uint32_t connect_count) {
  if (connect_count != seen_connect_count) {   // new connection: send the current state once
    seen_connect_count = connect_count;
    requeue_current_state();
  }
  if (!connected) {
    out_head = out_count = 0;                  // nobody listens: nothing to replay later
    return;
  }
  flush();
}
