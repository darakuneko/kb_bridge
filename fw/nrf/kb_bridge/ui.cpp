// KB Bridge nRF: LED, buttons, keep-alive, VBUS_IN monitor, keyboard VBUS switch.
#include "ui.h"
#include "config.h"
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;


namespace {

const char *CFG_FILE = "/kbb_cfg";
struct Settings {
  uint8_t magic = 0xB5;
  uint8_t keepalive = KA_WEAK;          // KeepAlive; 0 / 1 keep their old meaning (keep-alive on / off)
  uint8_t host = 0;                     // selected BLE host (added in 0.4.0: older files end before it)
} settings;

LedMode mode = LED_OFF;
uint32_t mode_since = 0;
uint8_t flash_left = 0;                 // half-periods left in the overlay
bool flash_green = false;
uint32_t flash_ms = 0;

bool vin_low = false;
uint16_t vin_mv = 0;
uint32_t vin_ms = 0;

bool kb_power = false;
uint32_t cc_ms = 0, cc_high_since = 0;
uint32_t kb_off_until = 0;              // VBUS_KB power cycle in progress until then

uint32_t ka_ms = 0;
bool ka_on = false;

struct Button {
  explicit Button(uint8_t p) : pin(p) {}
  uint8_t pin;
  bool down = false;
  uint32_t since = 0;
  uint32_t change_ms = 0;
};
Button pair_btn(PIN_BTN_PAIR), lowi_btn(PIN_BTN_LOWI);
// One press sequence lasts until both buttons are up again. Once both were down together it is
// a two-button press, and neither button's own action happens.
bool both_seen = false, long_sent = false, ignore_press = false;
uint32_t both_since = 0;

void led(int8_t colour) {               // -1 off, 0 red, 1 green
  if (colour < 0) {
    digitalWrite(PIN_LED_A, LOW);
    digitalWrite(PIN_LED_B, LOW);
    return;
  }
  bool a_high = (colour == 1) == LED_GREEN_IS_A_HIGH;
  digitalWrite(PIN_LED_A, a_high ? HIGH : LOW);
  digitalWrite(PIN_LED_B, a_high ? LOW : HIGH);
}

void load_settings() {
  InternalFS.begin();
  File f(InternalFS);
  if (f.open(CFG_FILE, FILE_O_READ)) {
    Settings s;
    int n = f.read(&s, sizeof(s));
    if (n >= 2 && s.magic == 0xB5 && s.keepalive <= KA_WEAK) {
      if (n < (int)sizeof(s) || s.host >= BLE_HOST_COUNT) s.host = 0;
      settings = s;
    }
    f.close();
  }
}

void save_settings() {
  InternalFS.remove(CFG_FILE);
  File f(InternalFS);
  if (f.open(CFG_FILE, FILE_O_WRITE)) {
    f.write((const uint8_t *)&settings, sizeof(settings));
    f.close();
  }
}

uint16_t read_mv(uint8_t pin);

// With CC_RP_PULSED: Rp (33k) is powered from a pin only while sampling (it draws ~90uA
// through the keyboard's Rd otherwise). A floating CC would read low = "Rd present", so it is
// always driven here.
uint16_t read_cc_mv() {
  if (!CC_RP_PULSED) return read_mv(PIN_KB_CC_SENSE);
  digitalWrite(PIN_KB_CC_PU, HIGH);
  delayMicroseconds(200);
  uint16_t mv = read_mv(PIN_KB_CC_SENSE);
  digitalWrite(PIN_KB_CC_PU, LOW);
  return mv;
}

uint16_t read_mv(uint8_t pin) {
  // 12-bit, internal 3.0 V reference (0.6 V x gain 1/5)
  uint32_t raw = analogRead(pin);
  return (uint16_t)(raw * 3000UL / 4095UL);
}

void led_task() {
  uint32_t now = millis();
  if (flash_left) {
    if (now - flash_ms >= 150) {
      flash_ms = now;
      flash_left--;
    }
    led((flash_left & 1) ? (flash_green ? 1 : 0) : -1);
    return;
  }
  uint32_t t = now - mode_since;
  switch (mode) {
    case LED_OFF:
      // low battery: one short red blink every 5 s
      led(vin_low && (now % 5000) < LED_BLINK_MS ? 0 : -1);
      break;
    case LED_ADVERTISING:
      led((now % 1000) < LED_BLINK_MS ? 1 : -1);
      break;
    case LED_CONNECTED:
      if (t < 2000) led(1);
      else { mode = LED_OFF; led(-1); }
      break;
    case LED_NO_KEYBOARD:
      led((now % 2000) < LED_BLINK_MS ? 0 : -1);
      break;
    case LED_UPDATING:
      led((now / 200) & 1 ? 1 : 0);
      break;
    case LED_ERROR:
      led((now % 250) < 125 ? 0 : -1);
      break;
  }
}

// Time until the blink pattern changes, so a 100 ms loop wait neither stretches nor skips a blink.
uint32_t led_ms_to_next_edge() {
  uint32_t now = millis(), period;
  if (flash_left) return 150 - min<uint32_t>(now - flash_ms, 150);
  switch (mode) {
    case LED_OFF: if (!vin_low) return UINT32_MAX; period = 5000; break;
    case LED_ADVERTISING: period = 1000; break;
    case LED_NO_KEYBOARD: period = 2000; break;
    case LED_CONNECTED: return 2000 - min<uint32_t>(now - mode_since, 2000);
    case LED_UPDATING: return 200 - now % 200;
    case LED_ERROR: return 125 - now % 125;
    default: return UINT32_MAX;
  }
  uint32_t p = now % period;
  return p < LED_BLINK_MS ? LED_BLINK_MS - p : period - p;
}

// 20 ms debounce: a change counts once it has been stable that long. True when the state changed.
bool debounce(Button &b) {
  bool pressed = digitalRead(b.pin) == LOW;
  uint32_t now = millis();
  if (pressed == b.down) {
    b.change_ms = 0;
    return false;
  }
  if (!b.change_ms) b.change_ms = now ? now : 1;
  if (now - b.change_ms < 20) return false;
  b.change_ms = 0;
  b.down = pressed;
  b.since = now;
  return true;
}

UiEvent button_task() {
  bool pair_changed = debounce(pair_btn), lowi_changed = debounce(lowi_btn);
  uint32_t now = millis();
  if (!pair_btn.down && !lowi_btn.down) {          // sequence over: the short actions happen on release
    UiEvent e = UI_NONE;
    if ((pair_changed || lowi_changed) && !long_sent && !ignore_press)
      e = both_seen ? UI_BOTH_SHORT : pair_changed ? UI_PAIR_SHORT : UI_LOWI_PRESS;
    both_seen = long_sent = ignore_press = false;
    return e;
  }
  if (ignore_press || long_sent) return UI_NONE;
  if (pair_btn.down && lowi_btn.down && !both_seen) {
    both_seen = true;
    both_since = now;
  }
  if (both_seen) {
    if (pair_btn.down && lowi_btn.down && now - both_since >= BTN_LONG_MS) {
      long_sent = true;
      return UI_BOTH_LONG;
    }
  } else if (pair_btn.down && now - pair_btn.since >= BTN_LONG_MS) {
    long_sent = true;
    return UI_PAIR_LONG;
  }
  return UI_NONE;
}

void power_task() {
  uint32_t now = millis();

  // battery / VBUS_IN
  if (now - vin_ms >= VIN_CHECK_PERIOD_MS) {
    vin_ms = now;
    vin_mv = read_mv(PIN_VIN_SENSE) * 2;
    vin_low = vin_mv < VIN_LOW_MV;
  }

  // keyboard VBUS: only with the keyboard's Rd on CC (a PC or charger plugged into J2 has none)
  if (kb_off_until && (int32_t)(now - kb_off_until) < 0) {
    // power cycle: VBUS stays off (the keyboard's Rd keeps CC low meanwhile)
  } else if (now - cc_ms >= CC_CHECK_PERIOD_MS) {
    kb_off_until = 0;
    cc_ms = now;
    bool rd = read_cc_mv() < CC_ATTACH_MV;
    if (rd) {
      cc_high_since = 0;
      if (!kb_power) {
        kb_power = true;
        digitalWrite(PIN_KB_VBUS_EN, HIGH);
      }
    } else if (kb_power) {
      if (!cc_high_since) cc_high_since = now ? now : 1;
      else if (now - cc_high_since >= CC_DETACH_MS) {
        kb_power = false;
        digitalWrite(PIN_KB_VBUS_EN, LOW);
      }
    }
  }

  // keep-alive load pulses
  bool weak = settings.keepalive == KA_WEAK;
  uint32_t pulse = weak ? KEEPALIVE_WEAK_PULSE_MS : KEEPALIVE_PULSE_MS;
  uint32_t period = weak ? KEEPALIVE_WEAK_PERIOD_MS : KEEPALIVE_PERIOD_MS;
  if (settings.keepalive == KA_OFF) {
    if (ka_on) { ka_on = false; digitalWrite(PIN_KEEPALIVE, LOW); }
  } else if (ka_on) {
    if (now - ka_ms >= pulse) { ka_on = false; digitalWrite(PIN_KEEPALIVE, LOW); }
  } else if (now - ka_ms >= period) {
    ka_ms = now;
    ka_on = true;
    digitalWrite(PIN_KEEPALIVE, HIGH);
  }
}

}  // namespace

void ui_begin() {
  pinMode(PIN_LED_A, OUTPUT_H0H1);       // high drive: standard drive leaves the LED dim
  pinMode(PIN_LED_B, OUTPUT_H0H1);
  led(-1);
  pinMode(PIN_BTN_PAIR, INPUT_PULLUP);
  pinMode(PIN_BTN_LOWI, INPUT_PULLUP);
  pinMode(PIN_KEEPALIVE, OUTPUT);
  digitalWrite(PIN_KEEPALIVE, LOW);
  pinMode(PIN_KB_VBUS_EN, OUTPUT);
  digitalWrite(PIN_KB_VBUS_EN, LOW);
  pinMode(PIN_KB_CC_PU, OUTPUT);
  digitalWrite(PIN_KB_CC_PU, CC_RP_PULSED ? LOW : HIGH);
  // A button already down at power-on (LOW-I that woke us from System OFF, PAIR in update mode)
  // counts only once it is released and pressed again.
  pair_btn.down = digitalRead(PIN_BTN_PAIR) == LOW;
  lowi_btn.down = digitalRead(PIN_BTN_LOWI) == LOW;
  ignore_press = pair_btn.down || lowi_btn.down;
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  load_settings();
  vin_mv = read_mv(PIN_VIN_SENSE) * 2;     // now, not after the first period: the boot checks use it
  vin_low = vin_mv < VIN_LOW_MV;
  vin_ms = millis();
  power_task();
}

UiEvent ui_task() {
  power_task();
  led_task();
  return button_task();
}

LedMode ui_mode() { return mode; }

void ui_set_mode(LedMode m) {
  if (m == mode) return;
  mode = m;
  mode_since = millis();
}

void ui_flash(bool green, uint8_t times) {
  flash_green = green;
  flash_left = times * 2;
  flash_ms = millis();
}

KeepAlive ui_keepalive() { return (KeepAlive)settings.keepalive; }

uint32_t ui_ms_to_next_event() {
  uint32_t next = led_ms_to_next_edge();
  if (!ka_on) return next;
  uint32_t pulse = settings.keepalive == KA_WEAK ? KEEPALIVE_WEAK_PULSE_MS : KEEPALIVE_PULSE_MS;
  uint32_t t = millis() - ka_ms;
  return min(next, t >= pulse ? 0 : pulse - t);
}

KeepAlive ui_next_keepalive() {
  static const KeepAlive order[] = {KA_WEAK, KA_STRONG, KA_OFF};
  uint8_t i = 0;
  while (i < 3 && order[i] != settings.keepalive) i++;
  settings.keepalive = order[(i + 1) % 3];
  save_settings();
  return (KeepAlive)settings.keepalive;
}

uint8_t ui_host() { return settings.host; }

void ui_set_host(uint8_t host) {
  if (host == settings.host) return;
  settings.host = host;
  save_settings();
}

bool ui_battery_low() { return vin_low; }
uint16_t ui_vin_mv() { return vin_mv; }
bool ui_pair_held() { return digitalRead(PIN_BTN_PAIR) == LOW; }
bool ui_keyboard_powered() { return kb_power; }

void ui_kb_power_cycle(uint32_t off_ms) {
  kb_power = false;
  digitalWrite(PIN_KB_VBUS_EN, LOW);
  kb_off_until = millis() + off_ms;
  if (!kb_off_until) kb_off_until = 1;
}

void ui_system_off_prepare() {
  led(-1);
  digitalWrite(PIN_KEEPALIVE, LOW);
  kb_power = false;
  digitalWrite(PIN_KB_VBUS_EN, LOW);
  digitalWrite(PIN_KB_CC_PU, HIGH);   // Rp stays on in System OFF (pins keep their state)
  uint32_t cc = g_ADigitalPinMap[PIN_KB_CC_SENSE];
  nrf_gpio_cfg_input(cc, NRF_GPIO_PIN_NOPULL);
  delayMicroseconds(200);
  // wake on the other CC level: plugged in (Rd pulls CC low) or pulled out
  nrf_gpio_cfg_sense_input(cc, NRF_GPIO_PIN_NOPULL,
                           nrf_gpio_pin_read(cc) ? NRF_GPIO_PIN_SENSE_LOW : NRF_GPIO_PIN_SENSE_HIGH);
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[PIN_BTN_LOWI], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
}
