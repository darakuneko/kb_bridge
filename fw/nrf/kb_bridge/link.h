// KB Bridge nRF: framed UART link to the CH559 (see link_proto.h).
#pragma once
#include <Arduino.h>
#include "link_proto.h"

struct LinkFrame {
  uint8_t type;
  uint8_t len;
  uint8_t data[LINK_MAX_PAYLOAD];
};

void link_begin(uint32_t baud = LINK_BAUD);
void link_peer_sleeps();                             // MSG_SLEEP was sent
bool link_peer_asleep();
void link_wake_peer();                               // no-op when awake; link_send* call it
bool link_uart_off();                                // UART stopped while the CH559 sleeps
void link_idle_task();                               // call every loop(): stops the UART when due
void link_shutdown();                                // v0.7: CH559 switched off (TX low); link_begin() to resume
bool link_poll(LinkFrame &f);                        // true when a complete frame arrived
void link_send(uint8_t type, const uint8_t *payload, uint8_t len);
void link_send2(uint8_t type, const uint8_t *head, uint8_t head_len, const uint8_t *data, uint8_t data_len);
