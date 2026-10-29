/* KB Bridge CH559: framed UART0 link to the nRF52 (see nrf/kb_bridge/link_proto.h). */
#ifndef LINK_H
#define LINK_H

#include <stdint.h>
#include "ch559_regs.h"
#include "link_proto.h"

void uart0_init(uint32_t baud);          /* UART0 on P0.2 (RXD) / P0.3 (TXD), RX interrupt */
void uart0_set_baud(uint32_t baud);
uint8_t uart0_rx_count(void);
uint8_t uart0_getc(void);                /* call only when uart0_rx_count() > 0 */
void uart0_putc(uint8_t c);              /* blocking */
uint8_t uart0_tx_idle(void);

void link_send(uint8_t type, const uint8_t *payload, uint8_t len);
void link_send2(uint8_t type, const uint8_t *head, uint8_t head_len,
                const uint8_t __xdata *data, uint8_t data_len);

/* Poll the receiver. Returns 1 when a complete frame is in link_rx_type/link_rx_len/link_rx_buf. */
uint8_t link_poll(void);
extern uint8_t link_rx_type;
extern uint8_t link_rx_len;
extern __xdata uint8_t link_rx_buf[LINK_MAX_PAYLOAD];

void uart0_isr(void) __interrupt(INT_NO_UART0);

#endif
