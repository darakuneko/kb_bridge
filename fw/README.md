# KB Bridge firmware

Turns a wired QMK/Vial keyboard (J2) into a BLE keyboard. Firmware for two microcontrollers:

| | Role | Toolchain | Path |
|---|---|---|---|
| CH559L | USB host for the keyboard on J2. In update mode: USB serial on J1 | SDCC | `ch559/` |
| nRF52832 (HY0020) | BLE HID + Vial, power management, CH559 updates | Arduino + Adafruit nRF52 (Feather nRF52832) | `nrf/kb_bridge/` |

The CH559 firmware is embedded in the nRF firmware. **Updating the nRF once also updates the CH559 on the next start** (when the CH559's build id differs).

## Build

```
fw/build.sh                 # KB Bridge v0.7
EXTRA_FLAGS=-DSYSTEM_OFF_IDLE_MS=30000 fw/build.sh   # extra defines, e.g. a 30 s System OFF for testing
```

- Output: `fw/build/kb_bridge_dfu.zip` (nRF DFU package), `fw/build/kb_bridge_ch559.{bin,hex}`
- Steps: make the CH559 → `tools/bin2h.py` generates `nrf/kb_bridge/ch559_image.h` → arduino-cli → DFU zip
- Needs: `sdcc`, `arduino-cli` + `adafruit:nrf52`, `adafruit-nrfutil` in `fw/.venv` (`python3 -m venv fw/.venv && fw/.venv/bin/pip install adafruit-nrfutil`)
- `fw/tools/bin/wchisp` is not in the repository. Get it from https://github.com/ch32-rs/wchisp
- Logic tests (on the PC):
  ```
  c++ -std=c++17 -I fw/test -I fw/nrf/kb_bridge fw/test/test_bridge.cpp \
      fw/nrf/kb_bridge/hid_parser.cpp fw/nrf/kb_bridge/bridge.cpp -o /tmp/test_bridge && /tmp/test_bridge
  ```

## Flashing

The browser pages (`docs/`, GitHub Pages) do the same as the steps below: `docs/ch559.html` for the CH559 first flash, `docs/index.html` for the nRF (and with it every later update).

**CH559 first flash (once, at the start of the build; also works with the HY0020 fitted)**
1. Short JP1 (BOOT) (P4.6 = GND) → plug J1 into the computer → the CH559 starts in WCH's USB bootloader
2. `fw/tools/flash_ch559.sh` (writes `build/kb_bridge_ch559.bin` with wchisp: chip check → erase → write → verify → reset)
3. Unplug J1 and remove the JP1 short

wchisp usage: `fw/tools/bin/wchisp -u info` / `-u flash <file>` / `-u verify <file>` / `-u config info`. The macOS arm64 nightly (v0.3.0) needs no libusb. `info` shows the CH559 bootloader version (BTVER).

**nRF**: solder the DIP → **plug J1 into the PC with PAIR held** → release → either
- `adafruit-nrfutil --verbose dfu serial -pkg kb_bridge_dfu.zip -p <port> -b 115200 --singlebank`
- or upload the sketch from the Arduino IDE (board: Adafruit Feather nRF52832)

How it works: powered up with PAIR held, the CH559 becomes a USB serial port (CDC-ACM, 7A79:1576) bridging J1 to the nRF UART. P0.20 (= PAIR) is low, so the nRF stays in the Adafruit bootloader. When DTR is asserted (not when it drops) the CH559 pulses the nRF's RESET low (like the Feather's DTR → capacitor → RESET). Once the nRF application runs, it waits for PAIR to be released and resets the CH559. If the CH559 does not answer with HELLO or its build id differs, the nRF rewrites it through the CH559's serial bootloader.

## nRF ↔ CH559 link

Defined in `nrf/kb_bridge/link_proto.h` (both firmwares use the same file). UART 250 kbps (no error at the CH559's 12 MHz), `0x7E, TYPE, LEN, DATA, CRC8`. Protocol version 3.
The CH559 only relays. When a device attaches it sends the HID interfaces and report descriptors, then forwards IN reports as they are. The nRF parses the descriptors and turns the reports into BLE reports.

## Design

| | |
|---|---|
| CH559 clock | 12 MHz normally (USB has its own 48 MHz), 48 MHz only in update mode |
| BLE connection parameters | 11.25–15 ms interval, slave latency 30, 4 s supervision timeout (within Apple's HID rules). Same input latency, lower idle current |
| No lost reports | BLE reports go through an ordered queue (32 entries) and are resent on the next loop if sending fails. On overflow the oldest are dropped (the latest state always stays). Input while disconnected is not replayed after reconnecting; the current state is sent once on connect |
| No stuck keys | All keys are released when the keyboard is unplugged, the CH559 restarts (HELLO) or the device re-enumerates |
| Watchdogs | nRF 4 s (from the end of setup), CH559 5.6 s at 12 MHz (stopped while powered down) |
| Heartbeat | The nRF sends PING every second → the CH559 answers with STATUS (state, suspended, consecutive errors, enumeration failures). No answer for 3.5 s: the CH559 is reset |
| USB recovery | Re-enumerate after 200 consecutive IN errors (20 right after a resume). Enumeration failing for 2 s: VBUS_KB off for 0.5 s and back on (like a re-plug, up to 3 times per plug-in) |
| USB suspend | After 2 minutes without input the keyboard is suspended. Remote wakeup is enabled; a key (bus K state) or host output (LEDs, Vial) resumes it, and it is re-enumerated if it does not answer. Without `rgb_matrix.sleep` a QMK keyboard keeps its RGB on in suspend |
| CH559 sleep | With no keyboard or the keyboard in USB suspend, the nRF's `MSG_SLEEP` powers the CH559 down. A USB event (remote wakeup) or RXD going low (the nRF sends one 0x00 byte and waits `LINK_WAKE_MS`) wakes it. Woken for no reason, it watches for 10 ms and sleeps again. While the bus is suspended it resumes only when it sees K within 40 ms (QMK repeats K about every 17 ms while a key is held) |
| CH559 power switch | With no keyboard the CH559 is switched off altogether (P0.02). It comes back with the keyboard's VBUS (CC detect) |
| CH559 auto-update | The running CH559 application is sent `MSG_ENTER_BOOTLOADER` (XI low + an RST pulse does not enter the bootloader; P4.6 is read only at power-on), then the nRF writes it over the serial bootloader (V2.40, P0.2/P0.3), ending with an empty chunk that commits the write. It rewrites only when the CH559 answers with another build, or when it does not answer (no application yet, or an update cut short: it is then power-cycled with XI low), and not when the supply is below 4.4 V. On failure the green blink count gives the reason, then fast red: 1 no answer, 2 wrong chip, 3 config read, 4 key, 5 erase, 6 write, 7 verify, 9 written but does not start |
| nRF idle | After 1 s with nothing to do, loop() runs every 10 ms instead of 1 ms (the first key after that waits up to 10 ms more). While the CH559 sleeps the UART is stopped too and loop() waits 100 ms. loop() waits on a task notification instead of delay(), so BLE writes and an RX falling edge wake it at once |
| System OFF | After 10 minutes without a BLE connection (`SYSTEM_OFF_IDLE_MS`) the CH559, keyboard power, keep-alive and LED are switched off and the nRF enters System OFF. LOW-I or plugging / unplugging the keyboard (CC) wakes it as a reset. Not PAIR: P0.20 low at reset keeps the bootloader in DFU mode |
| nRF power | Internal DC/DC enabled (`NRF_DCDC_ENABLE`). The HY0020 has the inductor inside; DCC tied to DEC4 selects DC/DC mode, which R1 (0 Ω) on the AE-HY0020-DIP already does |
| CH559 start-up | `main()` clears the P3.2 latch before making the pin push-pull: the latch resets to 1, and a short high pulse would reset the nRF through Q4 |

## Behaviour

| Feature | |
|---|---|
| BLE HID | Keyboard (6KRO, boot protocol), consumer, system, mouse. NKRO and multiple interfaces on the USB side are merged and sent as 6KRO |
| Vial | A second HID service (usage page 0xFF60, 32 bytes) relayed as is to the keyboard's raw HID. DIS serial number = `vial:f64c2b3c:XXXXXX`. ATT MTU 64 (with the default 23, 32-byte reports are split into 20+12 and the host drops them). The zero-length packet a keyboard sends after a reply is dropped. DIS PnP ID = KB Bridge's own 7A79:1576 (the attached keyboard's id is not passed on). While Vial talks (10 s after the last request) slave latency is off (otherwise requests arrive bunched, Unlock polls come less than 100 ms apart and the count resets) |
| Keyboard LEDs | Host LED output → the keyboard's LED output reports (all of them, including ones with a report id) |
| Keyboard power | VBUS_KB is switched on only when CC shows an Rd (0.44 V). A PC or charger plugged into J2 gets no 5 V |
| Keep-alive | LOW-I cycles weak → strong → off (saved, default weak). Strong: 200 ms every 5 s at ~150 mA (~6 mA average). Weak: 50 ms every 10 s (~0.75 mA average, for power banks in their own low-current mode). Makers differ: tune `KEEPALIVE_WEAK_*` |
| PAIR button | Short press: start advertising / hold 3 s: forget all bonds |
| LED (bi-colour) | Advertising: green blink / connected: green 2 s / connected, no keyboard: slow red blink / CH559 update: red-green alternating / CH559 fault: fast red / supply below 4.5 V: one red blink every 5 s / LOW-I: green ×2 = weak, green ×3 = strong, red ×2 = off |

## Not implemented / later

- Switching between several hosts, NKRO BLE reports, battery service

## About licenses

No code from CH559sdccUSBHost (GPLv3) or wchisp (GPLv2) is used; only register addresses and the ISP protocol were consulted.
