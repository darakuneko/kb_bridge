// KB Bridge nRF52832 (AE-HY0020-DIP) firmware: pins and tuning.
// Boards v0.6 / v0.7 (BOARD_REV). Arduino pin numbers = P0.xx on the Feather nRF52832 core.
#pragma once

#define FW_VERSION_STR      "0.3.0"
#define DEVICE_NAME         "KB Bridge"
#define MANUFACTURER_NAME   "KB Bridge"
#define MODEL_NAME          "KB Bridge v0.6"
// USB VID / PID of KB Bridge: the BLE PnP ID (what Vial shows) and the update-mode USB serial
// (ch559/cdc.c). Not the attached keyboard's.
#define USB_VID             0x7A79
#define USB_PID             0x1576

// Board revision: build.sh passes -DBOARD_REV (6 = v0.6, 7 = v0.7).
#ifndef BOARD_REV
#define BOARD_REV 6
#endif

// ---- pins ----------------------------------------------------------------------------------
#define PIN_LINK_TX         6     // -> CH559 P0.2 RXD_   (Serial default TX)
#define PIN_LINK_RX         8     // <- CH559 P0.3 TXD_   (Serial default RX)
#define PIN_CH559_RST       18    // -> CH559 RST (active high, CH559 has a pull-down)
#define PIN_CH559_BOOT      16    // -> CH559 P4.6/XI: drive low + RST pulse = CH559 bootloader
#define PIN_VIN_SENSE       5     // AIN3: VBUS_IN / 2 (100k/100k)
#define PIN_KB_CC_SENSE     4     // AIN2: keyboard CC, 33k to 3V3 + 10k series; ~0.44 V with the keyboard's Rd
#define PIN_KB_VBUS_EN      7     // SY6280 EN (high = keyboard powered)
#define PIN_BTN_LOWI        3     // SW2 low-current mode (low = pressed)
#define PIN_BTN_PAIR        20    // SW1 pairing (low = pressed; also the bootloader's DFU pin)
#define PIN_KEEPALIVE       12    // AO3400A gate: 3 x 100R load on VBUS_IN
#define PIN_LED_A           9     // bi-colour LED between LED_A and LED_B (NFC pins as GPIO)
#define PIN_LED_B           10
// v0.7 only
#define PIN_CH559_PWR_OFF   2     // high = CH559 unpowered (gate of its P-FET, 1M pull-down = on)
#define PIN_KB_CC_PU        28    // Rp 33k source (v0.7)
// v0.7: Rp powered only while CC is sampled (saves ~90uA with a keyboard attached). Off by default:
// a keyboard whose USB-C controller watches CC could take a missing Rp for a detach.
#define CC_RP_PULSED        false

// Which way round the bi-colour LED is: true = LED_A high / LED_B low lights green.
// ZSRG-1608C-06-Z4: green conducts pad 1 -> pad 2, red pad 2 -> pad 1. D1 pad 1 = LED_B (P0.10),
// pad 2 -> 330R -> LED_A (P0.09), so LED_B high lights green. If the board shows the colours the
// other way round, the LED was placed turned 180 deg: flip this.
#define LED_GREEN_IS_A_HIGH false
#define LED_BLINK_MS        80    // one status blink

// ---- timing / thresholds -------------------------------------------------------------------
#define CH559_HELLO_TIMEOUT_MS   800
#define KEEPALIVE_PERIOD_MS      5000   // power-bank auto-off defeat: one load pulse per period
#define KEEPALIVE_PULSE_MS       200    // ~150 mA while on (3 x 100R in parallel at 5 V)
// Weak keep-alive for power banks in their own low-current mode (thresholds differ by maker).
#define KEEPALIVE_WEAK_PERIOD_MS 10000
#define KEEPALIVE_WEAK_PULSE_MS  50
#define VIN_CHECK_PERIOD_MS      2000
#define VIN_LOW_MV               4500   // below this the battery is flagged as low
#define CC_CHECK_PERIOD_MS       100
#define CC_ATTACH_MV             1200   // CC below this = the keyboard's Rd is there (0.44 V expected)
#define CC_DETACH_MS             500    // CC high this long with VBUS on = keyboard unplugged
#define BTN_LONG_MS              3000   // PAIR held this long = forget all bonds
#define ADV_FAST_TIMEOUT_S       30

// ---- stability -----------------------------------------------------------------------------
#define NRF_WDT_TIMEOUT_MS       4000   // nRF watchdog (fed every loop; not running during setup)
#define LINK_PING_PERIOD_MS      1000   // heartbeat: PING -> STATUS
#define LINK_SILENT_RESET_MS     3500   // no frame from the CH559 this long: reset it
#define KB_FAILED_CYCLE_MS       2000   // CH559 reports a failed enumeration this long: power-cycle the keyboard
#define KB_POWER_CYCLE_OFF_MS    500
#define KB_POWER_CYCLE_MAX       3      // per plug-in; then wait for a re-plug
#define ISP_MIN_VIN_MV           4400   // do not reflash the CH559 on a sagging supply

// ---- power ---------------------------------------------------------------------------------
// loop() period: 1 ms while there is work, LOOP_IDLE_MS after LOOP_BUSY_HOLD_MS without any. The
// first key after such a pause waits up to LOOP_IDLE_MS longer.
#define LOOP_IDLE_MS             10
#define LOOP_BUSY_HOLD_MS        1000
#define LOOP_SLEEP_MS            100    // CH559 asleep, UART off: BLE and the link RX edge wake loop()
#define LINK_UART_OFF_DELAY_MS   50     // after MSG_SLEEP, before the nRF switches its UART off
// Slave latency stays off this long after the last Vial message (the GUI polls while open).
#define VIAL_LATENCY_OFF_MS      10000
// USB suspend of the keyboard after this long without input (QMK switches RGB / backlight off in
// suspend when the keyboard sets rgb_matrix.sleep). A key press wakes the keyboard
// (remote wakeup).
#define USB_SUSPEND_ENABLE       true
#define USB_SUSPEND_IDLE_MS      120000
#define USB_SUSPEND_RETRY_MS     5000
// CH559 power-down (MSG_SLEEP) with no keyboard or with the keyboard suspended; asked again this
// often while the conditions hold and it is awake.
#define CH559_SLEEP_ENABLE       true
#define CH559_SLEEP_RETRY_MS     1000
// nRF52832 internal DC/DC (buck) instead of its LDO: lower radio and CPU current.
// HY0020 has the DC/DC inductor inside; DCDC mode only needs DCC (M1) tied to DEC4 (N1)
// (HY0020 datasheet v2.0, table 4-1 / fig. 5-2). The AE-HY0020-DIP carrier does that with R1,
// a 0 ohm jumper (~0 ohm). The Adafruit core leaves the regulator in LDO
// mode, so the firmware switches it (the datasheet: register setting needed either way).
#define NRF_DCDC_ENABLE          true
// System OFF (nRF ~0.5 uA, CH559 / keyboard / keep-alive off) after this long without a BLE
// connection. v0.7 only (v0.6 cannot switch the CH559 off). Wakes as a reset on LOW-I or on a
// keyboard plugged in / pulled out (CC). Not PAIR: P0.20 low at reset keeps the bootloader in DFU.
#define SYSTEM_OFF_ENABLE        (BOARD_REV >= 7)
#ifndef SYSTEM_OFF_IDLE_MS
#define SYSTEM_OFF_IDLE_MS       600000   // build.sh: EXTRA_FLAGS=-DSYSTEM_OFF_IDLE_MS=30000 to test
#endif
