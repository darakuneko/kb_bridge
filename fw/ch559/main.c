/*
 * KB Bridge CH559 firmware.
 *
 * Normal mode: USB host for the keyboard on J2, relays everything to the nRF52 over UART0.
 *   Fsys 12 MHz (USB keeps its own 48 MHz), link 250 kbaud, watchdog 5.6 s. Powers down on the
 *   nRF's MSG_SLEEP while there is no device or the bus is suspended (usbhost_sleep).
 * Update mode (PAIR held at power-on): USB CDC serial on J1 <-> nRF52 UART, for the nRF DFU.
 *   Fsys 48 MHz so 115200 baud is accurate.
 *
 * Pins: P0.2 RXD_ <- nRF P0.06, P0.3 TXD_ -> nRF P0.08, P3.2 -> nRF RESET, P3.3 <- PAIR (low),
 *       P4.6/P4.7 crystal (the nRF may hold XI low + pulse RST to enter the bootloader).
 */
#include "util.h"
#include "link.h"
#include "usbhost.h"
#include "cdc.h"

#define FW_MAJOR 0
#define FW_MINOR 3
#ifndef BUILD_ID
#define BUILD_ID 0x00000000UL          /* set by the Makefile: CRC32 of the sources */
#endif

typedef void (*entry_t)(void);

static void send_hello(void)
{
    static __xdata uint8_t m[7];
    uint32_t id = BUILD_ID;
    m[0] = LINK_PROTO_VERSION;
    m[1] = FW_MAJOR;
    m[2] = FW_MINOR;
    m[3] = (uint8_t)id;
    m[4] = (uint8_t)(id >> 8);
    m[5] = (uint8_t)(id >> 16);
    m[6] = (uint8_t)(id >> 24);
    link_send(MSG_HELLO, m, 7);
}

static void enter_bootloader(void)
{
    EA = 0;
    safe_mode_open();
    GLOBAL_CFG &= (uint8_t)~bWDOG_EN;                 /* a running watchdog would trap the bootloader */
    safe_mode_close();
    USB_CTRL = 0;
    UHUB1_CTRL = 0;
    ((entry_t)0xF400)();
}

static uint8_t pair_held(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < 10; i++) {           /* 10 ms of samples, debounced */
        if (!P3_3) n++;
        delay_ms(1);
    }
    return n >= 8;
}

static void send_status(void)
{
    static __xdata uint8_t m[5];
    usbhost_status(m);
    link_send(MSG_STATUS, m, 5);
}

void main(void)
{
#if BOARD_REV >= 7
    /* P3.2 -> N-FET gate (nRF RESET): push-pull, idle low; P3.3 (PAIR, through a diode): input with
     * pull-up. The latch comes out of reset at 1: clear it before the pin turns push-pull, or it
     * drives high for a few cycles, which is enough to reset the nRF through the FET. First thing
     * after reset, so the pin spends as little time as possible on the weak reset pull-up. */
    P3_2 = 0;
    P3_3 = 1;
    PORT_CFG &= (uint8_t)~bP3_OC;
    P3_DIR = (P3_DIR & (uint8_t)~0x08) | 0x04;
    P3_PU |= 0x08;
#endif
    clock_init(FSYS_12MHZ);
    tick_init();

#if BOARD_REV < 7
    /* P3.2 (nRF RESET): quasi-bidirectional, idle high; P3.3 (PAIR): input with pull-up */
    PORT_CFG |= bP3_OC;
    P3_DIR &= (uint8_t)~0x0C;
    P3_PU |= 0x0C;
    P3_2 = 1;
    P3_3 = 1;
#endif
    EA = 1;

    if (pair_held()) {
        clock_init(FSYS_48MHZ);
        tick_init();
        wdog_enable();
        cdc_bridge_run();
    }

    /* Host mode never drives the nRF's RESET: P3.2 open-drain released and without pull-up
     * (the nRF has its own). Only the update mode pulses it. */
#if BOARD_REV < 7
    P3_PU &= (uint8_t)~0x04;
    P3_2 = 1;
#endif

    wdog_enable();
    safe_mode_open();                    /* clocks off for the peripherals host mode does not use */
    SLEEP_CTRL = bSLP_OFF_ADC | bSLP_OFF_UART1 | bSLP_OFF_P1S1 | bSLP_OFF_SPI0 | bSLP_OFF_TMR3 | bSLP_OFF_LED;
    safe_mode_close();
    uart0_init(LINK_BAUD);
    usbhost_init();
    send_hello();

    for (;;) {
        wdog_feed();
        if (link_poll()) {
            switch (link_rx_type) {
            case MSG_PING:
                send_status();
                break;
            case MSG_USB_SUSPEND:
                usbhost_suspend();
                break;
            case MSG_USB_RESUME:
                usbhost_resume();
                break;
            case MSG_OUT_REPORT:
                usbhost_out_report(link_rx_buf, link_rx_len);
                break;
            case MSG_RE_ENUMERATE:
                usbhost_re_enumerate();
                break;
            case MSG_ENTER_BOOTLOADER:
                enter_bootloader();
                break;
            case MSG_SLEEP:
                while (!uart0_tx_idle())
                    ;
                if (usbhost_sleep() == SLEEP_WOKE_SELF) {
                    uart0_putc(0x00);            /* wakes the nRF's UART (link.cpp) */
                    delay_ms(2 * LINK_WAKE_MS);
                }
                send_status();
                break;
            default:
                break;
            }
        }
        usbhost_poll();
    }
}
