/*
 * KB Bridge CH559: update mode. The CH559 becomes a USB CDC-ACM serial port on J1 and passes the
 * bytes to and from the nRF's UART, so the PC flashes the nRF with the Adafruit serial DFU
 * (adafruit-nrfutil / Arduino IDE "Adafruit Feather nRF52832") as if through a USB-serial chip.
 * DTR assertion pulses the nRF's RESET (P3.2), like the Feather's DTR -> RESET capacitor.
 */
#include <string.h>
#include "cdc.h"
#include "util.h"
#include "link.h"

/* endpoint buffers (xRAM, even addresses) */
static __xdata __at(0x0080) uint8_t ep0_buf[64];
static __xdata __at(0x00C0) uint8_t ep1_buf[64];
/* EP2: OUT at UEP2_DMA, IN at UEP2_DMA + 64. Two separate arrays: SDCC 4.x dropped the low
 * byte of "ep2_buf + 64" for an __at array and wrote the IN data over the OUT buffer. */
static __xdata __at(0x0100) uint8_t ep2_buf[64];       /* OUT */
static __xdata __at(0x0140) uint8_t ep2_in_buf[64];    /* IN */

#define EP0_SIZE 64

/* ---- descriptors -------------------------------------------------------------------------- */
/* VID/PID 7A79:1576 (KB Bridge; the nRF's BLE PnP ID uses the same pair) */
static __code const uint8_t dev_desc[18] = {
    18, 0x01, 0x00, 0x02, 0x02, 0x00, 0x00, EP0_SIZE,
    0x79, 0x7A, 0x76, 0x15, 0x00, 0x01, 1, 2, 3, 1
};

static __code const uint8_t cfg_desc[67] = {
    9, 0x02, 67, 0, 2, 1, 0, 0x80, 50,                     /* config: 2 interfaces, 100 mA */
    /* interface 0: CDC communication */
    9, 0x04, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,                             /* header */
    5, 0x24, 0x01, 0x00, 0x01,                             /* call management */
    4, 0x24, 0x02, 0x02,                                   /* ACM: line coding + control lines */
    5, 0x24, 0x06, 0, 1,                                   /* union */
    7, 0x05, 0x81, 0x03, 8, 0, 255,                        /* EP1 IN interrupt (notifications) */
    /* interface 1: CDC data */
    9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 0x05, 0x02, 0x02, 64, 0, 0,                         /* EP2 OUT bulk */
    7, 0x05, 0x82, 0x02, 64, 0, 0                          /* EP2 IN bulk */
};

static __code const uint8_t str_lang[4] = { 4, 0x03, 0x09, 0x04 };
static __code const uint8_t str_mfr[20] = {
    20, 0x03, 'K',0, 'B',0, ' ',0, 'B',0, 'r',0, 'i',0, 'd',0, 'g',0, 'e',0 };
static __code const uint8_t str_prod[34] = {
    34, 0x03, 'K',0, 'B',0, ' ',0, 'B',0, 'r',0, 'i',0, 'd',0, 'g',0, 'e',0, ' ',0,
    'U',0, 'p',0, 'd',0, 'a',0, 't',0, 'e',0 };
static __code const uint8_t str_serial[10] = { 10, 0x03, '0',0, '0',0, '0',0, '1',0 };

/* ---- state ---------------------------------------------------------------------------- */
static __xdata uint8_t line_coding[7] = { 0x00, 0xC2, 0x01, 0x00, 0, 0, 8 };   /* 115200 8N1 */
static const uint8_t __code *ep0_src;
static uint16_t ep0_remain;
static uint8_t pending_addr;
static uint8_t expect_line_coding;
static uint8_t dtr;

#define TX_RING 256                  /* USB OUT -> UART */
static __xdata uint8_t tx_ring[TX_RING];
static uint8_t tx_head, tx_tail;     /* 8-bit indexes wrap at 256 */
static uint8_t in_busy;

static void nrf_reset_pulse(void)
{
    P3_2 = !NRF_RESET_IDLE;
    delay_ms(10);
    P3_2 = NRF_RESET_IDLE;
}

static void apply_line_coding(void)
{
    uint32_t baud = line_coding[0] | ((uint32_t)line_coding[1] << 8) |
                    ((uint32_t)line_coding[2] << 16) | ((uint32_t)line_coding[3] << 24);
    if (baud >= 12000 && baud <= 3000000)
        uart0_set_baud(baud);
}

static void ep0_send_chunk(void)
{
    uint8_t n = ep0_remain > EP0_SIZE ? EP0_SIZE : (uint8_t)ep0_remain;
    memcpy(ep0_buf, ep0_src, n);
    ep0_src += n;
    ep0_remain -= n;
    UEP0_T_LEN = n;
}

static void usb_dev_reset_eps(void)
{
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
    UEP1_CTRL = bUEP_AUTO_TOG | UEP_T_RES_NAK;
    UEP2_CTRL = bUEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
    USB_DEV_AD = 0;
    in_busy = 0;
    expect_line_coding = 0;
    ep0_remain = 0;
    pending_addr = 0;
}

static void usb_dev_init(void)
{
    IE_USB = 0;
    USB_CTRL = 0x00;
    UEP0_DMA_H = 0x00; UEP0_DMA_L = 0x80;
    UEP1_DMA_H = 0x00; UEP1_DMA_L = 0xC0;
    UEP2_DMA_H = 0x01; UEP2_DMA_L = 0x00;
    UEP4_1_MOD = bUEP1_TX_EN;
    UEP2_3_MOD = bUEP2_RX_EN | bUEP2_TX_EN;
    usb_dev_reset_eps();
    UHUB1_CTRL = bUH1_DISABLE;                            /* HP/HM (keyboard side) off */
    UDEV_CTRL = bUD_DP_PD_DIS | bUD_DM_PD_DIS;            /* device: no pull-downs */
    USB_CTRL = bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN; /* D+ pull-up: full speed */
    UDEV_CTRL |= bUD_PORT_EN;
    USB_INT_FG = 0xFF;
    USB_INT_EN = bUIE_SUSPEND | bUIE_TRANSFER | bUIE_BUS_RST;
}

static void handle_setup(void)
{
    uint8_t type = ep0_buf[0], req = ep0_buf[1];
    uint8_t val_l = ep0_buf[2], val_h = ep0_buf[3];
    uint16_t wlen = ep0_buf[6] | ((uint16_t)ep0_buf[7] << 8);
    uint8_t stall = 0;
    uint8_t new_dtr;

    ep0_remain = 0;
    UEP0_T_LEN = 0;
    if ((type & 0x60) == 0x00) {                          /* standard */
        switch (req) {
        case 0x06:                                        /* GET_DESCRIPTOR */
            if (val_h == 1)      { ep0_src = dev_desc; ep0_remain = sizeof(dev_desc); }
            else if (val_h == 2) { ep0_src = cfg_desc; ep0_remain = sizeof(cfg_desc); }
            else if (val_h == 3) {
                if (val_l == 0)      { ep0_src = str_lang;   ep0_remain = sizeof(str_lang); }
                else if (val_l == 1) { ep0_src = str_mfr;    ep0_remain = sizeof(str_mfr); }
                else if (val_l == 2) { ep0_src = str_prod;   ep0_remain = sizeof(str_prod); }
                else if (val_l == 3) { ep0_src = str_serial; ep0_remain = sizeof(str_serial); }
                else stall = 1;
            } else stall = 1;
            if (!stall) {
                if (ep0_remain > wlen) ep0_remain = wlen;
                ep0_send_chunk();
            }
            break;
        case 0x05:                                        /* SET_ADDRESS: after the status stage */
            pending_addr = val_l;
            break;
        case 0x09:                                        /* SET_CONFIGURATION */
            break;
        case 0x08:                                        /* GET_CONFIGURATION */
            ep0_buf[0] = 1;
            UEP0_T_LEN = 1;
            break;
        case 0x00:                                        /* GET_STATUS */
            ep0_buf[0] = 0; ep0_buf[1] = 0;
            UEP0_T_LEN = 2;
            break;
        case 0x01:                                        /* CLEAR_FEATURE */
        case 0x03:                                        /* SET_FEATURE */
        case 0x0B:                                        /* SET_INTERFACE */
            break;
        case 0x0A:                                        /* GET_INTERFACE */
            ep0_buf[0] = 0;
            UEP0_T_LEN = 1;
            break;
        default:
            stall = 1;
        }
    } else if ((type & 0x60) == 0x20) {                   /* class (CDC) */
        switch (req) {
        case 0x20:                                        /* SET_LINE_CODING: data stage follows */
            expect_line_coding = 1;
            UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
            return;
        case 0x21:                                        /* GET_LINE_CODING */
            memcpy(ep0_buf, line_coding, 7);
            UEP0_T_LEN = 7;
            break;
        case 0x22:                                        /* SET_CONTROL_LINE_STATE */
            /* Reset only when DTR is asserted, like the Feather's DTR -> 100 nF -> RESET (falling
             * edge only). Resetting on de-assert as well hit the nRF while its bootloader was
             * still saving "app valid" after the DFU (the host closes the port right after the
             * activate command), so the new application was never started. */
            new_dtr = val_l & 0x01;
            if (new_dtr && !dtr)
                nrf_reset_pulse();
            dtr = new_dtr;
            break;
        default:
            stall = 1;
        }
    } else {
        stall = 1;
    }

    if (stall)
        UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_STALL | UEP_T_RES_STALL;
    else
        UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;   /* DATA1 */
}

static void handle_transfer(void)
{
    uint8_t token = USB_INT_ST & (MASK_UIS_TOKEN | MASK_UIS_ENDP);
    uint8_t n, i;

    switch (token) {
    case UIS_TOKEN_SETUP | 0:
        handle_setup();
        break;
    case UIS_TOKEN_IN | 0:
        if (pending_addr) {
            USB_DEV_AD = (USB_DEV_AD & bUDA_GP_BIT) | pending_addr;
            pending_addr = 0;
            UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        } else if (ep0_remain) {
            ep0_send_chunk();
            UEP0_CTRL ^= bUEP_T_TOG;
        } else {
            UEP0_T_LEN = 0;
            UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        }
        break;
    case UIS_TOKEN_OUT | 0:
        if (expect_line_coding && U_TOG_OK && USB_RX_LEN >= 7) {
            memcpy(line_coding, ep0_buf, 7);
            apply_line_coding();
            expect_line_coding = 0;
            UEP0_T_LEN = 0;
            UEP0_CTRL = bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_ACK;   /* status: ZLP DATA1 */
        } else {
            UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;                /* end of an IN transfer */
        }
        break;
    case UIS_TOKEN_OUT | 2:
        if (U_TOG_OK) {
            n = USB_RX_LEN;
            for (i = 0; i < n; i++)
                tx_ring[tx_head++] = ep2_buf[i];
            if ((uint8_t)(tx_head - tx_tail) > TX_RING - 64 - 1)
                UEP2_CTRL = (UEP2_CTRL & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_NAK;   /* no room for more */
        }
        break;
    case UIS_TOKEN_IN | 2:
        UEP2_CTRL = (UEP2_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        in_busy = 0;
        break;
    case UIS_TOKEN_IN | 1:
        UEP1_CTRL = (UEP1_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_NAK;
        break;
    }
}

void cdc_bridge_run(void)
{
    uint8_t n;

    P3_2 = NRF_RESET_IDLE;
    dtr = 0;
    tx_head = tx_tail = 0;
    uart0_init(115200);
    usb_dev_init();

    for (;;) {
        wdog_feed();
        if (UIF_TRANSFER) {
            handle_transfer();
            UIF_TRANSFER = 0;
        }
        if (UIF_BUS_RST) {
            usb_dev_reset_eps();
            UIF_BUS_RST = 0;
        }
        if (UIF_SUSPEND)
            UIF_SUSPEND = 0;

        /* USB OUT -> UART */
        if (tx_head != tx_tail && uart0_tx_idle()) {
            uart0_putc(tx_ring[tx_tail++]);
            if ((uint8_t)(tx_head - tx_tail) <= TX_RING - 64 - 1)
                UEP2_CTRL = (UEP2_CTRL & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_ACK;
        }
        /* UART -> USB IN */
        if (!in_busy && (n = uart0_rx_count()) != 0) {
            uint8_t i;
            if (n > 64) n = 64;
            for (i = 0; i < n; i++)
                ep2_in_buf[i] = uart0_getc();
            UEP2_T_LEN = n;
            UEP2_CTRL = (UEP2_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_ACK;
            in_busy = 1;
        }
    }
}
