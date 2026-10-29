// KB Bridge nRF: turns the CH559's relayed USB device (interfaces, report descriptors, reports)
// into BLE reports, and host output (keyboard LEDs, Vial) back into USB output reports.
#pragma once
#include <Arduino.h>
#include "link.h"

struct Ch559Hello {
  bool valid = false;
  uint8_t proto = 0, major = 0, minor = 0;
  uint32_t build_id = 0;
};

struct Ch559Status {
  bool valid = false;
  uint8_t state = 0, suspended = 0, enum_failures = 0;   // state: STATE_* (link_proto.h)
  uint16_t in_errors = 0;
};

void bridge_handle(const LinkFrame &f);
// Call every loop: sends queued BLE reports again, resends the current state after a connect.
void bridge_task(bool connected, uint32_t connect_count);
void bridge_host_leds(uint8_t leds);                  // from BLE: NumLock bit0 .. Kana bit4
void bridge_host_vial(const uint8_t data[32]);        // from BLE: Vial output report
bool bridge_keyboard_present();                       // enumerated and has at least one HID interface
bool bridge_has_vial();
// VID / PID / bcdDevice of the attached device (from DEV_ATTACH); false when none.
const Ch559Hello &bridge_hello();
void bridge_clear_hello();
const Ch559Status &bridge_status();                   // last MSG_STATUS (heartbeat answer)
void bridge_forget_device();                          // release every key, drop the interfaces
uint32_t bridge_last_activity_ms();                   // last key / mouse / Vial change (millis)
size_t bridge_pending_reports();                      // BLE reports waiting to go out
