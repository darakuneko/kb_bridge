/*
 * KB Bridge CH559: USB host on root port 1 (HP/HM, the keyboard plug J2). One device, no hubs.
 *
 * The CH559 only relays: it enumerates the device, sends every HID interface and its report
 * descriptor to the nRF, then polls the interrupt IN endpoints and forwards each report as is.
 * The nRF parses the descriptors and decides what the reports mean.
 */
#include <string.h>
#include "usbhost.h"
#include "util.h"
#include "link.h"

/* DMA buffers: must be in xRAM at even addresses (the linker places other xdata from 0x0200) */
static __xdata __at(0x0000) uint8_t host_rx[64];
static __xdata __at(0x0040) uint8_t host_tx[64];

#define MAX_IFACES   6
#define CFG_BUF_LEN  512
static __xdata uint8_t cfg_buf[CFG_BUF_LEN];

typedef struct {
    uint8_t num, subclass, protocol;
    uint8_t ep_in, ep_in_size, interval;
    uint8_t ep_out, ep_out_size;
    uint16_t desc_len;
    uint8_t tog_in, tog_out;
    uint16_t last_poll;
} iface_t;

static __xdata iface_t ifaces[MAX_IFACES];
static uint8_t n_ifaces;
static uint8_t ep0_size;
static uint8_t cfg_value;

enum { ST_IDLE, ST_ATTACHED, ST_RUNNING, ST_FAILED };
static uint8_t state;
static uint16_t state_ms;

/* health and power state (reported in MSG_STATUS) */
static uint16_t in_errors;                       /* consecutive failed IN transactions */
static uint8_t enum_failures;
static uint8_t remote_wakeup;                    /* the configuration supports remote wakeup */
static uint8_t suspended;
static uint16_t resumed_ms;                      /* when the bus was resumed (error check window) */
static uint8_t full_speed;

#define IN_ERROR_LIMIT        200                /* consecutive errors that mean the device is gone */
#define RESUME_ERROR_LIMIT    20                 /* ... right after a resume: re-enumerate quickly */
#define RESUME_WINDOW_MS      200

#define PID(pid, ep) ((uint8_t)(((pid) << 4) | (ep)))

/* transfer results */
#define R_OK      0
#define R_NAK     1
#define R_STALL   2
#define R_ERR     3

void usbhost_resume(void);

/* ---- low level ------------------------------------------------------------------------- */
static void port_disable(void)
{
    UHUB1_CTRL = 0;
    USB_DEV_AD = 0;
}

void usbhost_init(void)
{
    IE_USB = 0;
    USB_CTRL = bUC_HOST_MODE;
    USB_DEV_AD = 0;
    UH_EP_MOD = bUH_EP_TX_EN | bUH_EP_RX_EN;
    UH_RX_DMA_H = 0x00; UH_RX_DMA_L = 0x00;      /* host_rx */
    UH_TX_DMA_H = 0x00; UH_TX_DMA_L = 0x40;      /* host_tx */
    UH_RX_CTRL = 0;
    UH_TX_CTRL = 0;
    USB_CTRL = bUC_HOST_MODE | bUC_INT_BUSY | bUC_DMA_EN;
    UH_SETUP = bUH_SOF_EN;
    USB_INT_FG = 0xFF;
    UHUB0_CTRL = 0;                              /* root port 0 (J1) unused in host mode */
    port_disable();
    USB_INT_EN = bUIE_TRANSFER | bUIE_DETECT;    /* flags only, polled */
    state = ST_IDLE;
}

/* One transaction. pid_ep = (PID << 4) | endpoint. tog: DATA1 when non-zero.
 * nak_ms: how long to keep retrying NAKs (0 = return R_NAK at once). */
static uint8_t host_transfer(uint8_t pid_ep, uint8_t tog, uint16_t nak_ms)
{
    uint8_t res, ok, tries = 0;
    uint16_t t, start = millis();

    UH_RX_CTRL = tog ? bUH_R_TOG : 0;
    UH_TX_CTRL = tog ? bUH_T_TOG : 0;
    for (;;) {
        UH_EP_PID = pid_ep;
        UIF_TRANSFER = 0;
        for (t = 2000; t && !UIF_TRANSFER; t--)
            delay_us(1);
        wdog_feed();
        UH_EP_PID = 0;
        if (!UIF_TRANSFER)
            return R_ERR;
        ok = U_TOG_OK;
        res = USB_INT_ST & MASK_UIS_H_RES;
        UIF_TRANSFER = 0;
        if (ok)
            return R_OK;
        if (res == USB_PID_STALL)
            return R_STALL;
        if (res == USB_PID_NAK) {
            if (nak_ms == 0 || (uint16_t)(millis() - start) >= nak_ms)
                return R_NAK;
            continue;
        }
        if ((pid_ep >> 4) == USB_PID_IN && (res == USB_PID_DATA0 || res == USB_PID_DATA1))
            return R_ERR;                        /* toggle mismatch: repeated packet, drop it */
        if (++tries >= 3)
            return R_ERR;
        delay_us(20);
    }
}

/* Control transfer on endpoint 0. setup: 8 bytes. IN data goes to buf (up to buf_len) or, when
 * stream_iface != 0xFF, straight to the nRF as MSG_DESC pieces. OUT data comes from buf. */
static uint16_t ctrl_len;

static uint8_t host_ctrl(const uint8_t *setup, uint8_t __xdata *buf, uint16_t buf_len,
                         uint8_t stream_iface)
{
    uint8_t r, n, tog = 1, is_in = setup[0] & 0x80;
    uint16_t remain = setup[6] | ((uint16_t)setup[7] << 8);
    static __xdata uint8_t head[3];

    ctrl_len = 0;
    memcpy(host_tx, setup, 8);
    UH_TX_LEN = 8;
    r = host_transfer(PID(USB_PID_SETUP, 0), 0, 200);
    if (r != R_OK)
        return r;

    if (is_in) {
        while (remain) {
            r = host_transfer(PID(USB_PID_IN, 0), tog, 500);
            if (r != R_OK)
                return r;
            tog ^= 1;
            n = USB_RX_LEN;
            if (n > remain) n = (uint8_t)remain;
            if (stream_iface != 0xFF) {
                head[0] = stream_iface;
                head[1] = (uint8_t)ctrl_len;
                head[2] = (uint8_t)(ctrl_len >> 8);
                link_send2(MSG_DESC, head, 3, host_rx, n);
            } else if (buf && ctrl_len + n <= buf_len) {
                memcpy(buf + ctrl_len, host_rx, n);
            }
            ctrl_len += n;
            remain -= n;
            if (USB_RX_LEN < ep0_size)
                break;                           /* short packet ends the data stage */
        }
        UH_TX_LEN = 0;                           /* status: zero-length OUT, DATA1 */
        return host_transfer(PID(USB_PID_OUT, 0), 1, 200);
    }

    while (remain) {
        n = remain > ep0_size ? ep0_size : (uint8_t)remain;
        memcpy(host_tx, buf + ctrl_len, n);
        UH_TX_LEN = n;
        r = host_transfer(PID(USB_PID_OUT, 0), tog, 200);
        if (r != R_OK)
            return r;
        tog ^= 1;
        ctrl_len += n;
        remain -= n;
    }
    return host_transfer(PID(USB_PID_IN, 0), 1, 200);    /* status: zero-length IN, DATA1 */
}

static __xdata uint8_t setup_buf[8];

static void make_setup(uint8_t type, uint8_t req, uint16_t value, uint16_t index, uint16_t len)
{
    setup_buf[0] = type;
    setup_buf[1] = req;
    setup_buf[2] = (uint8_t)value;
    setup_buf[3] = (uint8_t)(value >> 8);
    setup_buf[4] = (uint8_t)index;
    setup_buf[5] = (uint8_t)(index >> 8);
    setup_buf[6] = (uint8_t)len;
    setup_buf[7] = (uint8_t)(len >> 8);
}

/* ---- enumeration ------------------------------------------------------------------------ */
static void port_reset(void)
{
    USB_DEV_AD = 0;
    UHUB1_CTRL = (UHUB1_CTRL & (uint8_t)~bUH_LOW_SPEED) | bUH_BUS_RESET;
    delay_ms(15);
    UHUB1_CTRL &= (uint8_t)~bUH_BUS_RESET;
    delay_us(250);
    UIF_DETECT = 0;
}

static uint8_t port_enable(void)
{
    if (!(USB_HUB_ST & bUHS_H1_ATTACH))
        return 0;
    if (USB_HUB_ST & bUHS_HM_LEVEL) {            /* D- high at attach: low speed */
        UHUB1_CTRL |= bUH_LOW_SPEED;
        USB_CTRL |= bUC_LOW_SPEED;
    } else {
        UHUB1_CTRL &= (uint8_t)~bUH_LOW_SPEED;
        USB_CTRL &= (uint8_t)~bUC_LOW_SPEED;
    }
    UHUB1_CTRL |= bUH_PORT_EN;
    return 1;
}

static void fail(uint8_t code)
{
    link_send(MSG_DEV_DETACH, &code, 1);
    port_disable();
    if (enum_failures < 255) enum_failures++;
    state = ST_FAILED;
}

/* Walk the configuration descriptor: HID interfaces (alternate setting 0) with their HID
 * descriptor (report descriptor length) and interrupt endpoints. */
static void parse_config(uint16_t total)
{
    uint16_t p = 0;
    iface_t __xdata *cur = 0;
    n_ifaces = 0;
    cfg_value = cfg_buf[5];
    remote_wakeup = (cfg_buf[7] & 0x20) != 0;
    while (p + 2 <= total) {
        uint8_t len = cfg_buf[p], type = cfg_buf[p + 1];
        if (len < 2 || p + len > total)
            break;
        if (type == 0x04 && len >= 9) {                         /* interface */
            cur = 0;
            if (cfg_buf[p + 5] == 0x03 && cfg_buf[p + 3] == 0 && n_ifaces < MAX_IFACES) {
                cur = &ifaces[n_ifaces++];
                memset(cur, 0, sizeof(iface_t));
                cur->num = cfg_buf[p + 2];
                cur->subclass = cfg_buf[p + 6];
                cur->protocol = cfg_buf[p + 7];
            }
        } else if (type == 0x21 && len >= 9 && cur) {           /* HID descriptor */
            if (cfg_buf[p + 6] == 0x22)
                cur->desc_len = cfg_buf[p + 7] | ((uint16_t)cfg_buf[p + 8] << 8);
        } else if (type == 0x05 && len >= 7 && cur) {           /* endpoint */
            if ((cfg_buf[p + 3] & 0x03) == 0x03) {              /* interrupt */
                uint8_t addr = cfg_buf[p + 2], size = cfg_buf[p + 4];
                if (size > 64) size = 64;
                if (addr & 0x80) {
                    cur->ep_in = addr & 0x0F;
                    cur->ep_in_size = size;
                    cur->interval = cfg_buf[p + 6] ? cfg_buf[p + 6] : 1;
                } else {
                    cur->ep_out = addr & 0x0F;
                    cur->ep_out_size = size;
                }
            }
        }
        p += len;
    }
}

static void enumerate(void)
{
    uint8_t i, r;
    uint16_t total;
    static __xdata uint8_t msg[10];

    port_reset();
    if (!port_enable()) { state = ST_IDLE; return; }
    delay_ms(20);

    ep0_size = 8;
    make_setup(0x80, 0x06, 0x0100, 0, 8);                       /* device descriptor, first 8 */
    if (host_ctrl(setup_buf, cfg_buf, 8, 0xFF) != R_OK) { fail(ENUM_ERR_DEV_DESC); return; }
    ep0_size = cfg_buf[7] ? cfg_buf[7] : 8;
    if (ep0_size > 64) ep0_size = 64;

    make_setup(0x00, 0x05, 1, 0, 0);                            /* SET_ADDRESS 1 */
    if (host_ctrl(setup_buf, 0, 0, 0xFF) != R_OK) { fail(ENUM_ERR_ADDRESS); return; }
    USB_DEV_AD = (USB_DEV_AD & bUDA_GP_BIT) | 1;
    delay_ms(5);

    make_setup(0x80, 0x06, 0x0100, 0, 18);
    if (host_ctrl(setup_buf, cfg_buf, 18, 0xFF) != R_OK) { fail(ENUM_ERR_DEV_DESC); return; }
    msg[0] = cfg_buf[8];  msg[1] = cfg_buf[9];                  /* VID */
    msg[2] = cfg_buf[10]; msg[3] = cfg_buf[11];                 /* PID */
    msg[4] = cfg_buf[12]; msg[5] = cfg_buf[13];                 /* bcdDevice */
    msg[6] = (USB_CTRL & bUC_LOW_SPEED) ? 0 : 1;

    make_setup(0x80, 0x06, 0x0200, 0, 9);                       /* configuration, header */
    if (host_ctrl(setup_buf, cfg_buf, 9, 0xFF) != R_OK) { fail(ENUM_ERR_CFG_DESC); return; }
    total = cfg_buf[2] | ((uint16_t)cfg_buf[3] << 8);
    if (total > CFG_BUF_LEN) total = CFG_BUF_LEN;
    make_setup(0x80, 0x06, 0x0200, 0, total);
    if (host_ctrl(setup_buf, cfg_buf, CFG_BUF_LEN, 0xFF) != R_OK) { fail(ENUM_ERR_CFG_DESC); return; }
    parse_config(ctrl_len);
    if (n_ifaces == 0) { fail(ENUM_ERR_NO_HID); return; }

    make_setup(0x00, 0x09, cfg_value, 0, 0);                    /* SET_CONFIGURATION */
    if (host_ctrl(setup_buf, 0, 0, 0xFF) != R_OK) { fail(ENUM_ERR_SET_CFG); return; }
    if (remote_wakeup) {                                        /* SET_FEATURE(DEVICE_REMOTE_WAKEUP) */
        make_setup(0x00, 0x03, 1, 0, 0);
        if (host_ctrl(setup_buf, 0, 0, 0xFF) != R_OK) remote_wakeup = 0;
    }

    msg[7] = n_ifaces;
    link_send(MSG_DEV_ATTACH, msg, 8);

    for (i = 0; i < n_ifaces; i++) {
        iface_t __xdata *f = &ifaces[i];
        make_setup(0x21, 0x0A, 0, f->num, 0);                   /* SET_IDLE 0 (a STALL is fine) */
        host_ctrl(setup_buf, 0, 0, 0xFF);

        msg[0] = f->num;  msg[1] = f->subclass;  msg[2] = f->protocol;
        msg[3] = f->ep_in; msg[4] = f->ep_in_size; msg[5] = f->interval;
        msg[6] = f->ep_out; msg[7] = f->ep_out_size;
        msg[8] = (uint8_t)f->desc_len; msg[9] = (uint8_t)(f->desc_len >> 8);
        link_send(MSG_IFACE, msg, 10);

        if (f->desc_len) {
            make_setup(0x81, 0x06, 0x2200, f->num, f->desc_len);   /* HID report descriptor */
            r = host_ctrl(setup_buf, 0, 0, f->num);
            if (r != R_OK) { fail(ENUM_ERR_REPORT_DESC); return; }
        }
        f->tog_in = 0;
        f->tog_out = 0;
        f->last_poll = millis();
    }
    link_send(MSG_ENUM_DONE, 0, 0);
    state = ST_RUNNING;
    in_errors = 0;
    enum_failures = 0;
    suspended = 0;
    full_speed = (USB_CTRL & bUC_LOW_SPEED) == 0;
}

/* ---- runtime ----------------------------------------------------------------------------- */
static void poll_inputs(void)
{
    uint8_t i, r;
    uint16_t now = millis();
    for (i = 0; i < n_ifaces; i++) {
        iface_t __xdata *f = &ifaces[i];
        if (!f->ep_in || (uint16_t)(now - f->last_poll) < f->interval)
            continue;
        f->last_poll = now;
        r = host_transfer(PID(USB_PID_IN, f->ep_in), f->tog_in, 0);
        if (r == R_OK) {
            f->tog_in ^= 1;
            in_errors = 0;
            link_send2(MSG_REPORT, &f->num, 1, host_rx, USB_RX_LEN);
        } else if (r == R_NAK) {
            in_errors = 0;
        } else if (r == R_STALL) {
            f->ep_in = 0;                        /* give up on this endpoint */
        } else if (in_errors < 0xFFFF) {
            in_errors++;
        }
    }
}

static iface_t __xdata *find_iface(uint8_t num)
{
    uint8_t i;
    for (i = 0; i < n_ifaces; i++)
        if (ifaces[i].num == num)
            return &ifaces[i];
    return 0;
}

/* MSG_OUT_REPORT: iface, report_type, report_id, data[] */
void usbhost_out_report(const uint8_t __xdata *msg, uint8_t len)
{
    static __xdata uint8_t done[2];
    uint8_t r = R_ERR, n, pos = 0;
    iface_t __xdata *f;

    done[0] = msg[0];
    usbhost_resume();                            /* host output (LEDs, Vial) wakes the bus */
    if (state != ST_RUNNING || len < 3 || !(f = find_iface(msg[0]))) {
        done[1] = R_ERR;
        link_send(MSG_OUT_DONE, done, 2);
        return;
    }
    n = len - 3;
    if (msg[2]) cfg_buf[pos++] = msg[2];         /* report id first when the device uses ids */
    memcpy(cfg_buf + pos, msg + 3, n);
    n += pos;

    if (msg[1] == 2 && f->ep_out && n <= f->ep_out_size) {
        memcpy(host_tx, cfg_buf, n);
        UH_TX_LEN = n;
        r = host_transfer(PID(USB_PID_OUT, f->ep_out), f->tog_out, 50);
        if (r == R_OK) f->tog_out ^= 1;
    } else {
        make_setup(0x21, 0x09, ((uint16_t)msg[1] << 8) | msg[2], f->num, n);   /* SET_REPORT */
        r = host_ctrl(setup_buf, cfg_buf, n, 0xFF);
    }
    done[1] = r;
    link_send(MSG_OUT_DONE, done, 2);
}

static void drop_device(void)
{
    uint8_t code = ENUM_OK;
    if (state == ST_RUNNING)
        link_send(MSG_DEV_DETACH, &code, 1);   /* the nRF releases every key it holds */
    port_disable();
    UH_SETUP |= bUH_SOF_EN;
    suspended = 0;
    state = ST_IDLE;
}

void usbhost_re_enumerate(void)
{
    drop_device();
}

/* ---- suspend / resume ------------------------------------------------------------------ */
void usbhost_suspend(void)
{
    if (state != ST_RUNNING || suspended)
        return;
    UH_SETUP &= (uint8_t)~bUH_SOF_EN;            /* idle bus: the device suspends after 3 ms */
    suspended = 1;
}

void usbhost_resume(void)
{
    if (!suspended)
        return;
    /* Restart SOFs: devices treat the end of the idle bus as a resume. One that stays silent is
     * caught by the error check below and enumerated again. CHECK ON HARDWARE (QMK keyboards). */
    UH_SETUP |= bUH_SOF_EN;
    delay_ms(10);
    suspended = 0;
    resumed_ms = millis();
    in_errors = 0;
}

/* Remote wakeup: the device drives the bus to K (full speed: D- high, low speed: D+ high). */
static uint8_t bus_in_k_state(void)
{
    uint8_t st = USB_HUB_ST;
    return full_speed ? (st & bUHS_HM_PIN) != 0 : (st & bUHS_HP_PIN) != 0;
}

/* Power-down until a USB event (attach, detach, the device's remote wakeup) or RXD0 low (the
 * nRF's wake byte). Only when there is nothing to do: no device, or the bus suspended. Fsys and
 * with it the watchdog stop meanwhile. A remote wakeup that only woke the chip is caught by the
 * K-state check in usbhost_poll(): QMK keeps signalling while the key is held. */
uint8_t usbhost_sleep(void)
{
    uint8_t attached = (USB_HUB_ST & bUHS_H1_ATTACH) != 0;
    uint8_t int_en = USB_INT_EN, setup = UH_SETUP, woke_by_device = 0;
    if (!((state == ST_IDLE && !attached) || (state == ST_RUNNING && suspended)))
        return SLEEP_NOT;
    UH_SETUP = setup & (uint8_t)~bUH_SOF_EN;              /* no SOF: an idle bus, nothing to wake on */
    USB_INT_EN = int_en | bUIE_DETECT | bUIE_SUSPEND;     /* wake sources; IE_USB stays off */
    UIF_DETECT = 0;
    UIF_SUSPEND = 0;
    UIF_TRANSFER = 0;
    safe_mode_open();
    WAKE_CTRL = bWAK_BY_USB | bWAK_RXD0_LO;
    safe_mode_close();
    for (;;) {
        uint16_t t;
        wdog_feed();
        PCON |= PD;
        __asm nop __endasm;
        __asm nop __endasm;
        /* Stay awake only with a reason: data from the nRF (its frame follows the wake byte after
         * LINK_WAKE_MS), a device attached, or the device's remote wakeup. The first K is usually
         * over by the time the clock runs, but QMK repeats it every ~17 ms while the key is held:
         * watch 40 ms for one. USB events without K come often while the bus is suspended (each
         * would resume the keyboard): back to sleep without telling the nRF. */
        t = millis();
        while ((uint16_t)(millis() - t) < (state == ST_RUNNING ? 40 : 2 * LINK_WAKE_MS)) {
            if (uart0_rx_count() || (state == ST_IDLE && (USB_HUB_ST & bUHS_H1_ATTACH) != 0))
                goto awake;
            if (state == ST_RUNNING && bus_in_k_state()) {
                woke_by_device = 1;
                goto awake;
            }
        }
        UIF_DETECT = 0;
        UIF_SUSPEND = 0;
        UIF_TRANSFER = 0;
    }
awake:
    safe_mode_open();
    WAKE_CTRL = 0;
    safe_mode_close();
    USB_INT_EN = int_en;
    UH_SETUP = setup;
    wdog_feed();
    if (woke_by_device)
        usbhost_resume();
    return uart0_rx_count() ? SLEEP_WOKE_BY_NRF : SLEEP_WOKE_SELF;
}

void usbhost_status(uint8_t __xdata *m)
{
    m[0] = state == ST_RUNNING ? STATE_RUNNING : state == ST_FAILED ? STATE_FAILED :
           state == ST_ATTACHED ? STATE_ENUMERATING : STATE_IDLE;
    m[1] = suspended;
    m[2] = (uint8_t)in_errors;
    m[3] = (uint8_t)(in_errors >> 8);
    m[4] = enum_failures;
}

void usbhost_poll(void)
{
    uint8_t attached = (USB_HUB_ST & bUHS_H1_ATTACH) != 0;

    switch (state) {
    case ST_IDLE:
        if (attached) {
            state = ST_ATTACHED;
            state_ms = millis();
        }
        break;
    case ST_ATTACHED:                            /* debounce the attach (USB: 100 ms) */
        if (!attached)
            state = ST_IDLE;
        else if ((uint16_t)(millis() - state_ms) >= 100)
            enumerate();
        break;
    case ST_RUNNING:
        if (!attached) {
            drop_device();
        } else if (suspended) {
            if (bus_in_k_state())
                usbhost_resume();
        } else {
            poll_inputs();
            if (in_errors >= IN_ERROR_LIMIT ||
                (in_errors >= RESUME_ERROR_LIMIT && (uint16_t)(millis() - resumed_ms) < RESUME_WINDOW_MS))
                drop_device();                   /* attached but not answering: enumerate again */
        }
        break;
    case ST_FAILED:                              /* wait for an unplug before trying again */
        if (!attached)
            state = ST_IDLE;
        break;
    }
}
