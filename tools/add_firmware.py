#!/usr/bin/env python3
"""Add a KB Bridge DFU package to the site (firmware/<board>/ + firmware/index.json, newest first).

  tools/add_firmware.py <kb_bridge_dfu.zip> --board v0.7 [--ch559 <kb_bridge_ch559.bin>] [--version 0.3.1]
                        [--notes "..."] [--notes-en "..."]

--ch559 defaults to kb_bridge_ch559.bin next to the zip (fw/build.sh writes both): the image for the CH559's
first flash before the HY0020 is fitted (ch559.html). Its build id comes from fw/ch559/build/build_id.txt.

--version defaults to FW_VERSION_STR in fw/nrf/kb_bridge/config.h. Files go to docs/firmware/.
Build the package first with fw/build.sh.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HERE = os.path.join(ROOT, "docs")                  # the GitHub Pages site
INDEX = os.path.join(HERE, "firmware", "index.json")
FW = os.path.join(ROOT, "fw")
CONFIG = os.path.join(FW, "nrf", "kb_bridge", "config.h")
BUILD_ID = os.path.join(FW, "ch559", "build", "build_id.txt")
BOARD_NAMES = {"v0.7": "KB Bridge v0.7"}


def fw_version():
    m = re.search(r'#define\s+FW_VERSION_STR\s+"([^"]+)"', open(CONFIG).read())
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip")
    ap.add_argument("--board", required=True, choices=sorted(BOARD_NAMES))
    ap.add_argument("--ch559")
    ap.add_argument("--version")
    ap.add_argument("--notes", default="")
    ap.add_argument("--notes-en", default="")
    a = ap.parse_args()

    z = zipfile.ZipFile(a.zip)
    app = json.loads(z.read("manifest.json"))["manifest"]["application"]
    z.getinfo(app["bin_file"]), z.getinfo(app["dat_file"])          # must be an application package

    version = a.version or fw_version()
    today = datetime.date.today()
    rel = "firmware/%s/kb_bridge_%s_%s_%s.zip" % (a.board, a.board, version, today.strftime("%Y%m%d"))
    os.makedirs(os.path.dirname(os.path.join(HERE, rel)), exist_ok=True)
    shutil.copyfile(a.zip, os.path.join(HERE, rel))

    ch559 = a.ch559 or os.path.join(os.path.dirname(os.path.abspath(a.zip)), "kb_bridge_ch559.bin")
    ch559_rel = None
    if os.path.exists(ch559):
        build_id = open(BUILD_ID).read().strip().rstrip("UL") if os.path.exists(BUILD_ID) else ""
        ch559_rel = rel[:-4].replace("kb_bridge_", "kb_bridge_ch559_", 1) + ".bin"
        shutil.copyfile(ch559, os.path.join(HERE, ch559_rel))

    index = json.load(open(INDEX)) if os.path.exists(INDEX) else {"boards": []}
    board = next((b for b in index["boards"] if b["id"] == a.board), None)
    if board is None:
        board = {"id": a.board, "name": BOARD_NAMES[a.board], "firmware": []}
        index["boards"].append(board)
        index["boards"].sort(key=lambda b: b["id"], reverse=True)    # newest board first
    board["firmware"] = [f for f in board["firmware"] if f["file"] != rel]
    entry = {"version": version, "date": today.isoformat(), "file": rel}
    if ch559_rel:
        entry["ch559"] = {"file": ch559_rel, "build_id": build_id}
    if a.notes or a.notes_en:
        entry["notes"] = {"ja": a.notes or a.notes_en, "en": a.notes_en or a.notes}
    board["firmware"].insert(0, entry)
    with open(INDEX, "w") as f:
        json.dump(index, f, ensure_ascii=False, indent=2)
        f.write("\n")
    print("added", rel)


if __name__ == "__main__":
    main()
