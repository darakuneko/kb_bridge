// KB Bridge nRF: own keymap + Vial protocol (see remap.h). Written from the VIA / Vial protocol
// and QMK keycode values; no QMK / Vial source is used.
#include "remap.h"
#include "vial_def.h"
#include <algorithm>

namespace {

// ---- protocol constants --------------------------------------------------------------------
constexpr uint16_t VIA_PROTOCOL = 0x0009;
constexpr uint32_t VIAL_PROTOCOL = 6;
const uint8_t VIAL_UID[8] = {0x4B, 0x42, 0x42, 0x52, 0x7A, 0x79, 0x15, 0x76};   // "KBBR" + VID/PID
constexpr uint32_t BLOB_MAGIC = 0x4B424B33;     // "KBK3" (9 x 22 matrix, conversion)
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
bool dirty = false;
uint32_t changed_ms = 0;

uint32_t layer_state = 0, default_layer_state = 1;
bool pos_down[REMAP_POSITIONS];
uint16_t pos_kc[REMAP_POSITIONS];             // keycode taken at press: its release undoes that
uint8_t key_count[256];                       // basic usages held (by keys and by mod+key keycodes)
uint8_t mod_count[8];
std::vector<uint8_t> order;                   // held usages, press order
uint16_t consumer = 0;
uint8_t system_usage = 0;
uint8_t buttons = 0;
int8_t wheel_v = 0, wheel_h = 0;

// conversion: the converted key pressed last decides the shift state while it is held
int conv_pos = -1;
uint8_t conv_shift = 0;

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
  } else if (kc >= 0xD9 && kc <= 0xDC && down) {          // wheel up / down / left / right: one step
    if (kc == 0xD9) wheel_v++;
    else if (kc == 0xDA) wheel_v--;
    else if (kc == 0xDB) wheel_h--;
    else wheel_h++;
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
    case VIAL_QMK_SETTINGS_QUERY:
      memset(msg, 0xFF, 32);                                // no QMK settings
      break;
    case VIAL_DYNAMIC_ENTRY_OP:
      if (msg[2] == 0) memset(msg, 0, 32);                  // 0 tap dance / combo / key override / alt repeat
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
  for (int l = 0; l < REMAP_LAYERS; l++)
    for (int p = 0; p < REMAP_POSITIONS; p++)
      blob.keymap[l][p] = l == 0 ? pos_usage(p) : (pos_usage(p) ? KC_TRNS : KC_NO);
}

bool remap_save_due() {
  if (!dirty || millis() - changed_ms < SAVE_DELAY_MS) return false;
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
}

void remap_key(uint8_t usage, bool pressed) {
  int p = remap_pos(usage);
  if (p < 0) {                                              // outside the matrix: as it came
    pressed ? usage_down(usage) : usage_up(usage);
    return;
  }
  if (pressed == pos_down[p]) return;
  pos_down[p] = pressed;
  if (pressed) {
    uint16_t kc = resolve(p);
    conv_pos = -1;                                          // a new key ends any shift override
    if (blob.conversion && kc >= 0x04 && kc <= 0xA4) {
      uint8_t shift = (mod_count[1] || mod_count[5]) ? 1 : 0;
      const Conv *c = blob.conversion == REMAP_CONV_JIS_TO_US ? find_conv(JIS_TO_US, kc, shift) : find_conv(US_TO_JIS, kc, shift);
      if (c) {
        kc = c->out;
        if (c->out_shift != shift) { conv_pos = p; conv_shift = c->out_shift; }
      }
    }
    pos_kc[p] = kc;
  } else if (p == conv_pos) {
    conv_pos = -1;
  }
  action(pos_kc[p], pressed);
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
bool remap_take_wheel(int8_t &v, int8_t &h) {
  if (!wheel_v && !wheel_h) return false;
  v = wheel_v; h = wheel_h;
  wheel_v = wheel_h = 0;
  return true;
}

bool remap_vial(uint8_t *msg) {
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
    case ID_KEYMAP_RESET:
    case ID_EEPROM_RESET:
      remap_defaults();
      touch();
      break;
    case ID_MACRO_GET_COUNT:
      d[0] = 0;
      break;
    case ID_MACRO_GET_BUFFER_SIZE:
      d[0] = d[1] = 0;
      break;
    case ID_MACRO_GET_BUFFER:
    case ID_MACRO_SET_BUFFER:
    case ID_MACRO_RESET:
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
