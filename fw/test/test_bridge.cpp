// Host test: HID descriptor parsing and USB -> BLE report translation with QMK-style descriptors.
//   c++ -std=c++17 -I fw/test -I fw/nrf/kb_bridge fw/test/test_bridge.cpp \
//       fw/nrf/kb_bridge/hid_parser.cpp fw/nrf/kb_bridge/bridge.cpp -o /tmp/test_bridge && /tmp/test_bridge
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
bool ble_vial_input(const uint8_t *d, uint8_t len) { snprintf(tmp, sizeof tmp, "vial %u %02x", len, d[0]); return record(tmp); }
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

static void frame(uint8_t type, std::vector<uint8_t> payload) {
  LinkFrame f{};
  f.type = type;
  f.len = (uint8_t)payload.size();
  memcpy(f.data, payload.data(), payload.size());
  bridge_handle(f);
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

int main() {
  // parser sanity
  HidDescriptor h;
  assert(h.parse(shared_desc, sizeof shared_desc));
  assert(h.uses_ids);
  assert(h.report_bytes(HID_INPUT, 2) == 5);
  assert(h.report_bytes(HID_INPUT, 6) == 9);
  assert(h.report_bytes(HID_OUTPUT, 6) == 1);

  frame(MSG_HELLO, {1, 0, 1, 0x78, 0x56, 0x34, 0x12});
  assert(bridge_hello().valid && bridge_hello().build_id == 0x12345678);

  frame(MSG_DEV_ATTACH, {0x6E, 0x44, 0x01, 0x00, 0x01, 0x00, 1, 3});
  frame(MSG_IFACE, {0, 1, 1, 1, 8, 1, 0, 0, sizeof kb_desc, 0});
  send_desc(0, kb_desc, sizeof kb_desc);
  frame(MSG_IFACE, {1, 0, 0, 2, 32, 1, 2, 32, sizeof raw_desc, 0});
  send_desc(1, raw_desc, sizeof raw_desc);
  frame(MSG_IFACE, {2, 0, 0, 3, 32, 1, 0, 0, sizeof shared_desc, 0});
  send_desc(2, shared_desc, sizeof shared_desc);
  frame(MSG_ENUM_DONE, {});
  assert(bridge_keyboard_present() && bridge_has_vial());
  events.clear();

  // boot keyboard: shift + A, then + B, release A
  frame(MSG_REPORT, {0, 0x02, 0, 0x04, 0, 0, 0, 0, 0});
  frame(MSG_REPORT, {0, 0x02, 0, 0x04, 0x05, 0, 0, 0, 0});
  frame(MSG_REPORT, {0, 0x02, 0, 0x05, 0, 0, 0, 0, 0});
  frame(MSG_REPORT, {0, 0x02, 0, 0x05, 0, 0, 0, 0, 0});        // unchanged: nothing sent
  expect({"kb 02 04 00 00 00 00 00", "kb 02 04 05 00 00 00 00", "kb 02 05 00 00 00 00 00"});

  // NKRO on the shared endpoint merges with the boot keyboard: C (0x06) and ctrl
  frame(MSG_REPORT, {2, 6, 0x01, 0x40, 0, 0, 0, 0, 0, 0, 0});
  expect({"kb 03 05 06 00 00 00 00"});
  // 7 keys held via NKRO: 6KRO keeps the first six (B was first)
  frame(MSG_REPORT, {2, 6, 0x00, 0xC0, 0x3F, 0, 0, 0, 0, 0, 0});   // usages 6,7,8..0x0D
  expect({"kb 02 05 06 07 08 09 0a"});
  frame(MSG_REPORT, {2, 6, 0, 0, 0, 0, 0, 0, 0, 0, 0});
  frame(MSG_REPORT, {0, 0, 0, 0, 0, 0, 0, 0, 0});
  expect({"kb 02 05 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // consumer volume up (0xE9), release; system sleep (0x82)
  frame(MSG_REPORT, {2, 4, 0xE9, 0x00});
  frame(MSG_REPORT, {2, 4, 0x00, 0x00});
  frame(MSG_REPORT, {2, 3, 0x82, 0x00});
  frame(MSG_REPORT, {2, 3, 0x00, 0x00});
  expect({"cc 00e9", "cc 0000", "sys 82", "sys 00"});

  // mouse: button 1, x -5, y +3, wheel -1, pan +2
  frame(MSG_REPORT, {2, 2, 0x01, 0xFB, 0x03, 0xFF, 0x02});
  expect({"mouse 01 -5 3 -1 2"});

  // Vial: keyboard -> host, host -> keyboard
  std::vector<uint8_t> raw(33, 0);
  raw[0] = 1; raw[1] = 0xFE;
  frame(MSG_REPORT, raw);
  expect({"vial 32 fe"});
  frame(MSG_REPORT, {1});                                        // zero-length packet: dropped
  frame(MSG_REPORT, {0});
  expect({});
  uint8_t vial_out[32] = {0x01, 0x02};
  bridge_host_vial(vial_out);
  assert(events.size() == 1 && events[0].rfind("out 81 01 02 00 01 02", 0) == 0);
  events.clear();

  // host LEDs: caps lock (bit 1) -> boot keyboard (no id) and NKRO report id 6
  bridge_host_leds(0x02);
  expect({"out 81 00 02 00 02", "out 81 02 02 06 02"});

  // BLE send failures: a quick tap while the SoftDevice queue is full is kept in order
  fail_next = 2;
  frame(MSG_REPORT, {0, 0, 0, 0x2C, 0, 0, 0, 0, 0});            // space down: send fails
  frame(MSG_REPORT, {0, 0, 0, 0, 0, 0, 0, 0, 0});               // space up: the retry fails too, queued behind
  expect({});
  assert(bridge_pending_reports() == 2);
  bridge_task(true, 0);
  expect({"kb 00 2c 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});

  // new connection: the current state is sent once (here: shift held)
  frame(MSG_REPORT, {0, 0x02, 0, 0, 0, 0, 0, 0, 0});
  expect({"kb 02 00 00 00 00 00 00"});
  bridge_task(true, 1);
  expect({"kb 02 00 00 00 00 00 00", "cc 0000", "sys 00", "mouse 01 0 0 0 0"});
  // disconnected: nothing queued now is replayed later
  fail_next = 100;
  frame(MSG_REPORT, {0, 0, 0, 0x04, 0, 0, 0, 0, 0});
  bridge_task(false, 1);
  assert(bridge_pending_reports() == 0);
  fail_next = 0;
  frame(MSG_REPORT, {0, 0, 0, 0, 0, 0, 0, 0, 0});
  expect({"kb 00 00 00 00 00 00 00"});

  // queue overflow (host stalls for 40 key changes): the oldest go, the final state survives
  fail_next = 1000;
  for (int i = 0; i < 20; i++) {
    frame(MSG_REPORT, {0, 0, 0, (uint8_t)(0x04 + i), 0, 0, 0, 0, 0});
    frame(MSG_REPORT, {0, 0, 0, 0, 0, 0, 0, 0, 0});
  }
  assert(bridge_pending_reports() == 32);
  fail_next = 0;
  bridge_task(true, 1);
  assert(events.size() == 32 && events.back() == "kb 00 00 00 00 00 00 00");
  events.clear();

  // STATUS (heartbeat answer)
  frame(MSG_STATUS, {STATE_RUNNING, 1, 0x34, 0x12, 2});
  assert(bridge_status().valid && bridge_status().suspended == 1 && bridge_status().in_errors == 0x1234);

  // detach releases everything that is held
  frame(MSG_REPORT, {0, 0x01, 0, 0x04, 0, 0, 0, 0, 0});
  frame(MSG_DEV_DETACH, {0});
  expect({"kb 01 04 00 00 00 00 00", "kb 00 00 00 00 00 00 00", "mouse 00 0 0 0 0"});
  assert(!bridge_keyboard_present());

  // a HELLO (CH559 restarted) also drops the device and releases keys
  frame(MSG_DEV_ATTACH, {0x6E, 0x44, 0x01, 0x00, 0x01, 0x00, 1, 1});
  frame(MSG_IFACE, {0, 1, 1, 1, 8, 1, 0, 0, sizeof kb_desc, 0});
  send_desc(0, kb_desc, sizeof kb_desc);
  frame(MSG_ENUM_DONE, {});
  frame(MSG_REPORT, {0, 0, 0, 0x05, 0, 0, 0, 0, 0});
  frame(MSG_HELLO, {2, 0, 2, 1, 2, 3, 4});
  expect({"kb 00 05 00 00 00 00 00", "kb 00 00 00 00 00 00 00"});
  assert(!bridge_keyboard_present());

  printf("all bridge tests passed\n");
  return 0;
}
