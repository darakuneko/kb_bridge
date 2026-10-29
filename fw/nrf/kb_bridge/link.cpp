// KB Bridge nRF: framed UART link to the CH559.
#include "link.h"
#include "config.h"
#include "wake.h"

static uint8_t crc8_update(uint8_t crc, uint8_t b) {
  crc ^= b;
  for (int i = 0; i < 8; i++) crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
  return crc;
}

static bool uart_on;
static bool peer_asleep;

void link_begin(uint32_t baud) {
  Serial.begin(baud);
  uart_on = true;
}

// While the CH559 sleeps the UART is off too (its receiver alone costs ~0.9 mA); the RX pin waits
// for a falling edge instead. The CH559 sends one 0x00 byte when it wakes up by itself and its
// first frame LINK_WAKE_MS x 2 later, time enough to start the UART again.
static volatile bool rx_edge;

static void on_rx_edge() {
  rx_edge = true;
  loop_wake_from_isr();
}

static void uart_start() {
  if (uart_on) return;
  detachInterrupt(PIN_LINK_RX);
  Serial.begin(LINK_BAUD);
  uart_on = true;
}

static void uart_stop() {
  if (!uart_on) return;
  Serial.end();
  uart_on = false;
  rx_edge = false;
  pinMode(PIN_LINK_TX, OUTPUT);                    // idle high: a floating line could wake the CH559
  digitalWrite(PIN_LINK_TX, HIGH);
  pinMode(PIN_LINK_RX, INPUT);
  attachInterrupt(PIN_LINK_RX, on_rx_edge, FALLING);
}

// v0.7: the CH559 is switched off. Its pins clamp to its (0 V) supply: TX low, RX left floating.
void link_shutdown() {
  if (uart_on) Serial.end();
  else detachInterrupt(PIN_LINK_RX);
  uart_on = false;
  rx_edge = false;
  peer_asleep = false;
  pinMode(PIN_LINK_TX, OUTPUT);
  digitalWrite(PIN_LINK_TX, LOW);
  pinMode(PIN_LINK_RX, INPUT);
}

// The CH559 powered down after MSG_SLEEP: one 0x00 byte (RXD low) wakes it; the byte itself is
// lost while its clock starts. Any frame from it means it is awake.
static uint32_t sleep_sent_ms;

void link_peer_sleeps() {
  peer_asleep = true;
  sleep_sent_ms = millis();
}
bool link_peer_asleep() { return peer_asleep; }
bool link_uart_off() { return !uart_on; }

void link_idle_task() {
  // it answers at once when it does not sleep: keep listening a moment before switching off
  if (peer_asleep && uart_on && millis() - sleep_sent_ms > LINK_UART_OFF_DELAY_MS) uart_stop();
}

void link_wake_peer() {
  uart_start();
  if (!peer_asleep) return;
  peer_asleep = false;
  Serial.write((uint8_t)0x00);
  Serial.flush();
  delay(LINK_WAKE_MS);
}

void link_send2(uint8_t type, const uint8_t *head, uint8_t head_len, const uint8_t *data, uint8_t data_len) {
  uint8_t len = head_len + data_len;
  if (len > LINK_MAX_PAYLOAD) return;
  link_wake_peer();
  uint8_t buf[LINK_MAX_PAYLOAD + 4];
  uint8_t n = 0, crc = 0;
  buf[n++] = LINK_SOF;
  buf[n++] = type;
  buf[n++] = len;
  if (head_len) memcpy(buf + n, head, head_len), n += head_len;
  if (data_len) memcpy(buf + n, data, data_len), n += data_len;
  for (uint8_t i = 1; i < n; i++) crc = crc8_update(crc, buf[i]);
  buf[n++] = crc;
  Serial.write(buf, n);
}

void link_send(uint8_t type, const uint8_t *payload, uint8_t len) { link_send2(type, payload, len, nullptr, 0); }

enum { RX_SOF, RX_TYPE, RX_LEN, RX_DATA, RX_CRC };
static uint8_t rx_state = RX_SOF, rx_pos, rx_crc;
static uint32_t rx_last_ms;
// The frame being received lives here, not in the caller's LinkFrame: a frame often spans several
// loop() passes, and the caller's (stack) frame is a new one each time. Writing into it with a
// length from an earlier pass would run rx_pos past data[] and over loop()'s stack.
static LinkFrame rx;

bool link_poll(LinkFrame &f) {
  if (!uart_on) {
    if (!rx_edge) return false;
    rx_edge = false;
    peer_asleep = false;           // it woke by itself; else link_idle_task() would stop the UART again
    uart_start();
  }
  if (rx_state != RX_SOF && millis() - rx_last_ms > LINK_BYTE_TIMEOUT_MS) rx_state = RX_SOF;
  while (Serial.available()) {
    uint8_t c = Serial.read();
    rx_last_ms = millis();
    switch (rx_state) {
      case RX_SOF:
        if (c == LINK_SOF) { rx_state = RX_TYPE; rx_crc = 0; }
        break;
      case RX_TYPE:
        rx.type = c; rx_crc = crc8_update(rx_crc, c); rx_state = RX_LEN;
        break;
      case RX_LEN:
        if (c > LINK_MAX_PAYLOAD) { rx_state = RX_SOF; break; }
        rx.len = c; rx_crc = crc8_update(rx_crc, c); rx_pos = 0;
        rx_state = c ? RX_DATA : RX_CRC;
        break;
      case RX_DATA:
        if (rx_pos >= sizeof(rx.data)) { rx_state = RX_SOF; break; }
        rx.data[rx_pos++] = c; rx_crc = crc8_update(rx_crc, c);
        if (rx_pos == rx.len) rx_state = RX_CRC;
        break;
      case RX_CRC:
        rx_state = RX_SOF;
        if (c == rx_crc) {
          peer_asleep = false;
          f.type = rx.type;
          f.len = rx.len;
          memcpy(f.data, rx.data, rx.len);
          return true;
        }
        break;
    }
  }
  return false;
}
