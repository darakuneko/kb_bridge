// KB Bridge nRF: storage for remap.cpp in the internal LittleFS: the keymap (with layout options,
// conversion, settings, tap dance) and the macro buffer, one file each.
#include "remap.h"
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

#define KEYMAP_FILE "/kbb_keymap"
#define MACRO_FILE  "/kbb_macros"

void remap_store_load() {
  remap_defaults();
  remap_macros_clear();
  InternalFS.begin();
  File f(InternalFS);
  if (f.open(KEYMAP_FILE, FILE_O_READ)) {
    // straight into the keymap (no 4 KB copy in RAM); anything off: back to the defaults
    bool ok = f.read(&remap_blob(), sizeof(RemapBlob)) == (int)sizeof(RemapBlob) && remap_blob_valid();
    f.close();
    if (!ok) remap_defaults();
  }
  if (f.open(MACRO_FILE, FILE_O_READ)) {
    if (f.read(remap_macro_buffer(), REMAP_MACRO_BUFFER) != REMAP_MACRO_BUFFER) remap_macros_clear();
    f.close();
  }
}

// Vial writes in many small requests: one save after the last of them. Every write rewrites 4 KB
// flash pages and a reset in the middle can corrupt the file system, so nothing is written when
// the file already holds the same data (Vial often sets a key to what it was), and a file is
// rewritten in place (its size never changes) instead of removed and created again.
static bool file_matches(const char *name, const uint8_t *p, uint32_t size) {
  File f(InternalFS);
  if (!f.open(name, FILE_O_READ)) return false;
  bool same = f.size() == size;
  uint8_t buf[64];
  for (uint32_t off = 0; same && off < size; off += sizeof(buf)) {
    uint32_t n = min<uint32_t>(sizeof(buf), size - off);
    same = f.read(buf, n) == (int)n && !memcmp(buf, p + off, n);
  }
  f.close();
  return same;
}

static void save(const char *name, const uint8_t *p, uint32_t size) {
  if (file_matches(name, p, size)) return;
  File f(InternalFS);
  if (f.open(name, FILE_O_WRITE)) {
    f.seek(0);
    f.write(p, size);
    f.close();
  }
}

void remap_store_task() {
  if (!remap_save_due()) return;
  save(KEYMAP_FILE, (const uint8_t *)&remap_blob(), sizeof(RemapBlob));
  save(MACRO_FILE, remap_macro_buffer(), REMAP_MACRO_BUFFER);
}
