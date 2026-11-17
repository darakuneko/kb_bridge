// Host test: the bridge's own keymap and Vial protocol (remap.cpp) with a keyboard without Vial.
//   c++ -std=c++17 -I fw/test -I fw/nrf/kb_bridge fw/test/test_remap.cpp fw/nrf/kb_bridge/hid_parser.cpp \
//       fw/nrf/kb_bridge/bridge.cpp fw/nrf/kb_bridge/remap.cpp -o /tmp/test_remap && /tmp/test_remap
#include <cassert>
#include <string>
#include <vector>
#include "bridge.h"
#include "hid_parser.h"
#include "link_proto.h"

// ---- stubs capturing what the bridge sends ------------------------------------------------
static std::vector<std::string> events;
static char tmp[128];
static int fail_next = 0;                 // the next N BLE sends fail (queue full / not connected)
static bool record(const char *s) {
  if (fail_next > 0) { fail_next--; return false; }
  events.push_back(s);
  return true;
}
uint32_t millis() { static uint32_t t = 0; return t += 1; }
bool ble_keyboard(uint8_t mods, const uint8_t k[6]) {
  snprintf(tmp, sizeof tmp, "kb %02x %02x %02x %02x %02x %02x %02x", mods, k[0], k[1], k[2], k[3], k[4], k[5]);
  return record(tmp);
}
bool ble_consumer(uint16_t u) { snprintf(tmp, sizeof tmp, "cc %04x", u); return record(tmp); }
bool ble_system(uint8_t u) { snprintf(tmp, sizeof tmp, "sys %02x", u); return record(tmp); }
bool ble_mouse(uint8_t b, int8_t x, int8_t y, int8_t w, int8_t p) {
  snprintf(tmp, sizeof tmp, "mouse %02x %d %d %d %d", b, x, y, w, p);
  return record(tmp);
}
static uint8_t last_vial[32];
bool ble_vial_input(const uint8_t *d, uint8_t len) { memcpy(last_vial, d, 32); snprintf(tmp, sizeof tmp, "vial %u %02x", len, d[0]); return record(tmp); }
void link_send2(uint8_t type, const uint8_t *h, uint8_t hl, const uint8_t *d, uint8_t dl) {
  std::string s = "out";
  snprintf(tmp, sizeof tmp, " %02x", type);
  s += tmp;
  for (int i = 0; i < hl; i++) { snprintf(tmp, sizeof tmp, " %02x", h[i]); s += tmp; }
  for (int i = 0; i < dl; i++) { snprintf(tmp, sizeof tmp, " %02x", d[i]); s += tmp; }
  events.push_back(s);
}
void link_send(uint8_t type, const uint8_t *p, uint8_t len) { link_send2(type, p, len, nullptr, 0); }

// ---- QMK-like descriptors -------------------------------------------------------------------
// interface 0: boot keyboard (no report ids) with LED output
static const uint8_t kb_desc[] = {
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
  0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 0x81, 0x01, 0x05, 0x08, 0x19, 0x01,
  0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02, 0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x05, 0x07,
  0x19, 0x00, 0x2A, 0xFF, 0x00, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x06, 0x75, 0x08, 0x81, 0x00,
  0xC0,
};
// interface 1: Vial raw HID
static const uint8_t raw_desc[] = {
  0x06, 0x60, 0xFF, 0x09, 0x61, 0xA1, 0x01, 0x09, 0x62, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x20,
  0x75, 0x08, 0x81, 0x02, 0x09, 0x63, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x95, 0x20, 0x75, 0x08, 0x91,
  0x02, 0xC0,
};
// interface 2: shared endpoint: mouse (2), system (3), consumer (4), NKRO (6)
static const uint8_t shared_desc[] = {
  // mouse, id 2: buttons 8, X Y (8 bit), wheel, pan
  0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x02, 0x09, 0x01, 0xA1, 0x00,
  0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02,
  0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x95, 0x02, 0x75, 0x08, 0x81, 0x06,
  0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08, 0x81, 0x06,
  0x05, 0x0C, 0x0A, 0x38, 0x02, 0x15, 0x81, 0x25, 0x7F, 0x95, 0x01, 0x75, 0x08, 0x81, 0x06,
  0xC0, 0xC0,
  // system control, id 3: one 16-bit usage 0x01..0xB7
  0x05, 0x01, 0x09, 0x80, 0xA1, 0x01, 0x85, 0x03, 0x19, 0x01, 0x2A, 0xB7, 0x00, 0x15, 0x01, 0x26,
  0xB7, 0x00, 0x95, 0x01, 0x75, 0x10, 0x81, 0x00, 0xC0,
  // consumer, id 4: one 16-bit usage 0x001..0x2A0
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x04, 0x19, 0x01, 0x2A, 0xA0, 0x02, 0x15, 0x01, 0x26,
  0xA0, 0x02, 0x95, 0x01, 0x75, 0x10, 0x81, 0x00, 0xC0,
  // NKRO, id 6: modifiers + 8-byte bitmap for usages 0x00..0x3F (short, for the test)
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x06, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00,
  0x25, 0x01, 0x95, 0x08, 0x75, 0x01, 0x81, 0x02, 0x05, 0x07, 0x19, 0x00, 0x29, 0x3F, 0x95, 0x40,
  0x75, 0x01, 0x81, 0x02, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x95, 0x05, 0x75, 0x01, 0x91, 0x02,
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0xC0,
};

static uint32_t conn_count = 0;

static void handle(uint8_t type, const std::vector<uint8_t> &payload) {
  LinkFrame f{};
  f.type = type;
  f.len = (uint8_t)payload.size();
  memcpy(f.data, payload.data(), payload.size());
  bridge_handle(f);
}

// one frame per loop(): reports go out in bridge_task()
static void frame(uint8_t type, std::vector<uint8_t> payload) {
  handle(type, payload);
  bridge_task(true, conn_count);
}

static void send_desc(uint8_t iface, const uint8_t *d, size_t n) {
  for (size_t off = 0; off < n; off += 64) {
    std::vector<uint8_t> p = {iface, (uint8_t)off, (uint8_t)(off >> 8)};
    p.insert(p.end(), d + off, d + std::min(n, off + 64));
    frame(MSG_DESC, p);
  }
}

static void expect(const std::vector<std::string> &want) {
  if (events != want) {
    printf("FAIL\n  got:\n");
    for (auto &e : events) printf("    %s\n", e.c_str());
    printf("  want:\n");
    for (auto &e : want) printf("    %s\n", e.c_str());
    exit(1);
  }
  events.clear();
}


#include "remap.h"
#include "vial_def.h"

static const uint8_t *vial(std::vector<uint8_t> m) {
  uint8_t buf[32] = {0};
  memcpy(buf, m.data(), m.size());
  bridge_host_vial(buf);
  bridge_task(true, conn_count);
  assert(events.size() == 1 && events[0].rfind("vial 32", 0) == 0);
  events.clear();
  return last_vial;
}
static void key(std::vector<uint8_t> r) {      // boot keyboard report: mods, 0, keys...
  r.resize(8);
  std::vector<uint8_t> p = {0};
  p.insert(p.end(), r.begin(), r.end());
  frame(MSG_REPORT, p);
}
static void set_kc(uint8_t layer, uint8_t usage, uint16_t kc) {
  int p = remap_pos(usage);
  vial({0x05, layer, (uint8_t)(p / REMAP_COLS), (uint8_t)(p % REMAP_COLS), (uint8_t)(kc >> 8), (uint8_t)kc});
}

int main() {
  frame(MSG_HELLO, {1, 0, 1, 0x78, 0x56, 0x34, 0x12});
  // no keyboard yet: the bridge answers Vial itself
  const uint8_t *r = vial({0xFE, 0x00});
  assert(r[0] == 6 && r[1] == 0 && r[4] == 'K');
  r = vial({0x01});
  assert(r[0] == 0x01 && r[1] == 0x00 && r[2] == 0x09);

  frame(MSG_DEV_ATTACH, {0x6E, 0x44, 0x01, 0x00, 0x01, 0x00, 1, 3});
  frame(MSG_IFACE, {0, 1, 1, 1, 8, 1, 0, 0, sizeof kb_desc, 0});
  send_desc(0, kb_desc, sizeof kb_desc);
  frame(MSG_IFACE, {2, 0, 0, 3, 32, 1, 0, 0, sizeof shared_desc, 0});
  send_desc(2, shared_desc, sizeof shared_desc);
  frame(MSG_ENUM_DONE, {});
  assert(bridge_keyboard_present() && !bridge_has_vial());
  events.clear();

  // definition: size, then 32-byte pages
  r = vial({0xFE, 0x01});
  uint32_t size = r[0] | (r[1] << 8) | (r[2] << 16) | ((uint32_t)r[3] << 24);
  assert(size == sizeof(VIAL_DEFINITION));
  std::vector<uint8_t> def;
  for (uint16_t page = 0; page * 32 < size; page++) {
    r = vial({0xFE, 0x02, (uint8_t)page, (uint8_t)(page >> 8)});
    def.insert(def.end(), r, r + std::min<uint32_t>(32, size - page * 32));
  }
  assert(memcmp(def.data(), VIAL_DEFINITION, size) == 0);
  r = vial({0x11});
  assert(r[1] == REMAP_LAYERS);
  r = vial({0x0C});
  assert(r[1] == 0);                             // no macros

  // default keymap: what the keyboard sends
  key({0x02, 0, 0x04});
  key({0});
  expect({"kb 02 04 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // A -> B
  set_kc(0, 0x04, 0x0005);
  r = vial({0x04, 0, (uint8_t)(remap_pos(0x04) / REMAP_COLS), (uint8_t)(remap_pos(0x04) % REMAP_COLS)});
  assert(r[4] == 0x00 && r[5] == 0x05);
  key({0, 0, 0x04});
  key({0});
  expect({"kb 00 05 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // Caps = MO(1), layer 1: H -> Left. Releasing Caps first still releases Left.
  set_kc(0, 0x39, 0x5221);
  set_kc(1, 0x0B, 0x0050);
  key({0, 0, 0x39});
  key({0, 0, 0x39, 0x0B});
  key({0, 0, 0x0B});
  key({0});
  expect({"kb 00 50 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0, 0, 0x0B});                             // layer 1 off again: plain H
  key({0});
  expect({"kb 00 0b 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // Left Ctrl -> Caps Lock, J -> LCTL(C), K -> volume up, L -> mouse button 1, Right Alt = TG(2)
  set_kc(0, 0xE0, 0x0039);
  set_kc(0, 0x0D, 0x0106);
  set_kc(0, 0x0E, 0x00A9);
  set_kc(0, 0x0F, 0x00D1);
  key({0x01});
  key({0});
  expect({"kb 00 39 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0, 0, 0x0D});
  key({0});
  expect({"kb 01 06 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0, 0, 0x0E});
  key({0});
  expect({"cc 00e9", "cc 0000"});
  key({0, 0, 0x0F});
  key({0});
  expect({"mouse 01 0 0 0 0", "mouse 00 0 0 0 0"});
  set_kc(0, 0xE6, 0x5262);
  set_kc(2, 0x04, 0x001E);                       // layer 2: A -> 1
  key({0x40});
  key({0});
  key({0, 0, 0x04});
  key({0});
  expect({"kb 00 1e 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0x40});
  key({0});
  events.clear();

  // keymap buffer: read back what was set (layer 0, A = 0x0005); offset = position * 2
  uint16_t off_a = remap_pos(0x04) * 2, off_b = remap_pos(0x05) * 2;
  r = vial({0x12, (uint8_t)(off_a >> 8), (uint8_t)off_a, 2});
  assert(r[4] == 0x00 && r[5] == 0x05);                                     // A -> B
  r = vial({0x12, (uint8_t)(off_b >> 8), (uint8_t)off_b, 2});
  assert(r[4] == 0x00 && r[5] == 0x05);                                     // B
  // set through the buffer: layer 0 B -> C
  vial({0x13, (uint8_t)(off_b >> 8), (uint8_t)off_b, 2, 0x00, 0x06});
  key({0, 0, 0x05});
  key({0});
  expect({"kb 00 06 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // matrix state: only once unlocked (Esc + Enter held)
  r = vial({0x02, 0x03});
  assert(r[2] == 0 && r[3] == 0);
  r = vial({0xFE, 0x05});
  int pe = remap_pos(0x29), pn = remap_pos(0x28);                          // Esc, Enter
  assert(r[0] == 0 && r[2] == pe / REMAP_COLS && r[3] == pe % REMAP_COLS &&
         r[4] == pn / REMAP_COLS && r[5] == pn % REMAP_COLS);
  vial({0xFE, 0x06});
  key({0, 0, 0x29, 0x28});
  events.clear();
  r = last_vial;
  for (int i = 0; i < 60 && !(i && r[0] == 1); i++) {
    for (int t = 0; t < 120; t++) millis();      // > 100 ms between polls
    r = vial({0xFE, 0x07});
  }
  assert(r[0] == 1);
  r = vial({0x02, 0x03});
  // 3 bytes per row, big endian, bit = column (Esc 0,0 and Enter in the ALL layout's Q row)
  for (int row = 0; row < REMAP_ROWS; row++) {
    uint32_t v = 0;
    for (int b = 0; b < 3; b++) v = (v << 8) | r[2 + row * 3 + b];
    uint32_t want = 0;
    if (pe / REMAP_COLS == row) want |= 1UL << (pe % REMAP_COLS);
    if (pn / REMAP_COLS == row) want |= 1UL << (pn % REMAP_COLS);
    assert(v == want);
  }
  // the layout and the matrix agree: row 3 is Caps A S D ... (left to right)
  assert(remap_pos(0x39) == 3 * REMAP_COLS && remap_pos(0x04) == 3 * REMAP_COLS + 1);
  key({0});
  events.clear();

  // reset: defaults again
  vial({0x06});
  key({0, 0, 0x04});
  key({0});
  expect({"kb 00 04 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // custom keys: queued for the main loop; JIS->US / US->JIS / no conversion also set the conversion
  uint8_t id;
  while (remap_take_custom(id)) {}
  set_kc(0, 0x3A, 0x7E01);                       // F1 = Host 2
  set_kc(0, 0x3B, 0x7E05);                       // F2 = JIS->US
  set_kc(0, 0x3C, 0x7E06);                       // F3 = US->JIS
  set_kc(0, 0x3D, 0x7E07);                       // F4 = no conversion
  key({0, 0, 0x3A}); key({0});
  assert(remap_take_custom(id) && id == KBB_HOST2 && !remap_take_custom(id));
  events.clear();

  // JIS->US (JIS keyboard, US OS): Shift+2 (") -> Shift+' ; @ -> Shift+2 ; Shift+@ (`) -> ` unshifted
  key({0, 0, 0x3B}); key({0});
  assert(remap_conversion() == REMAP_CONV_JIS_TO_US);
  events.clear();
  key({0x02, 0, 0x1F}); key({0x02}); key({0});
  expect({"kb 02 34 00 00 00 00 00", "kb 02 00 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0, 0, 0x2F}); key({0});
  expect({"kb 02 1f 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0x02}); key({0x02, 0, 0x2F}); key({0x02}); key({0});
  expect({"kb 02 00 00 00 00 00 00", "kb 00 35 00 00 00 00 00", "kb 02 00 00 00 00 00 00",
          "kb 00 00 00 00 00 00 00"});
  // a key without conversion is untouched
  key({0x02, 0, 0x04}); key({0});
  expect({"kb 02 04 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // US->JIS (US keyboard, JIS OS): Shift+2 (@) -> @ key unshifted ; = -> Shift+-
  key({0, 0, 0x3C}); key({0});
  assert(remap_conversion() == REMAP_CONV_US_TO_JIS);
  events.clear();
  key({0x02, 0, 0x1F}); key({0});
  expect({"kb 00 2f 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  key({0, 0, 0x2E}); key({0});
  expect({"kb 02 2d 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // no conversion again
  key({0, 0, 0x3D}); key({0});
  assert(remap_conversion() == REMAP_CONV_NONE);
  events.clear();
  key({0, 0, 0x2E}); key({0});
  expect({"kb 00 2e 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  while (remap_take_custom(id)) {}

  printf("all remap tests passed\n");
  return 0;
}
