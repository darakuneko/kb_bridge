# KB Bridge

Firmware, browser-based flashing pages and the build guide for KB Bridge, an adapter that turns a wired QMK / Vial keyboard into a Bluetooth LE keyboard.

- Firmware updater: https://darakuneko.github.io/kb_bridge/
- CH559 first flash: https://darakuneko.github.io/kb_bridge/ch559.html
- Build guide: https://darakuneko.github.io/kb_bridge/guide_en.html (Japanese: https://darakuneko.github.io/kb_bridge/guide.html)

| Path | Contents |
|---|---|
| `fw/` | Firmware (CH559L USB host + nRF52832 BLE). Details in [fw/README.md](fw/README.md) |
| `docs/` | GitHub Pages site: updater (`index.html`), build guide (`guide_en.html`, Japanese `guide.html`), CH559 first flash (`ch559.html`), published firmware (`firmware/`) |
| `docs/hardware/` | For people who design their own case: PCBA STL (HY0020 fitted, mm) and a dimension drawing (PDF) |
| `tools/gen_dimensions.py` | Generates the dimension PDF and the button map (`docs/img/buttons.svg`); needs matplotlib. The STL comes from the KiCad board: `kicad-cli pcb export step --no-dnp --subst-models --user-origin 128x113mm` (J4 set to DNP), converted to STL with CadQuery (`tolerance=0.05`). The STEP is not published |
| `tools/add_firmware.py` | Adds a build to `docs/firmware/` and updates the list |
| `tools/test/` | Checks that the updater's DFU sends the same data as adafruit-nrfutil |

## Flashing pages

- Normal update (`index.html`): runs the same procedure as `adafruit-nrfutil dfu serial --singlebank` over Web Serial. Plugging the board in with PAIR held makes the CH559 a USB serial port, "KB Bridge Update" (7A79:1576), and the page writes the nRF through it. The nRF firmware carries the CH559 firmware and updates it on the next start, so this is the only step for users.
- CH559 first flash (`ch559.html`): with JP1 (BOOT) shorted, the CH559 starts in its USB ISP mode (4348:55E0) and the page writes it over WebUSB (same as `fw/tools/flash_ch559.sh`). Done once at the start of the build; it also works with the HY0020 fitted. On Windows, install the WinUSB driver with Zadig first.
- Both run in Chrome / Edge on a computer (Windows, macOS, Linux). Not in Safari, Firefox or on phones.
- Static pages, no build step and no external libraries.

## Local testing

If writing from GitHub Pages keeps failing, clone this repository and serve the pages from your own computer. Web Serial / WebUSB only work over https or on localhost.

```sh
git clone https://github.com/darakuneko/kb_bridge.git
cd kb_bridge/docs && python3 -m http.server 8000     # open http://localhost:8000 in Chrome
```

Or write the nRF without a browser: `adafruit-nrfutil --verbose dfu serial -pkg <firmware zip> -p <port> -b 115200 --singlebank` (see [fw/README.md](fw/README.md)).

## Tests

```sh
fw/.venv/bin/python tools/test/ref_packets.py fw/build/kb_bridge_dfu.zip /tmp/ref.bin
node tools/test/compare.mjs fw/build/kb_bridge_dfu.zip /tmp/ref.bin     # MATCH = same as nrfutil
```

## Modifying and pull requests

If something bothers you or you want KB Bridge to do something else, change it as you like. If you build a good feature, pull requests are welcome.

## License

MIT (`LICENSE`). Licenses of the third-party software in the published firmware binaries: `THIRD_PARTY_NOTICES.md` (full LGPL-2.1 text in `licenses/`). The PCB design files (KiCad) are not published.
