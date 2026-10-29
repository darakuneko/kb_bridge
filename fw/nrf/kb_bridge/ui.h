// KB Bridge nRF: LED, buttons, power-bank keep-alive, battery (VBUS_IN) monitor, keyboard power.
#pragma once
#include <Arduino.h>

enum UiEvent : uint8_t {
  UI_NONE,
  UI_PAIR_SHORT,       // PAIR pressed and released (< BTN_LONG_MS)
  UI_PAIR_LONG,        // PAIR held BTN_LONG_MS
  UI_LOWI_PRESS,       // LOW-I pressed
};

enum LedMode : uint8_t {
  LED_OFF,
  LED_ADVERTISING,     // green blink
  LED_CONNECTED,       // green on for 2 s, then off
  LED_NO_KEYBOARD,     // connected but no keyboard: slow red blink
  LED_UPDATING,        // red / green alternating (CH559 update)
  LED_ERROR,           // fast red blink
};

void ui_begin();
UiEvent ui_task();                      // call every loop
void ui_set_mode(LedMode m);
LedMode ui_mode();
void ui_flash(bool green, uint8_t times); // short overlay: n blinks, then back to the mode
// Power-bank keep-alive load pulses (LOW-I button cycles strong -> weak -> off, persisted).
// 0 = strong (keep-alive on), 1 = off: the values stored by older firmware.
enum KeepAlive : uint8_t { KA_STRONG = 0, KA_OFF = 1, KA_WEAK = 2 };
KeepAlive ui_keepalive();
KeepAlive ui_next_keepalive();
uint32_t ui_ms_to_next_event();         // a keep-alive pulse ends then (loop() waits no longer)
bool ui_battery_low();
uint16_t ui_vin_mv();
bool ui_pair_held();                    // raw (for boot-time checks)
bool ui_keyboard_powered();
void ui_kb_power_cycle(uint32_t off_ms);
void ui_system_off_prepare();            // outputs off, LOW-I and CC armed as System OFF wake-ups   // VBUS_KB off for off_ms, then back on if the keyboard's Rd is there
