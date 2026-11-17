// KB Bridge nRF: file system reset, written by the firmware updater (docs/) before the real firmware
// when "initialise pairings and settings" is ticked.
//
// The internal file system (InternalFS: bonds of every host, settings, keymap) can be left corrupt
// by a reset or power loss in the middle of a write: every later write fails, so pairings are lost
// at each power cycle, whatever firmware runs. This formats it, checks that a file can be written and
// read back, and then repeats the result on the UART (115200; the CH559's update-mode serial on J1)
// until the updater writes the real firmware:
//   "KBB FS RESET OK"  or  "KBB FS RESET FAIL <step>"
#include <Arduino.h>
#include <InternalFileSystem.h>

using namespace Adafruit_LittleFS_Namespace;

static const char *result = "KBB FS RESET FAIL format";

static void reset_fs() {
  if (!InternalFS.format()) return;
  result = "KBB FS RESET FAIL mount";
  if (!InternalFS.begin()) return;
  result = "KBB FS RESET FAIL write";
  const uint32_t probe = 0x4B424252;      // "KBBR"
  File f(InternalFS);
  if (!f.open("/kbb_probe", FILE_O_WRITE)) return;
  bool ok = f.write((const uint8_t *)&probe, 4) == 4;
  f.close();
  if (!ok) return;
  result = "KBB FS RESET FAIL read";
  uint32_t back = 0;
  if (!f.open("/kbb_probe", FILE_O_READ)) return;
  f.read(&back, 4);
  f.close();
  if (back != probe) return;
  InternalFS.remove("/kbb_probe");
  result = "KBB FS RESET OK";
}

void setup() {
  Serial.begin(115200);
  reset_fs();
}

void loop() {
  Serial.println(result);
  delay(250);
}
