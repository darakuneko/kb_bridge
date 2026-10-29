/* KB Bridge CH559: framed UART0 link to the nRF52. */
#include "link.h"
#include "util.h"

#define RX_RING 128                       /* power of two */
static __xdata uint8_t rx_ring[RX_RING];
static volatile uint8_t rx_head, rx_tail;
static volatile __bit tx_busy;

void uart0_set_baud(uint32_t baud)
{
    /* timer1 mode 2 (8-bit auto reload) at Fsys, SMOD=1: baud = Fsys / 16 / (256 - TH1) */
    uint16_t div = (uint16_t)((fsys / 16UL + baud / 2) / baud);
    if (div < 1) div = 1;
    if (div > 256) div = 256;
    TR1 = 0;
    TH1 = (uint8_t)(256 - div);
    TR1 = 1;
}

void uart0_init(uint32_t baud)
{
    /* P0.3 TXD_ push-pull out, P0.2 RXD_ in with pull-up. P0 comes out of reset open-drain
     * (PORT_CFG bP0_OC = 1): TXD_ then only had the ~70k pull-up for "1", and switching the
     * keyboard's VBUS on coupled enough noise into it to upset the nRF's UART. */
    PORT_CFG &= (uint8_t)~bP0_OC;
    P0_DIR |= bTXD_;
    P0_DIR &= (uint8_t)~bRXD_;
    P0_PU |= bRXD_ | bTXD_;
    PIN_FUNC |= bUART0_PIN_X;

    SM0 = 0; SM1 = 1; SM2 = 0;            /* mode 1: 8N1, variable baud */
    PCON |= SMOD;
    TMOD = (TMOD & (uint8_t)~(bT1_GATE | bT1_CT | MASK_T1_MOD)) | bT1_M1;
    T2MOD |= bTMR_CLK | bT1_CLK;          /* timer1 at Fsys */
    uart0_set_baud(baud);
    rx_head = rx_tail = 0;
    tx_busy = 0;
    RI = 0; TI = 0;
    REN = 1;
    ES = 1;
}

void uart0_isr(void) __interrupt(INT_NO_UART0)
{
    if (RI) {
        uint8_t next = (uint8_t)(rx_head + 1) & (RX_RING - 1);
        RI = 0;
        if (next != rx_tail) {            /* drop on overflow */
            rx_ring[rx_head] = SBUF;
            rx_head = next;
        }
    }
    if (TI) {
        TI = 0;
        tx_busy = 0;
    }
}

uint8_t uart0_rx_count(void)
{
    return (uint8_t)(rx_head - rx_tail) & (RX_RING - 1);
}

uint8_t uart0_getc(void)
{
    uint8_t c = rx_ring[rx_tail];
    rx_tail = (uint8_t)(rx_tail + 1) & (RX_RING - 1);
    return c;
}

void uart0_putc(uint8_t c)
{
    while (tx_busy)
        ;
    tx_busy = 1;
    SBUF = c;
}

uint8_t uart0_tx_idle(void)
{
    return !tx_busy;
}

/* ---- frames ---------------------------------------------------------------------------- */
static uint8_t crc8_update(uint8_t crc, uint8_t b)
{
    uint8_t i;
    crc ^= b;
    for (i = 0; i < 8; i++)
        crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
    return crc;
}

static uint8_t tx_crc;

static void put_crc(uint8_t b)
{
    tx_crc = crc8_update(tx_crc, b);
    uart0_putc(b);
}

void link_send2(uint8_t type, const uint8_t *head, uint8_t head_len,
                const uint8_t __xdata *data, uint8_t data_len)
{
    uint8_t i;
    if ((uint16_t)head_len + data_len > LINK_MAX_PAYLOAD)
        return;
    tx_crc = 0;
    uart0_putc(LINK_SOF);
    put_crc(type);
    put_crc(head_len + data_len);
    for (i = 0; i < head_len; i++)
        put_crc(head[i]);
    for (i = 0; i < data_len; i++)
        put_crc(data[i]);
    uart0_putc(tx_crc);
}

void link_send(uint8_t type, const uint8_t *payload, uint8_t len)
{
    link_send2(type, payload, len, 0, 0);
}

/* ---- receiver ------------------------------------------------------------------------ */
uint8_t link_rx_type;
uint8_t link_rx_len;
__xdata uint8_t link_rx_buf[LINK_MAX_PAYLOAD];

enum { RX_SOF, RX_TYPE, RX_LEN, RX_DATA, RX_CRC };
static uint8_t rx_state = RX_SOF;
static uint8_t rx_pos, rx_crc;
static uint16_t rx_last_ms;

uint8_t link_poll(void)
{
    uint8_t c;
    if (rx_state != RX_SOF && (uint16_t)(millis() - rx_last_ms) > LINK_BYTE_TIMEOUT_MS)
        rx_state = RX_SOF;
    while (uart0_rx_count()) {
        c = uart0_getc();
        rx_last_ms = millis();
        switch (rx_state) {
        case RX_SOF:
            if (c == LINK_SOF) { rx_state = RX_TYPE; rx_crc = 0; }
            break;
        case RX_TYPE:
            link_rx_type = c;
            rx_crc = crc8_update(rx_crc, c);
            rx_state = RX_LEN;
            break;
        case RX_LEN:
            if (c > LINK_MAX_PAYLOAD) { rx_state = RX_SOF; break; }
            link_rx_len = c;
            rx_crc = crc8_update(rx_crc, c);
            rx_pos = 0;
            rx_state = c ? RX_DATA : RX_CRC;
            break;
        case RX_DATA:
            link_rx_buf[rx_pos++] = c;
            rx_crc = crc8_update(rx_crc, c);
            if (rx_pos == link_rx_len) rx_state = RX_CRC;
            break;
        case RX_CRC:
            rx_state = RX_SOF;
            if (c == rx_crc) return 1;
            break;
        }
    }
    return 0;
}
