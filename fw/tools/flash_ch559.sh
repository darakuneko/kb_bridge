#!/bin/bash
# Seller's first CH559 flash over USB (J1), with wchisp (https://github.com/ch32-rs/wchisp).
#   1. short JP1 (BOOT: CH559 P4.6 to GND)  2. plug J1 into the Mac  3. fw/tools/flash_ch559.sh
#   4. unplug, open JP1
# The CH559 starts in WCH's USB bootloader while P4.6 is low at power-on.
#   fw/tools/flash_ch559.sh [image]      default: fw/build/kb_bridge_ch559.bin (built if missing)
set -euo pipefail
cd "$(dirname "$0")/.."
WCHISP=tools/bin/wchisp
IMG=${1:-build/kb_bridge_ch559.bin}

[ -x "$WCHISP" ] || { echo "missing $WCHISP (see fw/README.md: wchisp)"; exit 1; }
[ -f "$IMG" ] || ./build.sh

# -r: keep scanning for the bootloader for a few seconds after plugging in
"$WCHISP" -u -r 10 info --chip CH559
"$WCHISP" -u -r 5 flash "$IMG"
echo "done: $IMG -> CH559. Unplug J1 and open JP1."
