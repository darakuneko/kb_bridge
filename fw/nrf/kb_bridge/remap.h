// KB Bridge nRF: its own keymap and Vial protocol, for keyboards without Vial / VIA (no 0xFF60
// interface). Keyboards with one keep their own Vial (pass-through, bridge.cpp).
//
// The keyboard is seen only through its HID usages, so the "matrix" is virtual: one row per row of
// keys of the Vial layout, columns from the left, then the usages a full-size layout has no place
// for (9 rows x 22 columns). fw/tools/vial_def.py builds it (remap_matrix.h) with the matching
// Vial definition (vial_def.h).
#pragma once
#include <Arduino.h>
#include <vector>

#include "remap_matrix.h"
#define REMAP_POSITIONS (REMAP_ROWS * REMAP_COLS)
#define REMAP_LAYERS    8

int remap_pos(uint8_t usage);                 // -1: not in the matrix (passed through as is)

void remap_defaults();                        // layer 0 = what the keyboard sends, others transparent
void remap_release_all();                     // every key up, layers back to the default layer
void remap_key(uint8_t usage, bool pressed);  // one key event from the keyboard
// Timers (tap-hold decisions, one-shot timeout): call every loop. true: what the keymap sends changed.
bool remap_task();
uint32_t remap_ms_to_next_event();            // when remap_task() has something to do (UINT32_MAX: nothing)
// Called whenever the keymap needs a report sent before it goes on (a tap: press, then release).
void remap_set_report_hook(void (*hook)());

// What the keymap currently sends.
uint8_t remap_mods();
const std::vector<uint8_t> &remap_keys();     // press order
uint16_t remap_consumer();
uint8_t remap_system();
uint8_t remap_buttons();
bool remap_take_wheel(int8_t &v, int8_t &h);  // pending wheel steps from wheel keycodes

// Vial custom keycodes (USER tab; QK_KB_0 + index, fw/tools/vial_def.py "customKeycodes").
// The JIS / US conversion ones are handled here; every one is also queued for the main loop
// (host switch, keep-alive, LED feedback).
enum : uint8_t {
  KBB_HOST1, KBB_HOST2, KBB_HOST3, KBB_HOST4, KBB_KEEPALIVE, KBB_JIS_TO_US, KBB_US_TO_JIS, KBB_NO_CONV,
  KBB_CUSTOM_COUNT
};
bool remap_take_custom(uint8_t &id);          // a custom key pressed since the last call

// JIS / US conversion (persisted): what the OS gets is what the key's legend shows.
enum : uint8_t { REMAP_CONV_NONE, REMAP_CONV_JIS_TO_US, REMAP_CONV_US_TO_JIS };
uint8_t remap_conversion();

// Vial / VIA raw HID command, answered in place (32 bytes). false: send no answer.
bool remap_vial(uint8_t msg[32]);

// Storage (remap_store.cpp on the nRF): the keymap and layout options as one blob.
// QMK settings Vial can change (Behavior / QMK settings tab), little endian as on the wire.
struct RemapSettings {
  uint16_t tapping_term;                      // qsid 7: ms until a tap-hold key counts as held
  uint16_t quick_tap_term;                    // qsid 25: tap then press again within this: tap held
  uint16_t osk_timeout;                       // qsid 6: one-shot given up after this (0: never)
  uint8_t tapping_flags;                      // qsid 22 / 23 / 24: REMAP_TAP_* bits
  uint8_t osk_tap_toggle;                     // qsid 5: one-shot tapped this often locks (0: never)
  uint8_t tapping_toggle;                     // qsid 20: TT tapped this often toggles its layer
  uint8_t reserved[55];                       // later settings without a new file format
};
enum : uint8_t { REMAP_TAP_PERMISSIVE_HOLD = 1, REMAP_TAP_HOLD_ON_OTHER_KEY = 2, REMAP_TAP_RETRO = 4 };

// Tap dance (Vial Tap Dance tab): TD(n) = QK_TAP_DANCE + n.
#define REMAP_TAP_DANCES 16
struct RemapTapDance {
  uint16_t on_tap, on_hold, on_double_tap, on_tap_hold;
  uint16_t tapping_term;                      // held this long: a hold (0: the tapping term setting)
};
static_assert(sizeof(RemapTapDance) == 10, "Vial's tap dance entry: 5 x u16");

struct RemapBlob {
  uint32_t magic;
  uint32_t layout_options;
  uint8_t conversion;                         // REMAP_CONV_*
  uint8_t reserved[3];
  RemapSettings settings;
  RemapTapDance tap_dance[REMAP_TAP_DANCES];
  uint16_t keymap[REMAP_LAYERS][REMAP_POSITIONS];
};
RemapBlob &remap_blob();
bool remap_save_due();                        // a change is 1 s old: true once, then save remap_blob()
void remap_store_load();
void remap_store_task();
