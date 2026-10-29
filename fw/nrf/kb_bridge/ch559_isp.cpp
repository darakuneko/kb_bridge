// KB Bridge nRF: CH559 serial ISP.
//
// Frames (as used by WCH's tools for the CH55x bootloader, V2.x):
//   request : 0x57 0xAB, cmd, len_lo, len_hi, payload[len], sum8(cmd .. payload)
//   response: 0x55 0xAA, cmd, status, len_lo, len_hi, payload[len], sum8(cmd .. payload)
// Sequence: IDENTIFY -> READ_CONFIG (bootloader version, 4-byte UID) -> ISP_KEY (all-zero seed)
//   -> ERASE -> PROGRAM (56-byte chunks XORed with the key, then an empty one) -> VERIFY -> ISP_END.
// XOR key for an all-zero seed: 8 x sum8(UID), last byte + chip id.
#include "ch559_isp.h"
#include "config.h"
#include "link_proto.h"
#include <InternalFileSystem.h>
using namespace Adafruit_LittleFS_Namespace;

namespace {

constexpr uint8_t CMD_IDENTIFY = 0xA1, CMD_ISP_END = 0xA2, CMD_ISP_KEY = 0xA3, CMD_ERASE = 0xA4,
                  CMD_PROGRAM = 0xA5, CMD_VERIFY = 0xA6, CMD_READ_CONFIG = 0xA7;
constexpr uint8_t CHIP_ID = 0x59, CHIP_TYPE = 0x11;         // CH559, CH55x series
constexpr uint32_t ISP_BAUD = 115200;
constexpr uint8_t CHUNK = 56;

uint8_t resp[64];
uint16_t resp_len;

void send_cmd(uint8_t cmd, const uint8_t *payload, uint16_t len) {
  uint8_t sum = cmd + (uint8_t)len + (uint8_t)(len >> 8);
  Serial.write(0x57);
  Serial.write(0xAB);
  Serial.write(cmd);
  Serial.write((uint8_t)len);
  Serial.write((uint8_t)(len >> 8));
  for (uint16_t i = 0; i < len; i++) sum += payload[i];
  if (len) Serial.write(payload, len);
  Serial.write(sum);
  Serial.flush();
}

inline void wdt_feed() { NRF_WDT->RR[0] = WDT_RR_RR_Reload; }    // harmless when it is not running

bool read_byte(uint8_t &b, uint32_t deadline) {
  while (!Serial.available()) {
    wdt_feed();
    if ((int32_t)(millis() - deadline) > 0) return false;
  }
  b = Serial.read();
  return true;
}

// Waits for a response to `cmd`. Leaves the payload in resp/resp_len.
bool recv(uint8_t cmd, uint32_t timeout_ms) {
  uint32_t deadline = millis() + timeout_ms;
  uint8_t b, prev = 0;
  for (;;) {                                                   // hunt for 0x55 0xAA
    if (!read_byte(b, deadline)) return false;
    if (prev == 0x55 && b == 0xAA) break;
    prev = b;
  }
  uint8_t hdr[4];
  for (auto &h : hdr)
    if (!read_byte(h, deadline)) return false;
  uint16_t len = hdr[2] | (hdr[3] << 8);
  uint8_t sum = hdr[0] + hdr[1] + hdr[2] + hdr[3];
  resp_len = 0;
  for (uint16_t i = 0; i < len; i++) {
    if (!read_byte(b, deadline)) return false;
    sum += b;
    if (resp_len < sizeof(resp)) resp[resp_len++] = b;
  }
  if (!read_byte(b, deadline) || b != sum) return false;
  return hdr[0] == cmd;
}

bool transfer(uint8_t cmd, const uint8_t *payload, uint16_t len, uint32_t timeout_ms = 1000) {
  while (Serial.available()) Serial.read();
  send_cmd(cmd, payload, len);
  return recv(cmd, timeout_ms);
}

// Uart::end() on a UART that is already off waits forever for its STOPPED events (the ISP hung
// at the first baud-rate try): only stop a running one.
void serial_end() {
  if (NRF_UARTE0->ENABLE != UARTE_ENABLE_ENABLE_Disabled) Serial.end();
}

// A running CH559 application jumps to the bootloader on ENTER_BOOTLOADER.
// Holding XI low through a RST pulse does NOT enter it on this board (the pin seems
// to be sampled at power-on only); it stays as the try for a CH559 without a working application.
void enter_bootloader(bool app_running) {
  if (app_running) {
    serial_end();
    Serial.begin(LINK_BAUD);
    uint8_t f[4] = {LINK_SOF, MSG_ENTER_BOOTLOADER, 0, 0};    // SOF, TYPE, LEN, CRC8
    for (int k = 1; k < 3; k++) {
      f[3] ^= f[k];
      for (int i = 0; i < 8; i++) f[3] = (f[3] & 0x80) ? (uint8_t)((f[3] << 1) ^ 0x07) : (uint8_t)(f[3] << 1);
    }
    Serial.write(f, 4);
    Serial.flush();
    delay(50);
  } else if (BOARD_REV >= 7) {
    // v0.7: power it up with XI held low: the bootloader samples P4.6 at power-on
    pinMode(PIN_CH559_BOOT, OUTPUT);
    digitalWrite(PIN_CH559_BOOT, LOW);
    digitalWrite(PIN_CH559_PWR_OFF, HIGH);
    delay(200);                                                // let VDD fall (C8 / C9)
    digitalWrite(PIN_CH559_PWR_OFF, LOW);
    delay(100);
  } else {
    pinMode(PIN_CH559_BOOT, OUTPUT);
    digitalWrite(PIN_CH559_BOOT, LOW);                         // XI low during the reset
    ch559_reset();
    delay(100);
  }
  serial_end();
}

void release_boot_pin() { pinMode(PIN_CH559_BOOT, INPUT); }

// PROGRAM needs a final empty chunk: the bootloader writes its buffer only then (wchisp does the
// same). VERIFY stops at the end of the data.
bool program_pass(uint8_t cmd, const uint8_t *image, uint32_t len, const uint8_t key[8]) {
  uint8_t buf[5 + CHUNK];
  uint8_t n;
  for (uint32_t addr = 0;; addr += n) {                        // the empty chunk goes at addr = len
    n = (uint8_t)min<uint32_t>(CHUNK, len - addr);
    buf[0] = (uint8_t)addr;
    buf[1] = (uint8_t)(addr >> 8);
    buf[2] = (uint8_t)(addr >> 16);
    buf[3] = (uint8_t)(addr >> 24);
    buf[4] = (uint8_t)random(256);                             // padding byte (ignored)
    for (uint8_t i = 0; i < n; i++) buf[5 + i] = image[addr + i] ^ key[i & 7];
    if (cmd == CMD_VERIFY && n == 0) break;
    if (!transfer(cmd, buf, 5 + n, 300)) return false;
    if (cmd == CMD_VERIFY && (resp_len < 1 || resp[0] != 0)) return false;
    if (cmd == CMD_PROGRAM && resp_len >= 1 && resp[0] != 0) return false;
    if (n == 0) break;
  }
  return true;
}

}  // namespace

void ch559_reset() {
  pinMode(PIN_CH559_RST, OUTPUT);
  digitalWrite(PIN_CH559_RST, HIGH);
  delay(10);
  digitalWrite(PIN_CH559_RST, LOW);
}

static const char *PENDING_FILE = "/kbb_isp";

bool ch559_isp_pending() { return InternalFS.exists(PENDING_FILE); }

void ch559_isp_set_pending(bool pending) {
  if (!pending) {
    InternalFS.remove(PENDING_FILE);
    return;
  }
  File f(InternalFS);
  if (f.open(PENDING_FILE, FILE_O_WRITE)) f.close();
}


Ch559IspResult ch559_isp_flash(const uint8_t *image, uint32_t len, bool app_running) {
  Ch559IspResult result = ISP_OK;
  enter_bootloader(app_running);

  do {
    // IDENTIFY: payload = chip id, chip type, "MCU ISP & WCH.CN". 115200 first, then 57600 (older
    // bootloaders, CH559EVT.PDF 3.1.1). Bootloader V2.40 answers at 115200.
    uint8_t id[2 + 16] = {CHIP_ID, CHIP_TYPE};
    memcpy(id + 2, "MCU ISP & WCH.CN", 16);
    bool ok = false;
    static const uint32_t bauds[] = {ISP_BAUD, 57600};
    for (uint32_t baud : bauds) {
      serial_end();
      Serial.begin(baud);
      for (int tries = 0; tries < 4 && !ok; tries++) ok = transfer(CMD_IDENTIFY, id, sizeof(id), 300);
      if (ok) break;
    }
    if (!ok) { result = ISP_NO_RESPONSE; break; }
    if (resp_len < 2 || resp[0] != CHIP_ID || resp[1] != CHIP_TYPE) { result = ISP_WRONG_CHIP; break; }

    // READ_CONFIG, all fields: [mask, 0, cfg x12, bootloader version x4, UID ...]
    uint8_t rc[2] = {0x1F, 0x00};
    if (!transfer(CMD_READ_CONFIG, rc, 2) || resp_len < 2 + 12 + 4 + 4) { result = ISP_CONFIG; break; }
    const uint8_t *uid = resp + 18;
    uint8_t s = uid[0] + uid[1] + uid[2] + uid[3];
    uint8_t key[8];
    for (auto &k : key) k = s;
    key[7] = (uint8_t)(key[7] + CHIP_ID);
    uint8_t key_sum = 0;
    for (auto k : key) key_sum += k;
    uint8_t seed[0x1E] = {0};

    uint16_t sectors = (len + 1023) / 1024;
    if (sectors < 8) sectors = 8;
    uint8_t er[4] = {(uint8_t)sectors, (uint8_t)(sectors >> 8), 0, 0};
    if (!transfer(CMD_ERASE, er, 4, 5000) || (resp_len >= 1 && resp[0] != 0)) { result = ISP_ERASE; break; }

    if (!transfer(CMD_ISP_KEY, seed, sizeof(seed)) || resp_len < 1 || resp[0] != key_sum) { result = ISP_KEY; break; }
    if (!program_pass(CMD_PROGRAM, image, len, key)) { result = ISP_PROGRAM; break; }

    if (!transfer(CMD_ISP_KEY, seed, sizeof(seed)) || resp_len < 1 || resp[0] != key_sum) { result = ISP_KEY; break; }
    if (!program_pass(CMD_VERIFY, image, len, key)) { result = ISP_VERIFY; break; }
  } while (false);

  release_boot_pin();
  if (result == ISP_OK) {
    uint8_t reason = 0;
    transfer(CMD_ISP_END, &reason, 1, 300);                    // the chip restarts itself
  }
  delay(20);
  ch559_reset();                                               // and once more, cleanly
  serial_end();
  return result;
}
