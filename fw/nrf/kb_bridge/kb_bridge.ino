// KB Bridge: wired QMK/Vial keyboard -> BLE, nRF52832 side (AE-HY0020-DIP, Adafruit bootloader).
// Board: Adafruit Feather nRF52832 (adafruit:nrf52:feather52832). Build with fw/build.sh.
//
// Start-up: reset the CH559 and wait for its HELLO. If it does not answer or its build id differs
// from the image embedded here (ch559_image.h), flash it over its serial bootloader first. So
// updating the nRF also updates the CH559 (one DFU for the user).
#include <bluefruit.h>
#include "config.h"
#include "link.h"
#include "wake.h"
#include "bridge.h"
#include "ble.h"
#include "ui.h"
#include "ch559_isp.h"
#include "ch559_image.h"

static bool ch559_ok = false;
static uint32_t last_link_rx_ms;               // any frame from the CH559 (heartbeat)
static uint32_t last_ping_ms;
static uint32_t failed_since_ms, idle_since_ms, last_suspend_req_ms;
static uint8_t kb_power_cycles;
static uint32_t last_sleep_req_ms;
static bool ch559_powered = true;              // v0.7: false while switched off (no keyboard)

// ---- nRF watchdog -------------------------------------------------------------------------
// Started at the end of setup() (setup may block for seconds while it reflashes the CH559).
// Pauses while a debugger halts the CPU, keeps counting in sleep.
static void wdt_start() {
  if (NRF_WDT->RUNSTATUS) return;              // survives a soft reset: already configured
  NRF_WDT->CONFIG = (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos) | (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos);
  NRF_WDT->CRV = (uint32_t)NRF_WDT_TIMEOUT_MS * 32768UL / 1000UL;
  NRF_WDT->RREN = WDT_RREN_RR0_Msk;
  NRF_WDT->TASKS_START = 1;
}
static inline void wdt_feed() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }

static void nfc_pins_as_gpio() {
  // P0.09/P0.10 drive the LED: the NFC function must be off (UICR, once; needs a reset).
  // The build also sets CONFIG_NFCT_PINS_AS_GPIOS, which does the same in SystemInit().
  if ((NRF_UICR->NFCPINS & UICR_NFCPINS_PROTECT_Msk) == (UICR_NFCPINS_PROTECT_NFC << UICR_NFCPINS_PROTECT_Pos)) {
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Wen << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NRF_UICR->NFCPINS &= ~UICR_NFCPINS_PROTECT_Msk;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NRF_NVMC->CONFIG = NVMC_CONFIG_WEN_Ren << NVMC_CONFIG_WEN_Pos;
    while (NRF_NVMC->READY == NVMC_READY_READY_Busy) {}
    NVIC_SystemReset();
  }
}

// Reset the CH559 and wait for HELLO on the link.
static bool ch559_restart() {
  bridge_clear_hello();
  link_begin();
  ch559_reset();
  uint32_t start = millis();
  LinkFrame f;
  while (millis() - start < CH559_HELLO_TIMEOUT_MS) {
    if (link_poll(f)) {
      bridge_handle(f);
      if (bridge_hello().valid) return true;
    }
  }
  return false;
}

static bool ch559_up_to_date() {
  const Ch559Hello &h = bridge_hello();
  return h.valid && h.proto == LINK_PROTO_VERSION && h.build_id == CH559_IMAGE_BUILD_ID;
}

static void ch559_start() {
  pinMode(PIN_CH559_PWR_OFF, OUTPUT);
  digitalWrite(PIN_CH559_PWR_OFF, LOW);            // on
  pinMode(PIN_CH559_BOOT, INPUT);                  // never hold XI in normal operation
  pinMode(PIN_CH559_RST, OUTPUT);
  digitalWrite(PIN_CH559_RST, LOW);

  // PAIR held at power-on is the update mode: the CH559 is a USB serial port for the nRF DFU.
  // Once the nRF application runs (DFU done or skipped) wait for the button to be released, so the
  // CH559 comes back in host mode after our reset.
  uint32_t t0 = millis();
  while (ui_pair_held() && millis() - t0 < 15000) {
    ui_task();
    delay(10);
  }

  // Update a CH559 that answers with another build, or a silent one (no application yet, or an
  // update of ours cut short): it is powered up with XI low to enter its bootloader.
  bool answered = ch559_restart();
  ch559_ok = answered && ch559_up_to_date();
  bool update = !ch559_ok;
  if (update && ui_vin_mv() < ISP_MIN_VIN_MV) {
    // reflashing on a sagging supply could leave the CH559 without firmware: try next boot
    ui_set_mode(LED_ERROR);
  } else if (update) {
    ui_set_mode(LED_UPDATING);
    Ch559IspResult r = ISP_OK;
    for (int attempt = 0; attempt < 2 && !ch559_ok; attempt++) {
      uint32_t until = millis() + 50;
      while (millis() < until) ui_task();
      r = ch559_isp_flash(CH559_IMAGE, CH559_IMAGE_LEN, answered);
      answered = false;                            // a second try finds the bootloader or a fresh chip
      ch559_ok = (r == ISP_OK) && ch559_restart() && ch559_up_to_date();
    }
    ui_set_mode(ch559_ok ? LED_OFF : LED_ERROR);
    if (!ch559_ok) ui_flash(true, r ? r : 9);      // green x result (ch559_isp.h), x9: wrote but no HELLO; then fast red
  } else if (!ch559_ok) {
    ui_set_mode(LED_ERROR);
  }
  if (!ch559_ok) link_begin();                     // keep listening; a later HELLO still counts
}

void setup() {
  loop_wake_init();
  nfc_pins_as_gpio();
  ui_begin();
  ch559_start();
  ble_begin();
  last_link_rx_ms = last_ping_ms = millis();
  wdt_start();
}

// Heartbeat: PING every second (the CH559 answers with STATUS). A CH559 that stays silent is
// reset; the bridge has already released every key through bridge_forget_device().
static void link_health() {
  uint32_t now = millis();
  if (link_peer_asleep() || !ch559_powered) { last_link_rx_ms = last_ping_ms = now; return; }
  if (now - last_ping_ms >= LINK_PING_PERIOD_MS) {
    last_ping_ms = now;
    link_send(MSG_PING, nullptr, 0);
  }
  if (now - last_link_rx_ms >= LINK_SILENT_RESET_MS) {
    bridge_forget_device();
    ch559_reset();
    last_link_rx_ms = now;
  }
}

// Keyboard that does not enumerate (CH559 STATE_FAILED) while powered: switch VBUS_KB off and on
// a few times, like a re-plug. Counted per plug-in.
static void keyboard_recovery() {
  const Ch559Status &st = bridge_status();
  uint32_t now = millis();
  if (!st.valid) return;
  if (st.state == STATE_RUNNING) {
    kb_power_cycles = 0;
    failed_since_ms = idle_since_ms = 0;
    return;
  }
  if (st.state == STATE_IDLE) {                // nothing attached for a while = unplugged
    failed_since_ms = 0;
    if (!idle_since_ms) idle_since_ms = now;
    else if (now - idle_since_ms > 5000) kb_power_cycles = 0;
    return;
  }
  idle_since_ms = 0;
  if (st.state != STATE_FAILED || !ui_keyboard_powered()) {
    failed_since_ms = 0;
    return;
  }
  if (!failed_since_ms) failed_since_ms = now;
  if (now - failed_since_ms >= KB_FAILED_CYCLE_MS && kb_power_cycles < KB_POWER_CYCLE_MAX) {
    kb_power_cycles++;
    failed_since_ms = 0;
    ui_kb_power_cycle(KB_POWER_CYCLE_OFF_MS);
  }
}

// USB suspend after a quiet period. The CH559 resumes by itself on a remote wakeup or on host
// output (LEDs, Vial); STATUS says whether the bus is suspended, so a lost request is repeated.
static void usb_power() {
  static bool was_suspended;
  static uint32_t resumed_ms;
  if (!USB_SUSPEND_ENABLE || !bridge_keyboard_present()) return;
  const Ch559Status &st = bridge_status();
  uint32_t now = millis();
  if (was_suspended && !st.suspended) resumed_ms = now;   // woke up: count the idle time afresh
  was_suspended = st.suspended;
  if (st.suspended || bridge_pending_reports()) return;
  if (now - bridge_last_activity_ms() < USB_SUSPEND_IDLE_MS) return;
  if (now - resumed_ms < USB_SUSPEND_IDLE_MS) return;
  if (now - last_suspend_req_ms < USB_SUSPEND_RETRY_MS) return;
  last_suspend_req_ms = now;
  link_send(MSG_USB_SUSPEND, nullptr, 0);
}

// CH559 power-down while it has nothing to do: no keyboard (VBUS_KB off, so nothing can attach),
// or the keyboard's bus suspended. It wakes itself on a USB event (remote wakeup); we wake it
// before any frame (link.cpp) and when a keyboard is plugged in.
// No keyboard -> the CH559 is switched off altogether (MSG_SLEEP still covers the suspended
// keyboard, whose USB state must survive). It comes back with the keyboard's VBUS (CC detect).
static void ch559_power(bool on) {
  if (on == ch559_powered) return;
  ch559_powered = on;
  if (on) {
    digitalWrite(PIN_CH559_PWR_OFF, LOW);
    link_begin();                                  // the CH559 says HELLO when it is up
    last_link_rx_ms = last_ping_ms = millis();
  } else {
    bridge_forget_device();
    link_shutdown();                               // TX low: no feeding the unpowered CH559
    pinMode(PIN_CH559_BOOT, INPUT);
    digitalWrite(PIN_CH559_RST, LOW);
    digitalWrite(PIN_CH559_PWR_OFF, HIGH);
  }
}

static void ch559_sleep() {
  if (!CH559_SLEEP_ENABLE) return;
  if (ch559_ok) {
    if (!ch559_powered) {
      if (ui_keyboard_powered()) ch559_power(true);
      return;
    }
    const Ch559Status &st = bridge_status();
    if (!link_peer_asleep() && st.valid && st.state == STATE_IDLE && !ui_keyboard_powered()) {
      ch559_power(false);
      return;
    }
  }
  if (link_peer_asleep()) {
    if (ui_keyboard_powered() && !bridge_keyboard_present()) link_wake_peer();   // plugged in
    return;
  }
  const Ch559Status &st = bridge_status();
  uint32_t now = millis();
  bool idle = st.state == STATE_IDLE && !ui_keyboard_powered();
  bool suspended = st.state == STATE_RUNNING && st.suspended;
  if (!st.valid || !(idle || suspended) || bridge_pending_reports()) return;
  if (now - last_sleep_req_ms < CH559_SLEEP_RETRY_MS) return;
  last_sleep_req_ms = now;
  link_send(MSG_SLEEP, nullptr, 0);
  link_peer_sleeps();
}

// No BLE connection for SYSTEM_OFF_IDLE_MS: switch everything off. The wake-up is a reset, so the
// next start is a normal boot (bootloader, CH559 restart, advertising).
static void system_off_task(bool user_input) {
  static uint32_t active_ms;                       // last connection, button press or boot
  if (!SYSTEM_OFF_ENABLE) return;
  uint32_t now = millis();
  if (ble_connected() || user_input || ui_pair_held()) active_ms = now;
  if (now - active_ms < SYSTEM_OFF_IDLE_MS) return;
  ch559_power(false);
  ui_system_off_prepare();
  sd_power_system_off();
}

static void update_led() {
  static bool was_connected = false;
  bool conn = ble_connected();
  if (!ch559_ok) {
    ui_set_mode(LED_ERROR);
  } else if (conn && !was_connected) {
    ui_set_mode(LED_CONNECTED);
  } else if (!conn && ble_advertising()) {
    ui_set_mode(LED_ADVERTISING);
  } else if (conn && !bridge_keyboard_present()) {
    ui_set_mode(LED_NO_KEYBOARD);
  } else if (ui_mode() == LED_NO_KEYBOARD || ui_mode() == LED_ERROR) {
    ui_set_mode(LED_OFF);                          // the keyboard arrived / the CH559 came back
  }
  was_connected = conn;
}

void loop() {
  static uint32_t busy_ms;                         // last time there was something to do
  wdt_feed();
  LinkFrame f;
  while (link_poll(f)) {
    last_link_rx_ms = busy_ms = millis();
    bridge_handle(f);
    if (f.type == MSG_HELLO && !ch559_ok && ch559_up_to_date()) ch559_ok = true;
  }
  bridge_task(ble_connected(), ble_connect_count());
  link_health();
  keyboard_recovery();
  usb_power();

  uint8_t leds;
  while (ble_take_leds(leds)) bridge_host_leds(leds), busy_ms = millis();
  uint8_t vial[32];
  while (ble_take_vial_output(vial)) bridge_host_vial(vial), busy_ms = millis();
  if (bridge_pending_reports()) busy_ms = millis();

  UiEvent ev = ui_task();
  switch (ev) {
    case UI_PAIR_SHORT:
      ble_start_advertising();
      ui_flash(true, 1);
      break;
    case UI_PAIR_LONG:
      ui_flash(false, 3);
      ble_forget_all();
      break;
    case UI_LOWI_PRESS:
      switch (ui_next_keepalive()) {                 // green x2 weak, green x3 strong, red x2 off
        case KA_STRONG: ui_flash(true, 3); break;
        case KA_WEAK: ui_flash(true, 2); break;
        default: ui_flash(false, 2); break;
      }
      break;
    default:
      break;
  }
  ch559_sleep();
  link_idle_task();
  update_led();
  system_off_task(ev != UI_NONE);
  // Busy (typing, Vial, reports to resend): 1 ms passes. Quiet for a while: 10 ms, the CPU sleeps
  // in between; the UART buffers what the CH559 sends meanwhile (1 KB, build.sh). CH559 asleep and
  // UART off: 100 ms; BLE writes and the CH559's wake byte cut the wait short (wake.h).
  uint32_t wait = millis() - busy_ms < LOOP_BUSY_HOLD_MS ? 1 : link_uart_off() ? LOOP_SLEEP_MS : LOOP_IDLE_MS;
  loop_wait(min(wait, ui_ms_to_next_event()));    // a keep-alive pulse ends on time
}
