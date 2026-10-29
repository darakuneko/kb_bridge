#!/bin/bash
# KB Bridge firmware build: CH559 (SDCC) -> embedded into the nRF sketch -> nRF (arduino-cli)
# -> DFU package for adafruit-nrfutil / serial DFU through the CH559.
#   fw/build.sh            outputs in fw/build/
set -euo pipefail
cd "$(dirname "$0")"
FQBN=adafruit:nrf52:feather52832
OUT=build
NRFUTIL=${NRFUTIL:-.venv/bin/adafruit-nrfutil}
BOARD_REV=${BOARD_REV:-6}           # 6 = v0.6 board, 7 = v0.7 (CH559 power switch, RESET via FET, CC Rp on P0.28)
mkdir -p "$OUT"

# 1. CH559
make -C ch559 -s -B BOARD_REV=$BOARD_REV
BUILD_ID=$(cat ch559/build/build_id.txt)
cp ch559/build/kb_bridge_ch559.bin ch559/build/kb_bridge_ch559.hex "$OUT"/
python3 tools/bin2h.py ch559/build/kb_bridge_ch559.bin "$BUILD_ID" nrf/kb_bridge/ch559_image.h

# 2. nRF52832 (bigger UART RX buffer: loop() may sleep 10 ms while the 250 kbaud link brings ~250 bytes; NFC pins as GPIO for the LED)
FLAGS="-DSERIAL_BUFFER_SIZE=1024 -DCONFIG_NFCT_PINS_AS_GPIOS -DBOARD_REV=$BOARD_REV ${EXTRA_FLAGS:-}"
arduino-cli compile --fqbn "$FQBN" \
  --build-property "compiler.c.extra_flags=$FLAGS" \
  --build-property "compiler.cpp.extra_flags=$FLAGS" \
  --output-dir "$OUT/nrf" nrf/kb_bridge

# 3. DFU package (application only; SoftDevice S132 6.1.1 is already on the HY0020)
"$NRFUTIL" dfu genpkg --dev-type 0x0052 --sd-req 0x00B7 \
  --application "$OUT/nrf/kb_bridge.ino.hex" "$OUT/kb_bridge_dfu.zip"
echo "done: $OUT/kb_bridge_dfu.zip (board v0.$BOARD_REV, nRF + embedded CH559 $BUILD_ID)"
