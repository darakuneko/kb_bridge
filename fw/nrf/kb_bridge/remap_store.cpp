// KB Bridge nRF: keymap storage for remap.cpp (one file in the internal LittleFS).
#include "remap.h"
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define KEYMAP_FILE "/kbb_keymap"

void remap_store_load() {
  remap_defaults();
  uint32_t magic = remap_blob().magic;    // what this firmware writes
  InternalFS.begin();
  File f(InternalFS);
  if (!f.open(KEYMAP_FILE, FILE_O_READ)) return;
  // straight into the keymap (no 3 KB copy in RAM); anything off: back to the defaults
  bool ok = f.read(&remap_blob(), sizeof(RemapBlob)) == (int)sizeof(RemapBlob) && remap_blob().magic == magic;
  f.close();
  if (!ok) remap_defaults();
}

// Vial writes a keymap in many small requests: one save after the last of them. Every write
// rewrites 4 KB flash pages and a reset in the middle can corrupt the file system, so nothing is
// written when the file already holds the same keymap (Vial often sets a key to what it was), and
// the file is rewritten in place (its size never changes) instead of removed and created again.
static bool file_matches() {
  File f(InternalFS);
  if (!f.open(KEYMAP_FILE, FILE_O_READ)) return false;
  bool same = f.size() == sizeof(RemapBlob);
  const uint8_t *p = (const uint8_t *)&remap_blob();
  uint8_t buf[64];
  for (uint32_t off = 0; same && off < sizeof(RemapBlob); off += sizeof(buf)) {
    uint32_t n = min<uint32_t>(sizeof(buf), sizeof(RemapBlob) - off);
    same = f.read(buf, n) == (int)n && !memcmp(buf, p + off, n);
  }
  f.close();
  return same;
}

void remap_store_task() {
  if (!remap_save_due() || file_matches()) return;
  File f(InternalFS);
  if (f.open(KEYMAP_FILE, FILE_O_WRITE)) {
    f.seek(0);
    f.write((const uint8_t *)&remap_blob(), sizeof(RemapBlob));
    f.close();
  }
}
