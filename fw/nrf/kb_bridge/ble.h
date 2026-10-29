// KB Bridge nRF: BLE side. One HID service for input (keyboard, consumer, system, mouse) and a
// second HID service for Vial raw HID (usage page 0xFF60), like RMK: Android only uses the first.
// Device Information Service serial = "vial:f64c2b3c:XXXXXX" so the Vial GUI finds the device.
#pragma once
#include <Arduino.h>

void ble_begin();
bool ble_connected();
bool ble_advertising();
void ble_start_advertising();
void ble_forget_all();                        // clear bonds, disconnect, advertise

// Report senders return false when the report did not go out (not connected, notifications not
// enabled yet, or the SoftDevice queue is full); the bridge keeps the state and sends it again.
bool ble_keyboard(uint8_t mods, const uint8_t keys[6]);
bool ble_consumer(uint16_t usage);
bool ble_system(uint8_t usage);
bool ble_mouse(uint8_t buttons, int8_t x, int8_t y, int8_t wheel, int8_t pan);
bool ble_vial_input(const uint8_t *data, uint8_t len);
uint32_t ble_connect_count();
// DIS PnP ID: the attached keyboard's ids (saved in flash when they change; hosts usually pick up a
// change on the next connection, macOS may need the device removed and paired again).

// Filled from BLE callbacks (other task), drained by loop():
bool ble_take_leds(uint8_t &leds);            // keyboard LED output report from the host
bool ble_take_vial_output(uint8_t buf[32]);   // Vial raw HID output report from the host
