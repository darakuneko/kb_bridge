#!/usr/bin/env python3
"""KB Bridge's own Vial definition (the keymap mode used with keyboards that have no Vial/VIA).

    vial_def.py <vial_def.h> <remap_matrix.h> [<out.json>]

The keyboard is seen only through its HID usages, so the matrix is virtual. It follows the drawn
layout: one row per row of keys (main block, navigation, numpad, left to right), then the usages
that have no place in a full-size layout, 22 per row. 9 rows x 22 columns: every row of a
full-size keyboard fits, and the Vial GUI's matrix tester still works (3 bytes x 9 rows <= 28).
remap_matrix.h gives the firmware both directions (usage -> position, position -> usage).
"""
import json
import lzma
import sys

ROWS, COLS = 9, 22
USAGES = list(range(0x04, 0xA5)) + list(range(0xE0, 0xE8))   # what the matrix holds
EXTRA_Y = 6.75                                                 # first row of the extra keys
matrix = {}                                                    # usage -> (row, col), see assign()


def rc(usage):
    return "%d,%d" % matrix[usage]


# Keys: (usage, x, y, w, h, choice) - choice None = always shown, else OPT_ALL / OPT_JIS / OPT_US.
# Extra KLE properties for the JIS Enter go in a dict as a 7th element.
keys = []


# Layout option (Vial "Layout"): ALL = the JIS keyboard plus every other key the matrix holds (the
# default), JIS = a full-size JIS keyboard, US = a full-size ANSI keyboard.
OPT_ALL, OPT_JIS, OPT_US = (0, 0), (0, 1), (0, 2)
ANSI, JIS, EXTRA, EVERY = "ansi", "jis", "extra", "every"   # how k() is told where a key is drawn


def k(usage, x, y, w=1, h=1, opt=None, extra=None):
    # one drawing per layout choice it appears in (Vial shows the keys of the chosen one)
    choices = {None: [None], JIS: [OPT_ALL, OPT_JIS], ANSI: [OPT_US], EXTRA: [OPT_ALL],
               EVERY: [OPT_ALL, OPT_JIS, OPT_US]}[opt]
    for c in choices:
        keys.append((usage, x, y, w, h, c, extra or {}))


# function row
k(0x29, 0, 0)
for i, u in enumerate(range(0x3A, 0x3E)): k(u, 2 + i, 0)          # F1-F4
for i, u in enumerate(range(0x3E, 0x42)): k(u, 6.5 + i, 0)        # F5-F8
for i, u in enumerate(range(0x42, 0x46)): k(u, 11 + i, 0)         # F9-F12
for i, u in enumerate((0x46, 0x47, 0x48)): k(u, 15.25 + i, 0)    # PrtSc ScrLk Pause

# number row: ` 1..0 - =
for i, u in enumerate([0x35] + list(range(0x1E, 0x28)) + [0x2D, 0x2E]): k(u, i, 1.25)
k(0x2A, 13, 1.25, w=2, opt=ANSI)
k(0x89, 13, 1.25, opt=JIS)                                         # Yen
k(0x2A, 14, 1.25, opt=JIS)
for i, u in enumerate((0x49, 0x4A, 0x4B)): k(u, 15.25 + i, 1.25)  # Ins Home PgUp
for i, u in enumerate((0x53, 0x54, 0x55, 0x56)): k(u, 18.5 + i, 1.25)

# Q row
k(0x2B, 0, 2.25, w=1.5)
for i, u in enumerate((0x14, 0x1A, 0x08, 0x15, 0x17, 0x1C, 0x18, 0x0C, 0x12, 0x13, 0x2F, 0x30)):
    k(u, 1.5 + i, 2.25)
k(0x31, 13.5, 2.25, w=1.5, opt=ANSI)
k(0x28, 13.75, 2.25, w=1.25, h=2, opt=JIS, extra={"w2": 1.5, "h2": 1, "x2": -0.25})
for i, u in enumerate((0x4C, 0x4D, 0x4E)): k(u, 15.25 + i, 2.25)  # Del End PgDn
for i, u in enumerate((0x5F, 0x60, 0x61)): k(u, 18.5 + i, 2.25)
k(0x57, 21.5, 2.25, h=2)

# A row
k(0x39, 0, 3.25, w=1.75)
for i, u in enumerate((0x04, 0x16, 0x07, 0x09, 0x0A, 0x0B, 0x0D, 0x0E, 0x0F, 0x33, 0x34)):
    k(u, 1.75 + i, 3.25)
k(0x28, 12.75, 3.25, w=2.25, opt=ANSI)
k(0x32, 12.75, 3.25, opt=JIS)                                      # JIS ]
for i, u in enumerate((0x5C, 0x5D, 0x5E)): k(u, 18.5 + i, 3.25)

# Z row
k(0xE1, 0, 4.25, w=2.25)
for i, u in enumerate((0x1D, 0x1B, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38)):
    k(u, 2.25 + i, 4.25)
k(0xE5, 12.25, 4.25, w=2.75, opt=ANSI)
k(0x87, 12.25, 4.25, opt=JIS)                                      # Ro
k(0xE5, 13.25, 4.25, w=1.75, opt=JIS)
k(0x52, 16.25, 4.25)
for i, u in enumerate((0x59, 0x5A, 0x5B)): k(u, 18.5 + i, 4.25)
k(0x58, 21.5, 4.25, h=2)

# bottom row
# Vial / Pipette move the chosen layout's keys so that their top left meets that of choice 0
# (ALL, whose extra rows start at x = 0): Left Ctrl at x = 0 in every choice keeps them in place.
k(0xE0, 0, 5.25, w=1.25, opt=EVERY)
x = 1.25
for u in (0xE3, 0xE2):
    k(u, x, 5.25, w=1.25); x += 1.25
k(0x2C, 3.75, 5.25, w=6.25, opt=ANSI)
x = 10
for u in (0xE6, 0xE7, 0x65, 0xE4):
    k(u, x, 5.25, w=1.25, opt=ANSI); x += 1.25
k(0x8B, 3.75, 5.25, w=1.25, opt=JIS)                               # Muhenkan
k(0x2C, 5, 5.25, w=2.5, opt=JIS)
x = 7.5
for u in (0x8A, 0x88, 0xE6, 0xE7, 0x65, 0xE4):                     # Henkan Kana RAlt RGui App RCtrl
    k(u, x, 5.25, w=1.25, opt=JIS); x += 1.25
for i, u in enumerate((0x50, 0x51, 0x4F)): k(u, 15.25 + i, 5.25)  # Left Down Right
k(0x62, 18.5, 5.25, w=2)
k(0x63, 20.5, 5.25)

# everything else the matrix holds, in usage order: F13-F24 first, then the rest
placed = {u for (u, x, y, w, h, opt, e) in keys if opt in (None, OPT_ALL)}
rest = [u for u in range(0x68, 0x74)] + [u for u in range(0x04, 0xA5) if not 0x68 <= u < 0x74]
rest = [u for u in rest if u not in placed]
for i, u in enumerate(rest):
    k(u, (i % COLS) * 1.0, EXTRA_Y + (i // COLS), opt=EXTRA)


def assign():
    """Matrix row = row of keys (by y) in the ALL layout, column = order from the left. Within a
    row, keys sort by their rightmost drawing in that row, so a JIS key comes before the key it
    pushes right (Yen before Backspace, Ro before Right Shift)."""
    draws = {}
    for (u, x, y, w, h, opt, extra) in keys:
        draws.setdefault(u, []).append((x, y, opt))
    rows_y = sorted({y for (_u, _x, y, _w, _h, opt, _e) in keys if opt in (None, OPT_ALL)})
    by_row = {}
    for u, ds in draws.items():
        y = [d for d in ds if d[2] in (None, OPT_ALL)][0][1]
        x = max(d[0] for d in ds if d[1] == y and d[2] in (None, OPT_ALL))
        by_row.setdefault(rows_y.index(y), []).append((x, u))
    assert len(by_row) == ROWS, len(by_row)
    for r, row in by_row.items():
        assert len(row) <= COLS, (r, len(row))
        for c, (_x, u) in enumerate(sorted(row)):
            matrix[u] = (r, c)
    assert sorted(matrix) == USAGES


assign()

# every choice must have the same top left (see Left Ctrl above), or its keys are drawn shifted
tops = {}
for (u, x, y, w, h, opt, e) in keys:
    if opt is not None:
        t = tops.setdefault(opt, [x, y])
        t[0], t[1] = min(t[0], x), min(t[1], y)
assert len({tuple(t) for t in tops.values()}) == 1, tops


def kle():
    rows, cur_y = [], -1.0
    by_y = {}
    for key in keys:
        by_y.setdefault(key[2], []).append(key)
    for y in sorted(by_y):
        row, cx = [], 0.0
        first = True
        for (u, x, _y, w, h, opt, extra) in sorted(by_y[y], key=lambda t: (t[1], t[5] or (-1, -1))):
            props = {}
            if first and y != cur_y + 1:
                props["y"] = round(y - cur_y - 1, 4)
            if x != cx:
                props["x"] = round(x - cx, 4)
            if w != 1: props["w"] = w
            if h != 1: props["h"] = h
            props.update(extra)
            if props: row.append(props)
            label = rc(u) if opt is None else "%s\n\n\n%d,%d" % (rc(u), opt[0], opt[1])
            row.append(label)
            cx = x + w
            first = False
        rows.append(row)
        cur_y = y
    return rows


definition = {
    "name": "KB Bridge",
    "vendorId": "0x7A79",
    "productId": "0x1576",
    "matrix": {"rows": ROWS, "cols": COLS},
    "layouts": {"labels": [["Layout", "ALL", "JIS", "US"]], "keymap": kle()},
    # Vial's USER tab: QK_KB_0 (0x7E00) + index, in this order. Must match remap.h (KBB_*).
    "customKeycodes": [
        {"name": "KBB_HOST1", "title": "Connect to host 1 (BLE)", "shortName": "Host 1"},
        {"name": "KBB_HOST2", "title": "Connect to host 2 (BLE)", "shortName": "Host 2"},
        {"name": "KBB_HOST3", "title": "Connect to host 3 (BLE)", "shortName": "Host 3"},
        {"name": "KBB_HOST4", "title": "Connect to host 4 (BLE)", "shortName": "Host 4"},
        {"name": "KBB_KEEPALIVE", "title": "Power bank keep-alive: weak -> strong -> off (like LOW-I)", "shortName": "Keep\nalive"},
        {"name": "KBB_JIS_TO_US", "title": "JIS keyboard, OS set to US", "shortName": "JIS\n\u2192US"},
        {"name": "KBB_US_TO_JIS", "title": "US keyboard, OS set to JIS", "shortName": "US\n\u2192JIS"},
        {"name": "KBB_NO_CONV", "title": "No JIS / US conversion", "shortName": "No\nconv"},
    ],
}

out_h, out_matrix = sys.argv[1], sys.argv[2]
data = json.dumps(definition, separators=(",", ":"))
blob = lzma.compress(data.encode())
lines = ["// Generated by fw/tools/vial_def.py - do not edit.",
         "#pragma once",
         "#include <stdint.h>",
         "static const uint8_t VIAL_DEFINITION[] = {"]
for i in range(0, len(blob), 16):
    lines.append("  " + ", ".join("0x%02X" % b for b in blob[i:i + 16]) + ",")
lines.append("};")
open(out_h, "w").write("\n".join(lines) + "\n")
pos_of = [0xFF] * 256
usage_at = [0] * (ROWS * COLS)
for u, (r, c) in matrix.items():
    pos_of[u] = r * COLS + c
    usage_at[r * COLS + c] = u
m = ["// Generated by fw/tools/vial_def.py - do not edit.",
     "// KB Bridge's virtual matrix: one row per row of keys of the Vial layout, columns left to right.",
     "#pragma once",
     "#include <stdint.h>",
     "#define REMAP_ROWS %d" % ROWS,
     "#define REMAP_COLS %d" % COLS,
     "// HID usage -> position (row * REMAP_COLS + col), 0xFF: not in the matrix",
     "static const uint8_t REMAP_POS_OF[256] = {"]
for i in range(0, 256, 16):
    m.append("  " + ", ".join("0x%02X" % v for v in pos_of[i:i + 16]) + ",")
m += ["};", "// position -> HID usage, 0: no key there", "static const uint8_t REMAP_USAGE_AT[%d] = {" % (ROWS * COLS)]
for r in range(ROWS):
    m.append("  " + ", ".join("0x%02X" % v for v in usage_at[r * COLS:(r + 1) * COLS]) + ",   // row %d" % r)
m.append("};")
open(out_matrix, "w").write("\n".join(m) + "\n")
if len(sys.argv) > 3:
    json.dump(definition, open(sys.argv[3], "w"), indent=1)
print("%s: %d keys, json %d bytes, compressed %d bytes" % (out_h, len(keys), len(data), len(blob)))
