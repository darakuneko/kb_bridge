// KB Bridge nRF: own keymap + Vial protocol (see remap.h). Written from the VIA / Vial protocol
// and QMK keycode values; no QMK / Vial source is used.
#include "remap.h"
#include "vial_def.h"
#include <algorithm>
#include <cstddef>
#include <climits>

namespace {

// ---- protocol constants --------------------------------------------------------------------
constexpr uint16_t VIA_PROTOCOL = 0x0009;
constexpr uint32_t VIAL_PROTOCOL = 6;
const uint8_t VIAL_UID[8] = {0x4B, 0x42, 0x42, 0x52, 0x7A, 0x79, 0x15, 0x76};   // "KBBR" + VID/PID
constexpr uint32_t BLOB_MAGIC = 0x4B424B36;     // "KBK6" (+ combos, key overrides)
constexpr uint32_t DEFAULT_LAYOUT = 0;          // ALL
constexpr uint32_t SAVE_DELAY_MS = 3000;

enum : uint8_t {
  ID_GET_PROTOCOL_VERSION = 0x01, ID_GET_KEYBOARD_VALUE, ID_SET_KEYBOARD_VALUE,
  ID_KEYMAP_GET_KEYCODE, ID_KEYMAP_SET_KEYCODE, ID_KEYMAP_RESET,
  ID_EEPROM_RESET = 0x0A, ID_BOOTLOADER_JUMP,
  ID_MACRO_GET_COUNT, ID_MACRO_GET_BUFFER_SIZE, ID_MACRO_GET_BUFFER, ID_MACRO_SET_BUFFER, ID_MACRO_RESET,
  ID_GET_LAYER_COUNT, ID_KEYMAP_GET_BUFFER, ID_KEYMAP_SET_BUFFER,
  ID_VIAL_PREFIX = 0xFE, ID_UNHANDLED = 0xFF,
};
enum : uint8_t { KV_UPTIME = 0x01, KV_LAYOUT_OPTIONS, KV_SWITCH_MATRIX_STATE };
enum : uint8_t {
  VIAL_GET_KEYBOARD_ID = 0x00, VIAL_GET_SIZE, VIAL_GET_DEF, VIAL_GET_ENCODER, VIAL_SET_ENCODER,
  VIAL_GET_UNLOCK_STATUS, VIAL_UNLOCK_START, VIAL_UNLOCK_POLL, VIAL_LOCK,
  VIAL_QMK_SETTINGS_QUERY, VIAL_QMK_SETTINGS_GET, VIAL_QMK_SETTINGS_SET, VIAL_QMK_SETTINGS_RESET,
  VIAL_DYNAMIC_ENTRY_OP,
};

// unlock: hold Esc + Enter (positions of usages 0x29 and 0x28)
const uint8_t UNLOCK_USAGES[2] = {0x29, 0x28};
constexpr uint8_t UNLOCK_COUNT = 50;            // polls 100 ms apart while held

// ---- keycodes (QMK keycode values, Vial protocol 6) ----------------------------------------
constexpr uint16_t KC_NO = 0x0000, KC_TRNS = 0x0001;
constexpr uint16_t QK_MODS = 0x0100, QK_MODS_MAX = 0x1FFF;
constexpr uint16_t QK_LAYER_MOD = 0x5000, QK_LAYER_MOD_MAX = 0x51FF;
constexpr uint16_t QK_KB = 0x7E00;             // Vial custom keycodes (USER tab)
constexpr uint16_t QK_MOD_TAP = 0x2000, QK_MOD_TAP_MAX = 0x3FFF;       // MT(mods, kc)
constexpr uint16_t QK_LAYER_TAP = 0x4000, QK_LAYER_TAP_MAX = 0x4FFF;   // LT(layer, kc)
constexpr uint16_t QK_ONE_SHOT_LAYER = 0x5280, QK_ONE_SHOT_MOD = 0x52A0, QK_LAYER_TAP_TOGGLE = 0x52C0;
constexpr uint16_t QK_TAP_DANCE = 0x5700;
constexpr uint16_t QK_MACRO = 0x7700;
constexpr uint16_t QK_TO = 0x5200, QK_MOMENTARY = 0x5220, QK_DEF_LAYER = 0x5240,
                   QK_TOGGLE_LAYER = 0x5260, QK_PERSISTENT_DEF_LAYER = 0x52E0;

// keycode 0xA8.. -> consumer usage (HID Usage Tables)
uint16_t consumer_usage(uint8_t kc) {
  switch (kc) {
    case 0xA8: return 0x0E2;   // mute
    case 0xA9: return 0x0E9;   // volume up
    case 0xAA: return 0x0EA;   // volume down
    case 0xAB: return 0x0B5;   // next track
    case 0xAC: return 0x0B6;   // previous track
    case 0xAD: return 0x0B7;   // stop
    case 0xAE: return 0x0CD;   // play / pause
    case 0xAF: return 0x183;   // media select
    case 0xB0: return 0x0CC;   // eject
    case 0xB1: return 0x18A;   // mail
    case 0xB2: return 0x192;   // calculator
    case 0xB3: return 0x194;   // my computer
    case 0xB4: return 0x221;   // search
    case 0xB5: return 0x223;   // home
    case 0xB6: return 0x224;   // back
    case 0xB7: return 0x225;   // forward
    case 0xB8: return 0x226;   // stop
    case 0xB9: return 0x227;   // refresh
    case 0xBA: return 0x22A;   // bookmarks
    case 0xBB: return 0x0B3;   // fast forward
    case 0xBC: return 0x0B4;   // rewind
    case 0xBD: return 0x06F;   // brightness up
    case 0xBE: return 0x070;   // brightness down
    case 0xBF: return 0x19F;   // control panel
    case 0xC0: return 0x1CB;   // assistant
    case 0xC1: return 0x29F;   // mission control (show all windows)
    case 0xC2: return 0x2A0;   // launchpad
    default: return 0;
  }
}

// 5-bit QMK mod mask (ctrl, shift, alt, gui, right) -> HID modifier bits
uint8_t hid_mods(uint8_t m5) {
  uint8_t m = m5 & 0x0F;
  return (m5 & 0x10) ? (uint8_t)(m << 4) : m;
}

// ---- state ---------------------------------------------------------------------------------
// ---- JIS / US conversion -------------------------------------------------------------------
// The key with its shift state -> the key and shift state that type the same character on the
// other layout (Windows / Linux JIS layout). Only the keys whose characters differ.
struct Conv { uint8_t in, in_shift, out, out_shift; };
// US keyboard, OS set to JIS (KBB_US_TO_JIS)
const Conv US_TO_JIS[] = {
  {0x1F, 1, 0x2F, 0},  // @
  {0x23, 1, 0x2E, 0},  // ^
  {0x24, 1, 0x23, 1},  // &
  {0x25, 1, 0x34, 1},  // *
  {0x26, 1, 0x25, 1},  // (
  {0x27, 1, 0x26, 1},  // )
  {0x2D, 1, 0x87, 1},  // _
  {0x2E, 0, 0x2D, 1},  // =
  {0x2E, 1, 0x33, 1},  // +
  {0x2F, 0, 0x30, 0},  // [
  {0x2F, 1, 0x30, 1},  // {
  {0x30, 0, 0x32, 0},  // ]
  {0x30, 1, 0x32, 1},  // }
  {0x31, 0, 0x87, 0},  // backslash
  {0x31, 1, 0x89, 1},  // |
  {0x33, 1, 0x34, 0},  // :
  {0x34, 0, 0x24, 1},  // '
  {0x34, 1, 0x1F, 1},  // "
  {0x35, 0, 0x2F, 1},  // `
  {0x35, 1, 0x2E, 1},  // ~
};
// JIS keyboard, OS set to US (KBB_JIS_TO_US)
const Conv JIS_TO_US[] = {
  {0x1F, 1, 0x34, 1},  // "
  {0x23, 1, 0x24, 1},  // &
  {0x24, 1, 0x34, 0},  // '
  {0x25, 1, 0x26, 1},  // (
  {0x26, 1, 0x27, 1},  // )
  {0x2D, 1, 0x2E, 0},  // =
  {0x2E, 0, 0x23, 1},  // ^
  {0x2E, 1, 0x35, 1},  // ~
  {0x2F, 0, 0x1F, 1},  // @
  {0x2F, 1, 0x35, 0},  // `
  {0x30, 0, 0x2F, 0},  // [
  {0x30, 1, 0x2F, 1},  // {
  {0x32, 0, 0x30, 0},  // ]
  {0x32, 1, 0x30, 1},  // }
  {0x33, 1, 0x2E, 1},  // +
  {0x34, 0, 0x33, 1},  // :
  {0x34, 1, 0x25, 1},  // *
  {0x87, 0, 0x31, 0},  // backslash (Ro)
  {0x87, 1, 0x2D, 1},  // _
  {0x89, 0, 0x31, 0},  // Yen -> backslash
  {0x89, 1, 0x31, 1},  // |
};

template <size_t N>
const Conv *find_conv(const Conv (&t)[N], uint8_t kc, uint8_t shift) {
  for (const Conv &c : t)
    if (c.in == kc && c.in_shift == shift) return &c;
  return nullptr;
}

RemapBlob blob;
uint8_t macro_buf[REMAP_MACRO_BUFFER];
bool dirty = false;
uint32_t changed_ms = 0;
uint32_t last_vial_ms = 0;                    // last Vial request

uint32_t layer_state = 0, default_layer_state = 1;
bool pos_down[REMAP_POSITIONS];
uint16_t pos_kc[REMAP_POSITIONS];             // keycode taken at press: its release undoes that
uint8_t key_count[256];                       // basic usages held (by keys and by mod+key keycodes)
uint8_t mod_count[8];
std::vector<uint8_t> order;                   // held usages, press order
uint16_t consumer = 0;
uint8_t system_usage = 0;
uint8_t buttons = 0;
int16_t wheel_v = 0, wheel_h = 0;           // pending wheel steps
int16_t motion_x = 0, motion_y = 0;         // pending cursor motion
// Mouse keys: directions held, accelerations held, repeats, next move
uint8_t mk_dirs = 0;                        // bits: up, down, left, right
uint8_t mk_wheel_dirs = 0;                  // bits: up, down, left, right
uint8_t mk_accel = 0;                       // bits: ACL0, ACL1, ACL2
uint8_t mk_repeat = 0, mk_wheel_repeat = 0;
uint32_t mk_next = 0, mk_wheel_next = 0;

// conversion: the converted key pressed last decides the shift state while it is held
int conv_pos = -1;
uint8_t conv_shift = 0;

void (*report_hook)() = nullptr;
void report() { if (report_hook) report_hook(); }    // a report boundary (a tap: press, then release)

// Tap-hold (MT / LT): pressed, not yet known to be a tap or a hold. The key events after it wait
// here until that is decided (released before the tapping term: tap; the term over, or another
// key with the permissive hold / hold on other key press settings: hold), then go on in order.
struct KeyEv { uint8_t usage; bool pressed; };
struct Pending {
  bool active = false;
  int pos = -1;
  uint16_t kc = 0;
  uint32_t ms = 0;
  uint8_t n = 0;
  KeyEv buf[16];
} pend;
int last_tap_pos = -1;                        // quick tap: the same tap-hold key again, soon
uint32_t last_tap_ms = 0;
uint32_t press_seq = 0;                       // key presses so far (retro tapping)
int retro_pos = -1;                           // held tap-hold key that still taps if released alone
uint8_t retro_kc = 0;
uint32_t retro_seq = 0;
uint32_t pos_ms[REMAP_POSITIONS];             // press time

// One-shot mods / layer (OSM / OSL)
int os_key_pos = -1;                          // the one-shot key held now
bool os_key_used = false;                     // another key pressed while it was held
uint8_t osm_armed = 0, osm_locked = 0;        // HID mod bits for the next key / locked on
int8_t osl_armed = -1, osl_locked = -1;       // layer for the next key / locked on
uint32_t os_ms = 0;                           // armed at (timeout)
int os_tap_pos = -1;                          // tap toggle: taps of the same one-shot key
uint8_t os_taps = 0;
uint32_t os_tap_ms = 0;
uint8_t os_mods_at[REMAP_POSITIONS];          // one-shot mods given to a key, taken back at release

// Macro player: one action at a time from remap_task(), while the report queue has room.
bool (*ready_hook)() = nullptr;
struct Player {
  bool active = false;
  uint16_t off = 0;                           // next byte in macro_buf
  uint32_t wait_until = 0;                    // a delay
  uint8_t queued[4];                          // macros pressed while one plays
  uint8_t nq = 0;
} player;

// Combos: presses waiting for the rest of a combo; combos that went off
struct ComboWait {
  uint8_t n = 0;
  KeyEv ev[8];                                // the presses waiting (and nothing else)
  uint16_t kc[8];                             // their keycodes when pressed
  uint32_t ms = 0;                            // first press
} cw;
struct Fired {                                // a combo that went off: its output until a key of it is up
  bool on = false;
  uint16_t out = 0;
  uint8_t usage[4];
  uint8_t n = 0;
} fired[4];

// Tap dance: the one being counted, and what a finished one keeps down until its key is released
struct Dance {
  bool active = false;
  uint8_t idx = 0;
  int pos = -1;
  uint8_t count = 0;
  bool pressed = false, interrupted = false;
  uint32_t ms = 0;                            // last press: the entry's term runs from there
} dance;
int dance_held_pos = -1;
uint16_t dance_held_kc = 0;

// TT: taps per layer, and the layers it toggled on
uint8_t tt_taps[REMAP_LAYERS];
uint32_t tt_ms[REMAP_LAYERS];
uint32_t tt_toggled = 0;

uint8_t custom_queue[8], custom_head = 0, custom_tail = 0;

uint8_t unlocked = 0, unlock_busy = 0, unlock_left = UNLOCK_COUNT;
uint32_t unlock_ms = 0;

void touch() {
  dirty = true;
  changed_ms = millis();
}

uint8_t pos_usage(int p) { return REMAP_USAGE_AT[p]; }   // 0: no key there

void usage_down(uint8_t u) {
  if (u >= 0xE0 && u <= 0xE7) { mod_count[u - 0xE0]++; return; }
  if (!u) return;
  if (key_count[u]++ == 0) order.push_back(u);
}
void usage_up(uint8_t u) {
  if (u >= 0xE0 && u <= 0xE7) { if (mod_count[u - 0xE0]) mod_count[u - 0xE0]--; return; }
  if (!u || !key_count[u]) return;
  if (--key_count[u] == 0) order.erase(std::remove(order.begin(), order.end(), u), order.end());
}
void mods_down(uint8_t hid) { for (int i = 0; i < 8; i++) if (hid & (1 << i)) mod_count[i]++; }
void mods_up(uint8_t hid) { for (int i = 0; i < 8; i++) if ((hid & (1 << i)) && mod_count[i]) mod_count[i]--; }

uint16_t resolve(int p) {
  uint32_t active = layer_state | default_layer_state;
  for (int l = REMAP_LAYERS - 1; l >= 0; l--) {
    if (!(active & (1UL << l))) continue;
    uint16_t kc = blob.keymap[l][p];
    if (kc != KC_TRNS) return kc;
  }
  return KC_NO;
}

// ---- mouse keys (QMK's accelerated mode) --------------------------------------------------
uint16_t mk_or(uint16_t v, uint16_t def) { return v ? v : def; }

uint16_t mk_unit(bool wheel) {
  const RemapSettings &c = blob.settings;
  uint16_t delta = wheel ? 1 : mk_or(c.mk_move_delta, 8);
  uint16_t max = wheel ? mk_or(c.mk_wheel_max_speed, 8) : mk_or(c.mk_max_speed, 10);
  uint16_t ttm = wheel ? mk_or(c.mk_wheel_time_to_max, 40) : mk_or(c.mk_time_to_max, 30);
  uint8_t rep = wheel ? mk_wheel_repeat : mk_repeat;
  uint32_t u;
  if (mk_accel & 1) u = delta * max / 4;
  else if (mk_accel & 2) u = delta * max / 2;
  else if (mk_accel & 4) u = delta * max;
  else if (rep == 0) u = delta;
  else if (rep >= ttm) u = delta * max;
  else u = (uint32_t)delta * max * rep / ttm;
  return u > 127 ? 127 : (u ? u : 1);
}

void mk_move(bool wheel) {
  int16_t u = mk_unit(wheel);
  uint8_t d = wheel ? mk_wheel_dirs : mk_dirs;
  int16_t vy = ((d & 2) ? 1 : 0) - ((d & 1) ? 1 : 0);    // down - up
  int16_t vx = ((d & 8) ? 1 : 0) - ((d & 4) ? 1 : 0);    // right - left
  if (wheel) {
    wheel_v -= vy * u;                                    // wheel up = positive
    wheel_h += vx * u;
  } else {
    if (vx && vy) u = std::max<int16_t>(1, u * 181 / 256);  // diagonal: the same speed
    motion_x += vx * u;
    motion_y += vy * u;
  }
}

// A direction key pressed / released: the first press moves at once, the next move after the delay.
void mouse_key(uint8_t &dirs, uint8_t bit, bool down, bool wheel) {
  bool was = dirs != 0;
  dirs = down ? (dirs | bit) : (dirs & ~bit);
  if (!down) {
    if (!dirs) (wheel ? mk_wheel_repeat : mk_repeat) = 0;
    return;
  }
  const RemapSettings &c = blob.settings;
  if (!was) (wheel ? mk_wheel_repeat : mk_repeat) = 0;
  mk_move(wheel);
  uint32_t delay = wheel ? mk_or(c.mk_wheel_delay, 10) : mk_or(c.mk_delay, 10);
  (wheel ? mk_wheel_next : mk_next) = millis() + delay;
}

// Held direction keys: the next moves, faster each time.
bool mouse_task(uint32_t now) {
  const RemapSettings &c = blob.settings;
  bool moved = false;
  if (mk_dirs && (int32_t)(now - mk_next) >= 0) {
    if (mk_repeat < 255) mk_repeat++;
    mk_move(false);
    mk_next = now + mk_or(c.mk_interval, 20);
    moved = true;
  }
  if (mk_wheel_dirs && (int32_t)(now - mk_wheel_next) >= 0) {
    if (mk_wheel_repeat < 255) mk_wheel_repeat++;
    mk_move(true);
    mk_wheel_next = now + mk_or(c.mk_wheel_interval, 80);
    moved = true;
  }
  return moved;
}

void basic(uint8_t kc, bool down) {
  if ((kc >= 0x04 && kc <= 0xA4) || (kc >= 0xE0 && kc <= 0xE7)) {
    down ? usage_down(kc) : usage_up(kc);
  } else if (kc >= 0xA5 && kc <= 0xA7) {                  // system power / sleep / wake
    uint8_t u = 0x81 + (kc - 0xA5);
    if (down) system_usage = u;
    else if (system_usage == u) system_usage = 0;
  } else if (uint16_t u = consumer_usage(kc)) {
    if (down) consumer = u;
    else if (consumer == u) consumer = 0;
  } else if (kc >= 0xD1 && kc <= 0xD8) {                  // mouse buttons 1-8
    uint8_t b = 1 << (kc - 0xD1);
    buttons = down ? (buttons | b) : (buttons & ~b);
  } else if (kc >= 0xCD && kc <= 0xD0) {                  // cursor up / down / left / right
    mouse_key(mk_dirs, 1 << (kc - 0xCD), down, false);
  } else if (kc >= 0xD9 && kc <= 0xDC) {                  // wheel up / down / left / right
    mouse_key(mk_wheel_dirs, 1 << (kc - 0xD9), down, true);
  } else if (kc >= 0xDD && kc <= 0xDF) {                  // ACL0-2: fixed speed while held
    uint8_t b = 1 << (kc - 0xDD);
    mk_accel = down ? (mk_accel | b) : (mk_accel & ~b);
  }
}

void custom(uint8_t id) {
  if (id == KBB_JIS_TO_US || id == KBB_US_TO_JIS || id == KBB_NO_CONV) {
    uint8_t c = id == KBB_JIS_TO_US ? REMAP_CONV_JIS_TO_US : id == KBB_US_TO_JIS ? REMAP_CONV_US_TO_JIS : REMAP_CONV_NONE;
    if (blob.conversion != c) { blob.conversion = c; touch(); }
  }
  uint8_t next = (custom_head + 1) % sizeof(custom_queue);
  if (next != custom_tail) { custom_queue[custom_head] = id; custom_head = next; }
}

void action(uint16_t kc, bool down) {
  if (kc <= 0xFF) { basic((uint8_t)kc, down); return; }
  if (kc >= QK_KB && kc < QK_KB + KBB_CUSTOM_COUNT) {
    if (down) custom(kc - QK_KB);
    return;
  }
  if (kc >= QK_MACRO && kc < QK_MACRO + REMAP_MACROS) {
    if (down && player.nq < sizeof(player.queued)) player.queued[player.nq++] = kc - QK_MACRO;
    return;
  }
  if (kc >= QK_MODS && kc <= QK_MODS_MAX) {
    uint8_t m = hid_mods((kc >> 8) & 0x1F);
    if (down) { mods_down(m); basic(kc & 0xFF, true); }
    else { basic(kc & 0xFF, false); mods_up(m); }
    return;
  }
  if (kc >= QK_LAYER_MOD && kc <= QK_LAYER_MOD_MAX) {
    uint8_t layer = (kc >> 5) & 0x0F, m = hid_mods(kc & 0x1F);
    if (layer >= REMAP_LAYERS) return;
    if (down) { layer_state |= 1UL << layer; mods_down(m); }
    else { layer_state &= ~(1UL << layer); mods_up(m); }
    return;
  }
  uint8_t layer = kc & 0x1F;
  if (layer >= REMAP_LAYERS) return;
  switch (kc & 0xFFE0) {
    case QK_TO: if (down) layer_state = 1UL << layer; break;
    case QK_MOMENTARY: down ? (layer_state |= 1UL << layer) : (layer_state &= ~(1UL << layer)); break;
    case QK_DEF_LAYER:
    case QK_PERSISTENT_DEF_LAYER: if (down) default_layer_state = 1UL << layer; break;
    case QK_TOGGLE_LAYER: if (down) layer_state ^= 1UL << layer; break;
    default: break;                                         // not supported (yet): nothing
  }
}

void put32be(uint8_t *d, uint32_t v) { d[0] = v >> 24; d[1] = v >> 16; d[2] = v >> 8; d[3] = v; }

// ---- QMK settings (Vial protocol: query / get / set / reset by qsid) ------------------------
void settings_defaults() {
  RemapSettings &c = blob.settings;
  memset(&c, 0, sizeof(c));
  c.tapping_term = 200;
  c.quick_tap_term = 200;
  c.osk_timeout = 0;
  c.tapping_flags = 0;
  c.osk_tap_toggle = 0;
  c.tapping_toggle = 5;
  c.mk_delay = 10;
  c.mk_interval = 20;
  c.mk_move_delta = 8;
  c.mk_max_speed = 10;
  c.mk_time_to_max = 30;
  c.mk_wheel_delay = 10;
  c.mk_wheel_interval = 80;
  c.mk_wheel_max_speed = 8;
  c.mk_wheel_time_to_max = 40;
  c.combo_term = 50;
}

struct Qs { uint16_t qsid; uint8_t offset, width; int8_t bit; };   // bit -1: the whole value
const Qs QSETTINGS[] = {
  {2, offsetof(RemapSettings, combo_term), 2, -1},
  {5, offsetof(RemapSettings, osk_tap_toggle), 1, -1},
  {6, offsetof(RemapSettings, osk_timeout), 2, -1},
  {7, offsetof(RemapSettings, tapping_term), 2, -1},
  {9, offsetof(RemapSettings, mk_delay), 2, -1},
  {10, offsetof(RemapSettings, mk_interval), 2, -1},
  {11, offsetof(RemapSettings, mk_move_delta), 2, -1},
  {12, offsetof(RemapSettings, mk_max_speed), 2, -1},
  {13, offsetof(RemapSettings, mk_time_to_max), 2, -1},
  {14, offsetof(RemapSettings, mk_wheel_delay), 2, -1},
  {15, offsetof(RemapSettings, mk_wheel_interval), 2, -1},
  {16, offsetof(RemapSettings, mk_wheel_max_speed), 2, -1},
  {17, offsetof(RemapSettings, mk_wheel_time_to_max), 2, -1},
  {20, offsetof(RemapSettings, tapping_toggle), 1, -1},
  {22, offsetof(RemapSettings, tapping_flags), 1, 0},   // permissive hold
  {23, offsetof(RemapSettings, tapping_flags), 1, 1},   // hold on other key press
  {24, offsetof(RemapSettings, tapping_flags), 1, 2},   // retro tapping
  {25, offsetof(RemapSettings, quick_tap_term), 2, -1},
};

const Qs *find_qs(uint16_t qsid) {
  for (const Qs &q : QSETTINGS)
    if (q.qsid == qsid) return &q;
  return nullptr;
}

bool unlock_keys_held() {
  for (uint8_t u : UNLOCK_USAGES)
    if (!pos_down[remap_pos(u)]) return false;
  return true;
}

void vial_cmd(uint8_t *msg) {
  switch (msg[1]) {
    case VIAL_GET_KEYBOARD_ID:
      memset(msg, 0, 32);
      msg[0] = VIAL_PROTOCOL; msg[1] = VIAL_PROTOCOL >> 8; msg[2] = VIAL_PROTOCOL >> 16; msg[3] = VIAL_PROTOCOL >> 24;
      memcpy(msg + 4, VIAL_UID, 8);
      break;
    case VIAL_GET_SIZE: {
      uint32_t n = sizeof(VIAL_DEFINITION);
      msg[0] = n; msg[1] = n >> 8; msg[2] = n >> 16; msg[3] = n >> 24;
      break;
    }
    case VIAL_GET_DEF: {
      uint32_t start = (uint32_t)(msg[2] | (msg[3] << 8)) * 32;
      if (start >= sizeof(VIAL_DEFINITION)) break;
      uint32_t n = std::min<uint32_t>(32, sizeof(VIAL_DEFINITION) - start);
      memcpy(msg, VIAL_DEFINITION + start, n);
      break;
    }
    case VIAL_GET_ENCODER:
      memset(msg, 0, 4);
      break;
    case VIAL_GET_UNLOCK_STATUS:
      memset(msg, 0xFF, 32);
      msg[0] = unlocked;
      msg[1] = unlock_busy;
      for (int i = 0; i < 2; i++) {
        int p = remap_pos(UNLOCK_USAGES[i]);
        msg[2 + i * 2] = p / REMAP_COLS;
        msg[3 + i * 2] = p % REMAP_COLS;
      }
      break;
    case VIAL_UNLOCK_START:
      unlock_busy = 1;
      unlock_left = UNLOCK_COUNT;
      unlock_ms = millis();
      break;
    case VIAL_UNLOCK_POLL:
      if (unlock_busy) {
        if (millis() - unlock_ms > 100 && unlock_keys_held()) {
          unlock_ms = millis();
          if (--unlock_left == 0) { unlock_busy = 0; unlocked = 1; }
        } else if (!unlock_keys_held()) {
          unlock_left = UNLOCK_COUNT;
        }
      }
      msg[0] = unlocked;
      msg[1] = unlock_busy;
      msg[2] = unlock_left;
      break;
    case VIAL_LOCK:
      unlocked = 0;
      break;
    case VIAL_QMK_SETTINGS_QUERY: {                         // the qsids above the one asked for
      uint16_t gt = msg[2] | (msg[3] << 8);
      memset(msg, 0xFF, 32);
      int n = 0;
      for (const Qs &q : QSETTINGS)
        if (q.qsid > gt && n < 16) { msg[n * 2] = q.qsid; msg[n * 2 + 1] = q.qsid >> 8; n++; }
      break;
    }
    case VIAL_QMK_SETTINGS_GET: {
      const Qs *q = find_qs(msg[2] | (msg[3] << 8));
      if (!q) { msg[0] = 0xFF; break; }
      const uint8_t *v = (const uint8_t *)&blob.settings + q->offset;
      memset(msg, 0, 32);
      if (q->bit >= 0) msg[1] = (v[0] >> q->bit) & 1;
      else memcpy(msg + 1, v, q->width);                    // little endian, as stored
      break;
    }
    case VIAL_QMK_SETTINGS_SET: {
      const Qs *q = find_qs(msg[2] | (msg[3] << 8));
      if (!q) { msg[0] = 0xFF; break; }
      uint8_t *v = (uint8_t *)&blob.settings + q->offset;
      if (q->bit >= 0) v[0] = (v[0] & ~(1 << q->bit)) | ((msg[4] & 1) << q->bit);
      else memcpy(v, msg + 4, q->width);
      if (blob.settings.tapping_term < 10) blob.settings.tapping_term = 10;
      touch();
      msg[0] = 0;
      break;
    }
    case VIAL_QMK_SETTINGS_RESET:
      settings_defaults();
      touch();
      break;
    case VIAL_DYNAMIC_ENTRY_OP:
      switch (msg[2]) {
        case 0x00:                                          // counts: tap dance, combo, key override, alt repeat
          memset(msg, 0, 32);
          msg[0] = REMAP_TAP_DANCES;
          msg[1] = REMAP_COMBOS;
          break;
        case 0x01: {                                        // tap dance get: 5 x u16 LE
          uint8_t i = msg[3];
          if (i >= REMAP_TAP_DANCES) { msg[0] = 0xFF; break; }
          memset(msg, 0, 32);
          memcpy(msg + 1, &blob.tap_dance[i], sizeof(RemapTapDance));
          break;
        }
        case 0x02: {                                        // tap dance set
          uint8_t i = msg[3];
          if (i >= REMAP_TAP_DANCES) { msg[0] = 0xFF; break; }
          memcpy(&blob.tap_dance[i], msg + 4, sizeof(RemapTapDance));
          touch();
          msg[0] = 0;
          break;
        }
        case 0x03: {                                        // combo get: 4 inputs + output, u16 LE
          uint8_t i = msg[3];
          if (i >= REMAP_COMBOS) { msg[0] = 0xFF; break; }
          memset(msg, 0, 32);
          memcpy(msg + 1, &blob.combo[i], sizeof(RemapCombo));
          break;
        }
        case 0x04: {                                        // combo set
          uint8_t i = msg[3];
          if (i >= REMAP_COMBOS) { msg[0] = 0xFF; break; }
          memcpy(&blob.combo[i], msg + 4, sizeof(RemapCombo));
          touch();
          msg[0] = 0;
          break;
        }
        default:
          msg[0] = 0xFF;
          break;
      }
      break;
    default:
      break;
  }
}

}  // namespace

int remap_pos(uint8_t usage) { return REMAP_POS_OF[usage] == 0xFF ? -1 : REMAP_POS_OF[usage]; }

RemapBlob &remap_blob() { return blob; }

void remap_defaults() {
  blob.magic = BLOB_MAGIC;
  blob.layout_options = DEFAULT_LAYOUT;
  blob.conversion = REMAP_CONV_NONE;
  memset(blob.reserved, 0, sizeof(blob.reserved));
  settings_defaults();
  memset(blob.tap_dance, 0, sizeof(blob.tap_dance));
  memset(blob.combo, 0, sizeof(blob.combo));
  memset(blob.key_override, 0, sizeof(blob.key_override));
  for (auto &td : blob.tap_dance) td.tapping_term = 200;   // as Vial: each entry has its own term
  for (int l = 0; l < REMAP_LAYERS; l++)
    for (int p = 0; p < REMAP_POSITIONS; p++)
      blob.keymap[l][p] = l == 0 ? pos_usage(p) : (pos_usage(p) ? KC_TRNS : KC_NO);
}

// Saved once the changes are SAVE_DELAY_MS old and Vial has been quiet that long: a flash write
// stops the answers for a few hundred ms, and a request left unanswered for 500 ms is sent again by
// the GUI (Pipette), which then reads every answer one request late.
bool remap_save_due() {
  uint32_t now = millis();
  if (!dirty || now - changed_ms < SAVE_DELAY_MS || now - last_vial_ms < SAVE_DELAY_MS) return false;
  dirty = false;
  return true;
}

void remap_release_all() {
  memset(pos_down, 0, sizeof(pos_down));
  memset(key_count, 0, sizeof(key_count));
  memset(mod_count, 0, sizeof(mod_count));
  order.clear();
  consumer = 0;
  system_usage = 0;
  buttons = 0;
  wheel_v = wheel_h = 0;
  layer_state = 0;
  conv_pos = -1;
  pend.active = false;
  last_tap_pos = retro_pos = os_key_pos = os_tap_pos = -1;
  osm_armed = osm_locked = 0;
  osl_armed = osl_locked = -1;
  tt_toggled = 0;
  memset(tt_taps, 0, sizeof(tt_taps));
  dance.active = false;
  dance_held_pos = -1;
  player.active = false;
  player.nq = 0;
  mk_dirs = mk_wheel_dirs = mk_accel = 0;
  cw.n = 0;
  for (Fired &f : fired) f.on = false;
  motion_x = motion_y = 0;
  memset(os_mods_at, 0, sizeof(os_mods_at));
}

namespace {

const RemapSettings &cfg() { return blob.settings; }

bool is_tap_hold(uint16_t kc) {
  return (kc >= QK_MOD_TAP && kc <= QK_MOD_TAP_MAX) || (kc >= QK_LAYER_TAP && kc <= QK_LAYER_TAP_MAX);
}
// what a held MT / LT does: its mods (QK_MODS with no key) / MO(layer)
uint16_t hold_code(uint16_t kc) {
  if (kc <= QK_MOD_TAP_MAX) return ((kc >> 8) & 0x1F) << 8;
  return QK_MOMENTARY | ((kc >> 8) & 0x0F);
}

// a key that types something: the one-shot mods / layer apply to it and are used up
bool sends_key(uint16_t kc) {
  return (kc >= 0x04 && kc <= 0xFF && !(kc >= 0xE0 && kc <= 0xE7)) || (kc >= QK_MODS && kc <= QK_MODS_MAX && (kc & 0xFF));
}

void process_key(uint8_t usage, bool pressed);

// US layout: ASCII 0x20..0x7E -> keycode, bit 7 = with shift (what QMK's send_string types)
uint8_t ascii_key(char c) {
  if (c >= 'a' && c <= 'z') return 0x04 + (c - 'a');
  if (c >= 'A' && c <= 'Z') return 0x80 | (0x04 + (c - 'A'));
  if (c >= '1' && c <= '9') return 0x1E + (c - '1');
  switch (c) {
    case '0': return 0x27;  case '\n': return 0x28; case '\t': return 0x2B; case ' ': return 0x2C;
    case '!': return 0x80 | 0x1E; case '@': return 0x80 | 0x1F; case '#': return 0x80 | 0x20;
    case '$': return 0x80 | 0x21; case '%': return 0x80 | 0x22; case '^': return 0x80 | 0x23;
    case '&': return 0x80 | 0x24; case '*': return 0x80 | 0x25; case '(': return 0x80 | 0x26;
    case ')': return 0x80 | 0x27; case '-': return 0x2D; case '_': return 0x80 | 0x2D;
    case '=': return 0x2E; case '+': return 0x80 | 0x2E; case '[': return 0x2F; case '{': return 0x80 | 0x2F;
    case ']': return 0x30; case '}': return 0x80 | 0x30; case '\\': return 0x31; case '|': return 0x80 | 0x31;
    case ';': return 0x33; case ':': return 0x80 | 0x33; case '\'': return 0x34; case '"': return 0x80 | 0x34;
    case '`': return 0x35; case '~': return 0x80 | 0x35; case ',': return 0x36; case '<': return 0x80 | 0x36;
    case '.': return 0x37; case '>': return 0x80 | 0x37; case '/': return 0x38; case '?': return 0x80 | 0x38;
    default: return 0;
  }
}

// Start of macro n in the buffer, or -1 (no such macro, or the buffer is not 0-terminated: a
// write still going on).
int macro_start(uint8_t n) {
  if (macro_buf[REMAP_MACRO_BUFFER - 1] != 0) return -1;
  int off = 0;
  while (n) {
    if (off >= REMAP_MACRO_BUFFER) return -1;
    if (macro_buf[off++] == 0) n--;
  }
  return off < REMAP_MACRO_BUFFER ? off : -1;
}

uint8_t mbyte() { return player.off < REMAP_MACRO_BUFFER ? macro_buf[player.off++] : 0; }

// One action of the playing macro (a tap is a press and a release, each in its own report).
// false: the macro is over.
bool macro_step(uint32_t now) {
  uint8_t c = mbyte();
  if (c == 0) return false;
  if (c != 0x01) {                                          // text, typed on the US layout
    uint8_t k = ascii_key((char)c);
    if (!k) return true;
    uint16_t kc = (k & 0x80) ? (uint16_t)(0x0200 | (k & 0x7F)) : k;   // LSFT(kc)
    if (blob.conversion == REMAP_CONV_US_TO_JIS) {          // the OS is set to JIS
      const Conv *cv = find_conv(US_TO_JIS, k & 0x7F, (k & 0x80) ? 1 : 0);
      if (cv) kc = cv->out_shift ? (uint16_t)(0x0200 | cv->out) : cv->out;
    }
    action(kc, true); report(); action(kc, false); report();
    return true;
  }
  uint8_t op = mbyte();
  if (op >= 1 && op <= 3) {
    uint8_t kc = mbyte();
    if (!kc) return false;
    if (op != 3) action(kc, true);
    if (op == 1) report();
    if (op != 2) action(kc, false);
    report();
  } else if (op >= 5 && op <= 7) {
    uint8_t lo = mbyte(), hi = mbyte();
    if (!lo || !hi) return false;
    uint16_t kc = lo | (hi << 8);
    if (kc > 0xFF00) kc = (kc & 0xFF) << 8;                 // 0xFF01 = 0x0100 (no 0 bytes in the buffer)
    if (op != 7) action(kc, true);
    if (op == 5) report();
    if (op != 6) action(kc, false);
    report();
  } else if (op == 4) {
    uint8_t d0 = mbyte(), d1 = mbyte();
    if (!d0 || !d1) return false;
    player.wait_until = now + (d0 - 1) + (d1 - 1) * 255;
  } else {
    return false;
  }
  return true;
}

// true: something was sent
bool macro_task(uint32_t now) {
  bool did = false;
  for (int i = 0; i < 8; i++) {                             // a few actions per call, as the queue allows
    if (!player.active) {
      if (!player.nq) break;
      int start = macro_start(player.queued[0]);
      memmove(player.queued, player.queued + 1, --player.nq);
      if (start < 0) continue;
      player.active = true;
      player.off = start;
      player.wait_until = now;
    }
    if ((int32_t)(now - player.wait_until) < 0) break;
    if (ready_hook && !ready_hook()) break;
    if (!macro_step(now)) player.active = false;
    did = true;
  }
  return did;
}

void send_press(int p, uint16_t kc) {
  if (blob.conversion && kc >= 0x04 && kc <= 0xA4) {
    uint8_t shift = (mod_count[1] || mod_count[5] || (osm_armed & 0x22)) ? 1 : 0;
    const Conv *c = blob.conversion == REMAP_CONV_JIS_TO_US ? find_conv(JIS_TO_US, kc, shift) : find_conv(US_TO_JIS, kc, shift);
    if (c) {
      kc = c->out;
      if (c->out_shift != shift) { conv_pos = p; conv_shift = c->out_shift; }
    }
  }
  bool typing = sends_key(kc);
  if (typing && osm_armed) {                                // one-shot mods: on this key only
    os_mods_at[p] = osm_armed;
    mods_down(osm_armed);
    osm_armed = 0;
  }
  pos_kc[p] = kc;
  action(kc, true);
  if (typing && osl_armed >= 0) {                           // one-shot layer: used by this key
    if (osl_armed != osl_locked) layer_state &= ~(1UL << osl_armed);
    osl_armed = -1;
  }
}

void tap_code(uint16_t kc) {
  action(kc, true);
  report();
  action(kc, false);
  report();
}

// As a Vial keyboard behaves: while the key is held, the entry's own term decides a hold; once it
// is released, the tapping term setting decides when the taps are counted (both from the press).
uint32_t dance_term() {
  uint16_t t = blob.tap_dance[dance.idx].tapping_term;
  return dance.pressed && t ? t : cfg().tapping_term;
}

// The counting is over (time, another key, or another tap dance): what the taps and holds mean.
void dance_finish() {
  const RemapTapDance &td = blob.tap_dance[dance.idx];
  dance.active = false;
  uint16_t down = 0;
  if (dance.count == 1) {
    down = (dance.interrupted || !dance.pressed) ? td.on_tap : (td.on_hold ? td.on_hold : td.on_tap);
  } else if (dance.count == 2) {
    if (dance.interrupted) {                                 // two single taps
      if (td.on_tap) { tap_code(td.on_tap); down = td.on_tap; }
    } else if (dance.pressed) {                              // tap, then hold
      if (td.on_tap_hold) down = td.on_tap_hold;
      else if (td.on_tap) { tap_code(td.on_tap); down = td.on_hold ? td.on_hold : td.on_tap; }
      else down = td.on_hold;
    } else if (td.on_double_tap) {
      down = td.on_double_tap;
    } else if (td.on_tap) {
      tap_code(td.on_tap); down = td.on_tap;
    }
  }                                                          // 3 and more: typed while tapping
  if (!down) return;
  action(down, true);
  if (dance.pressed) { dance_held_pos = dance.pos; dance_held_kc = down; }
  else { report(); action(down, false); }
}

void release(int p, uint32_t now) {
  pos_down[p] = false;
  uint16_t kc = pos_kc[p];
  if (p == conv_pos) conv_pos = -1;
  if (kc >= QK_ONE_SHOT_MOD && kc < QK_ONE_SHOT_MOD + 0x20) {
    uint8_t m = hid_mods(kc & 0x1F);
    mods_up(m);
    if (!os_key_used && now - pos_ms[p] < cfg().tapping_term) {   // tapped
      if (osm_locked & m) { mods_up(osm_locked & m); osm_locked &= ~m; osm_armed &= ~m; }
      else {
        os_taps = (os_tap_pos == p && now - os_tap_ms < cfg().tapping_term) ? os_taps + 1 : 1;
        os_tap_pos = p;
        os_tap_ms = now;
        if (cfg().osk_tap_toggle && os_taps >= cfg().osk_tap_toggle) {
          osm_locked |= m; mods_down(m); osm_armed &= ~m; os_taps = 0;
        } else {
          osm_armed |= m; os_ms = now;
        }
      }
    }
    os_key_pos = -1;
  } else if (kc >= QK_ONE_SHOT_LAYER && kc < QK_ONE_SHOT_LAYER + 0x20) {
    int8_t l = kc & 0x1F;
    bool tap = !os_key_used && now - pos_ms[p] < cfg().tapping_term;
    if (tap && osl_locked == l) {                           // tapped while locked: off
      osl_locked = -1; osl_armed = -1; layer_state &= ~(1UL << l);
    } else if (tap) {
      os_taps = (os_tap_pos == p && now - os_tap_ms < cfg().tapping_term) ? os_taps + 1 : 1;
      os_tap_pos = p;
      os_tap_ms = now;
      if (cfg().osk_tap_toggle && os_taps >= cfg().osk_tap_toggle) { osl_locked = l; osl_armed = -1; os_taps = 0; }
      else { osl_armed = l; os_ms = now; }                  // the layer stays on for the next key
    } else if (osl_locked != l && osl_armed != l) {
      layer_state &= ~(1UL << l);                           // held: it was MO
    }
    os_key_pos = -1;
  } else if (kc >= QK_TAP_DANCE && kc < QK_TAP_DANCE + REMAP_TAP_DANCES) {
    if (dance.active && dance.pos == p) dance.pressed = false;   // the term runs from the press
    else if (dance_held_pos == p) { action(dance_held_kc, false); dance_held_pos = -1; }
  } else if (kc >= QK_LAYER_TAP_TOGGLE && kc < QK_LAYER_TAP_TOGGLE + 0x20) {
    uint8_t l = kc & 0x1F;
    if (now - pos_ms[p] < cfg().tapping_term) {             // tapped: count toward toggling
      tt_taps[l] = now - tt_ms[l] < cfg().tapping_term ? tt_taps[l] + 1 : 1;
      tt_ms[l] = now;
      if (cfg().tapping_toggle && tt_taps[l] >= cfg().tapping_toggle) { tt_toggled ^= 1UL << l; tt_taps[l] = 0; }
    }
    if (tt_toggled & (1UL << l)) layer_state |= 1UL << l;
    else layer_state &= ~(1UL << l);
  } else {
    action(kc, false);
  }
  if (os_mods_at[p]) { mods_up(os_mods_at[p]); os_mods_at[p] = 0; }
  if (p == retro_pos) {                                     // held alone past the term: still a tap
    retro_pos = -1;
    if (press_seq == retro_seq && retro_kc) {
      report();
      action(retro_kc, true);
      report();
      action(retro_kc, false);
    }
  }
}

void press(int p, uint32_t now) {
  pos_down[p] = true;
  pos_ms[p] = now;
  press_seq++;
  conv_pos = -1;                                            // a new key ends any shift override
  if (os_key_pos >= 0) os_key_used = true;
  uint16_t kc = resolve(p);
  bool is_dance = kc >= QK_TAP_DANCE && kc < QK_TAP_DANCE + REMAP_TAP_DANCES;
  if (dance.active && (!is_dance || dance.pos != p)) {       // another key: the dance is over
    dance.interrupted = true;
    dance_finish();
    report();
  }
  if (is_dance) {
    if (!dance.active) {
      dance.active = true; dance.idx = kc - QK_TAP_DANCE; dance.pos = p;
      dance.count = 0; dance.interrupted = false;
    }
    dance.count++;
    dance.pressed = true;
    dance.ms = now;
    pos_kc[p] = kc;
    uint16_t t = blob.tap_dance[dance.idx].on_tap;
    if (t && dance.count == 3) { tap_code(t); tap_code(t); tap_code(t); }
    else if (t && dance.count > 3) tap_code(t);
    return;
  }
  if (is_tap_hold(kc)) {
    if (p == last_tap_pos && now - last_tap_ms < cfg().quick_tap_term) {
      send_press(p, kc & 0xFF);                             // tap, then press again: the tap key held
      return;
    }
    pend.active = true; pend.pos = p; pend.kc = kc; pend.ms = now; pend.n = 0;
    return;
  }
  last_tap_pos = -1;
  if (kc >= QK_ONE_SHOT_MOD && kc < QK_ONE_SHOT_MOD + 0x20) {
    pos_kc[p] = kc;
    mods_down(hid_mods(kc & 0x1F));                         // held: a plain modifier
    os_key_pos = p; os_key_used = false;
    return;
  }
  if (kc >= QK_ONE_SHOT_LAYER && kc < QK_ONE_SHOT_LAYER + 0x20) {
    if ((kc & 0x1F) >= REMAP_LAYERS) { pos_kc[p] = KC_NO; return; }
    pos_kc[p] = kc;
    layer_state |= 1UL << (kc & 0x1F);                      // held: MO
    os_key_pos = p; os_key_used = false;
    return;
  }
  if (kc >= QK_LAYER_TAP_TOGGLE && kc < QK_LAYER_TAP_TOGGLE + 0x20) {
    if ((kc & 0x1F) >= REMAP_LAYERS) { pos_kc[p] = KC_NO; return; }
    pos_kc[p] = kc;
    layer_state |= 1UL << (kc & 0x1F);                      // held: MO
    return;
  }
  send_press(p, kc);
}

// The events that waited for a tap-hold decision, each with its own report (a key pressed and
// released while waiting must still be seen).
void replay(const Pending &pd) {
  for (uint8_t i = 0; i < pd.n; i++) {
    if (i) report();
    process_key(pd.buf[i].usage, pd.buf[i].pressed);
  }
}

// The pending tap-hold key is held: its hold action, then the events that waited.
void decide_hold() {
  Pending pd = pend;
  pend.active = false;
  pos_kc[pd.pos] = hold_code(pd.kc);
  action(pos_kc[pd.pos], true);
  retro_pos = -1;
  if ((cfg().tapping_flags & REMAP_TAP_RETRO) && pd.n == 0) {
    retro_pos = pd.pos; retro_kc = pd.kc & 0xFF; retro_seq = press_seq;
  }
  replay(pd);
}

void process_key(uint8_t usage, bool pressed) {
  uint32_t now = millis();
  int p = remap_pos(usage);
  if (pend.active) {
    if (p == pend.pos && !pressed) {                        // the tap-hold key released
      if (now - pend.ms < cfg().tapping_term) {             // tap: press, release, then the rest
        Pending pd = pend;
        pend.active = false;
        send_press(p, pd.kc & 0xFF);
        report();
        release(p, now);
        last_tap_pos = p;
        last_tap_ms = now;
        if (pd.n) report();                                 // the tap's release on its own
        replay(pd);
      } else {
        decide_hold();
        release(p, now);
      }
      return;
    }
    if (pend.n == sizeof(pend.buf) / sizeof(pend.buf[0])) {
      decide_hold();
      process_key(usage, pressed);
      return;
    }
    pend.buf[pend.n++] = {usage, pressed};
    if (pressed && (cfg().tapping_flags & REMAP_TAP_HOLD_ON_OTHER_KEY)) { decide_hold(); return; }
    if (!pressed && (cfg().tapping_flags & REMAP_TAP_PERMISSIVE_HOLD)) {
      for (uint8_t i = 0; i + 1 < pend.n; i++)              // a key pressed and released inside it
        if (pend.buf[i].usage == usage && pend.buf[i].pressed) { decide_hold(); return; }
    }
    return;
  }
  if (p < 0) {                                              // outside the matrix: as it came
    pressed ? usage_down(usage) : usage_up(usage);
    return;
  }
  if (pressed == pos_down[p]) return;
  pressed ? press(p, now) : release(p, now);
}

}  // namespace

// ---- combos: before everything else (a key that belongs to a combo waits for the others) ----
namespace {


uint32_t combo_term() { return blob.settings.combo_term ? blob.settings.combo_term : 50; }

bool combo_used(const RemapCombo &c) { return c.output && c.input[0] && c.input[1]; }

bool in_any_combo(uint16_t kc) {
  if (!kc) return false;
  for (const RemapCombo &c : blob.combo)
    if (combo_used(c))
      for (uint16_t i : c.input)
        if (i == kc) return true;
  return false;
}

// how the waiting keys stand to combo c: 2 = exactly its keys, 1 = some of them, 0 = no
int combo_match(const RemapCombo &c) {
  if (!combo_used(c)) return 0;
  int need = 0;
  for (uint16_t i : c.input) if (i) need++;
  for (uint8_t k = 0; k < cw.n; k++) {
    bool found = false;
    for (uint16_t i : c.input) if (i && i == cw.kc[k]) found = true;
    if (!found) return 0;
  }
  return cw.n == need ? 2 : 1;
}

void combo_flush() {                          // no combo: the waiting keys as they came
  uint8_t n = cw.n;
  KeyEv ev[8];
  memcpy(ev, cw.ev, sizeof(ev));
  cw.n = 0;
  for (uint8_t i = 0; i < n; i++) {
    if (i) report();
    process_key(ev[i].usage, true);
  }
}

void combo_fire(const RemapCombo &c) {
  Fired *f = nullptr;
  for (Fired &x : fired) if (!x.on) { f = &x; break; }
  if (!f) { combo_flush(); return; }
  f->on = true;
  f->out = c.output;
  f->n = cw.n > 4 ? 4 : cw.n;
  for (uint8_t i = 0; i < f->n; i++) f->usage[i] = cw.ev[i].usage;
  cw.n = 0;
  action(c.output, true);
}

// the waiting keys after a new press or the term: a combo, more waiting, or none
void combo_check(bool term_over) {
  const RemapCombo *exact = nullptr;
  bool more = false;
  for (const RemapCombo &c : blob.combo) {
    int m = combo_match(c);
    if (m == 2 && !exact) exact = &c;
    if (m == 1) more = true;                  // a longer combo could still come
  }
  if (exact && (!more || term_over)) combo_fire(*exact);
  else if (!more || term_over) combo_flush();
}

}  // namespace

void remap_key(uint8_t usage, bool pressed) {
  int p = remap_pos(usage);
  if (pressed) {
    uint16_t kc = p >= 0 ? resolve(p) : 0;
    if (p < 0 || !in_any_combo(kc)) {
      if (cw.n) { combo_flush(); report(); }
      process_key(usage, true);
      return;
    }
    if (cw.n == sizeof(cw.ev) / sizeof(cw.ev[0])) combo_flush();
    if (!cw.n) cw.ms = millis();
    cw.ev[cw.n] = {usage, true};
    cw.kc[cw.n++] = kc;
    combo_check(false);
    return;
  }
  for (Fired &f : fired) {                    // a key of a combo that went off
    if (!f.on) continue;
    for (uint8_t i = 0; i < f.n; i++) {
      if (f.usage[i] != usage) continue;
      f.usage[i] = f.usage[--f.n];
      if (f.out) { action(f.out, false); f.out = 0; }   // the first key up ends the output
      if (!f.n) f.on = false;
      return;
    }
  }
  for (uint8_t i = 0; i < cw.n; i++)          // released before the combo was complete
    if (cw.ev[i].usage == usage) { combo_flush(); report(); break; }
  process_key(usage, false);
}

void remap_set_report_hook(void (*hook)()) { report_hook = hook; }
void remap_set_ready_hook(bool (*ready)()) { ready_hook = ready; }
uint8_t *remap_macro_buffer() { return macro_buf; }
void remap_macros_clear() { memset(macro_buf, 0, sizeof(macro_buf)); }

bool remap_task() {
  uint32_t now = millis();
  bool changed = false;
  if (cw.n && now - cw.ms >= combo_term()) {
    combo_check(true);
    changed = true;
  }
  if (pend.active && now - pend.ms >= cfg().tapping_term) {
    decide_hold();
    changed = true;
  }
  if (dance.active && now - dance.ms >= dance_term()) {
    dance_finish();
    changed = true;
  }
  if ((player.active || player.nq) && macro_task(now)) changed = true;
  if ((mk_dirs || mk_wheel_dirs) && mouse_task(now)) changed = true;
  if (cfg().osk_timeout && (osm_armed || (osl_armed >= 0 && osl_armed != osl_locked)) &&
      now - os_ms >= cfg().osk_timeout) {
    osm_armed = 0;
    if (osl_armed >= 0 && osl_armed != osl_locked) layer_state &= ~(1UL << osl_armed);
    osl_armed = -1;
    changed = true;
  }
  return changed;
}

uint32_t remap_ms_to_next_event() {
  uint32_t now = millis(), next = UINT32_MAX;
  if (cw.n) {
    uint32_t t = now - cw.ms;
    next = std::min<uint32_t>(next, t >= combo_term() ? 0 : combo_term() - t);
  }
  if (pend.active) {
    uint32_t t = now - pend.ms;
    next = t >= cfg().tapping_term ? 0 : cfg().tapping_term - t;
  }
  if (mk_dirs) {
    int32_t w = (int32_t)(mk_next - now);
    next = std::min<uint32_t>(next, w > 0 ? (uint32_t)w : 0);
  }
  if (mk_wheel_dirs) {
    int32_t w = (int32_t)(mk_wheel_next - now);
    next = std::min<uint32_t>(next, w > 0 ? (uint32_t)w : 0);
  }
  if (player.active || player.nq) {
    int32_t w = (int32_t)(player.wait_until - now);
    next = std::min<uint32_t>(next, w > 0 ? (uint32_t)w : 1);
  }
  if (dance.active) {
    uint32_t t = now - dance.ms;
    next = std::min<uint32_t>(next, t >= dance_term() ? 0 : dance_term() - t);
  }
  if (cfg().osk_timeout && (osm_armed || osl_armed >= 0)) {
    uint32_t t = now - os_ms;
    next = std::min<uint32_t>(next, t >= cfg().osk_timeout ? 0 : cfg().osk_timeout - t);
  }
  return next;
}

bool remap_take_custom(uint8_t &id) {
  if (custom_tail == custom_head) return false;
  id = custom_queue[custom_tail];
  custom_tail = (custom_tail + 1) % sizeof(custom_queue);
  return true;
}

uint8_t remap_conversion() { return blob.conversion; }

uint8_t remap_mods() {
  uint8_t m = 0;
  for (int i = 0; i < 8; i++)
    if (mod_count[i]) m |= 1 << i;
  if (conv_pos >= 0) m = conv_shift ? (m & ~0x20) | 0x02 : m & ~0x22;   // converted key: its shift
  return m;
}
const std::vector<uint8_t> &remap_keys() { return order; }
uint16_t remap_consumer() { return consumer; }
uint8_t remap_system() { return system_usage; }
uint8_t remap_buttons() { return buttons; }
bool remap_take_motion(int16_t &x, int16_t &y, int16_t &v, int16_t &h) {
  if (!motion_x && !motion_y && !wheel_v && !wheel_h) return false;
  x = motion_x; y = motion_y; v = wheel_v; h = wheel_h;
  motion_x = motion_y = wheel_v = wheel_h = 0;
  return true;
}

bool remap_vial(uint8_t *msg) {
  last_vial_ms = millis();
  uint8_t *d = msg + 1;
  // while an unlock is running, only the Vial commands it needs
  if (unlock_busy && !(msg[0] == ID_VIAL_PREFIX && (msg[1] <= VIAL_GET_DEF || (msg[1] >= VIAL_GET_UNLOCK_STATUS && msg[1] <= VIAL_UNLOCK_POLL))))
    return true;
  switch (msg[0]) {
    case ID_GET_PROTOCOL_VERSION:
      d[0] = VIA_PROTOCOL >> 8;
      d[1] = VIA_PROTOCOL & 0xFF;
      break;
    case ID_GET_KEYBOARD_VALUE:
      if (d[0] == KV_UPTIME) put32be(d + 1, millis());
      else if (d[0] == KV_LAYOUT_OPTIONS) put32be(d + 1, blob.layout_options);
      else if (d[0] == KV_SWITCH_MATRIX_STATE) {
        if (!unlocked) break;                               // no key logging while locked
        // QMK layout: per row (REMAP_COLS + 7) / 8 bytes, big endian, bit = column
        constexpr int N = (REMAP_COLS + 7) / 8;
        static_assert(1 + REMAP_ROWS * N <= 31, "matrix state must fit one report");
        for (int r = 0; r < REMAP_ROWS; r++) {
          uint32_t v = 0;
          for (int c = 0; c < REMAP_COLS; c++)
            if (pos_down[r * REMAP_COLS + c]) v |= 1UL << c;
          for (int b = 0; b < N; b++) d[1 + r * N + b] = v >> (8 * (N - 1 - b));
        }
      } else msg[0] = ID_UNHANDLED;
      break;
    case ID_SET_KEYBOARD_VALUE:
      if (d[0] == KV_LAYOUT_OPTIONS) {
        blob.layout_options = ((uint32_t)d[1] << 24) | ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 8) | d[4];
        touch();
      } else msg[0] = ID_UNHANDLED;
      break;
    case ID_KEYMAP_GET_KEYCODE: {
      int p = d[1] * REMAP_COLS + d[2];
      uint16_t kc = (d[0] < REMAP_LAYERS && d[1] < REMAP_ROWS && d[2] < REMAP_COLS) ? blob.keymap[d[0]][p] : 0;
      d[3] = kc >> 8;
      d[4] = kc & 0xFF;
      break;
    }
    case ID_KEYMAP_SET_KEYCODE:
      if (d[0] < REMAP_LAYERS && d[1] < REMAP_ROWS && d[2] < REMAP_COLS) {
        blob.keymap[d[0]][d[1] * REMAP_COLS + d[2]] = (d[3] << 8) | d[4];
        touch();
      }
      break;
    case ID_EEPROM_RESET:
      memset(macro_buf, 0, sizeof(macro_buf));
      [[fallthrough]];
    case ID_KEYMAP_RESET:
      remap_defaults();
      touch();
      break;
    case ID_MACRO_GET_COUNT:
      d[0] = REMAP_MACROS;
      break;
    case ID_MACRO_GET_BUFFER_SIZE:
      d[0] = REMAP_MACRO_BUFFER >> 8;
      d[1] = REMAP_MACRO_BUFFER & 0xFF;
      break;
    case ID_MACRO_GET_BUFFER:
    case ID_MACRO_SET_BUFFER: {
      uint16_t off = (d[0] << 8) | d[1];
      uint8_t n = d[2];
      if (n > 28) break;
      if (msg[0] == ID_MACRO_SET_BUFFER && !unlocked) break;   // as Vial: macros only once unlocked
      for (uint8_t i = 0; i < n && off + i < REMAP_MACRO_BUFFER; i++) {
        if (msg[0] == ID_MACRO_GET_BUFFER) d[3 + i] = macro_buf[off + i];
        else macro_buf[off + i] = d[3 + i];
      }
      if (msg[0] == ID_MACRO_SET_BUFFER) touch();
      break;
    }
    case ID_MACRO_RESET:
      memset(macro_buf, 0, sizeof(macro_buf));
      touch();
      break;
    case ID_BOOTLOADER_JUMP:                                // never from here
      break;
    case ID_GET_LAYER_COUNT:
      d[0] = REMAP_LAYERS;
      break;
    case ID_KEYMAP_GET_BUFFER:
    case ID_KEYMAP_SET_BUFFER: {
      uint16_t off = (d[0] << 8) | d[1];
      uint8_t n = d[2];
      if (n > 28) break;
      for (uint8_t i = 0; i < n; i++) {
        uint32_t o = (uint32_t)off + i;
        uint32_t idx = o / 2;                               // keycode index: layer * POSITIONS + pos
        if (idx >= (uint32_t)REMAP_LAYERS * REMAP_POSITIONS) break;
        uint16_t &kc = blob.keymap[idx / REMAP_POSITIONS][idx % REMAP_POSITIONS];
        if (msg[0] == ID_KEYMAP_GET_BUFFER) d[3 + i] = (o & 1) ? (kc & 0xFF) : (kc >> 8);
        else kc = (o & 1) ? ((kc & 0xFF00) | d[3 + i]) : ((kc & 0x00FF) | (d[3 + i] << 8));
      }
      if (msg[0] == ID_KEYMAP_SET_BUFFER) touch();
      break;
    }
    case ID_VIAL_PREFIX:
      vial_cmd(msg);
      break;
    default:
      msg[0] = ID_UNHANDLED;
      break;
  }
  return true;
}

// usable before remap_store_load() (and in host tests): start from the default keymap
static struct RemapInit { RemapInit() { remap_defaults(); } } remap_init;
