/*
 * KB Bridge: UART link between the CH559 (USB host) and the nRF52832 (BLE).
 * Shared by both firmwares: the CH559 build (SDCC, C) includes this file from here.
 *
 * Frame: SOF, TYPE, LEN, PAYLOAD[LEN], CRC8
 *   SOF  = 0x7E
 *   LEN  = payload length, 0..LINK_MAX_PAYLOAD
 *   CRC8 = polynomial 0x07, init 0x00, over TYPE, LEN and PAYLOAD
 * A receiver that sees a bad CRC or a gap longer than LINK_BYTE_TIMEOUT_MS inside a frame drops it
 * and hunts for the next SOF. Nothing is acknowledged: the sender does not retry (key reports are
 * state, the next one repairs a loss; Vial retries at its own level).
 *
 * Multi-byte fields are little-endian.
 */
#ifndef KB_BRIDGE_LINK_PROTO_H
#define KB_BRIDGE_LINK_PROTO_H

#define LINK_SOF              0x7E
#define LINK_MAX_PAYLOAD      72
#define LINK_BAUD             250000UL    /* CH559 at 12 MHz: 12 MHz / 16 / 3, exact; nRF: standard rate */
#define LINK_BYTE_TIMEOUT_MS  5

#define LINK_PROTO_VERSION    3

/* ---- CH559 -> nRF ------------------------------------------------------------------------ */
/* HELLO: sent once after every CH559 reset in host mode (a HELLO while running = the CH559 restarted).
 *   u8 proto_version, u8 fw_major, u8 fw_minor, u32 build_id */
#define MSG_HELLO             0x01
/* DEV_ATTACH: a device finished the standard enumeration steps.
 *   u16 vid, u16 pid, u16 bcd_device, u8 speed (0 low, 1 full), u8 num_hid_ifaces */
#define MSG_DEV_ATTACH        0x02
/* IFACE: one HID interface.
 *   u8 iface, u8 subclass, u8 protocol, u8 ep_in, u8 ep_in_size, u8 interval_ms,
 *   u8 ep_out (0 = none), u8 ep_out_size, u16 report_desc_len */
#define MSG_IFACE             0x03
/* DESC: a piece of an interface's HID report descriptor.
 *   u8 iface, u16 offset, u8 data[] (up to 64 bytes) */
#define MSG_DESC              0x04
/* ENUM_DONE: all IFACE/DESC messages are sent, IN polling starts. No payload. */
#define MSG_ENUM_DONE         0x05
/* DEV_DETACH: the device is gone (or enumeration failed: u8 error code). */
#define MSG_DEV_DETACH        0x06
/* REPORT: data from an interrupt IN endpoint.
 *   u8 iface, u8 data[] (up to 64 bytes, exactly what the device sent) */
#define MSG_REPORT            0x10
/* OUT_DONE: result of MSG_OUT_REPORT.  u8 iface, u8 status (0 = ok) */
#define MSG_OUT_DONE          0x11
/* STATUS: answer to PING (heartbeat), also sent after waking from SLEEP.
 *   u8 state (STATE_*), u8 suspended (0/1), u16 consecutive IN errors, u8 enumeration failures */
#define MSG_STATUS            0x07

/* ---- nRF -> CH559 ------------------------------------------------------------------------ */
/* PING: CH559 answers with STATUS. The nRF pings every second and resets a silent CH559. */
#define MSG_PING              0x80
/* OUT_REPORT: send a report to the device.
 *   u8 iface, u8 report_type (2 output, 3 feature), u8 report_id, u8 data[] (without the id)
 * Output reports go to the interface's interrupt OUT endpoint when it has one (report id
 * prepended when non-zero), else as a SET_REPORT control request. */
#define MSG_OUT_REPORT        0x81
/* SUSPEND / RESUME the USB bus (keyboard idle power saving). No payload.
 * SUSPEND: stop polling and SOF (the device suspends after 3 ms of idle bus); remote wakeup was
 * enabled at enumeration when the device supports it. The CH559 resumes by itself when the device
 * signals a wakeup (bus leaves the idle J state) or on RESUME; if the device does not answer after
 * the resume it is reset and enumerated again. */
#define MSG_USB_SUSPEND       0x82
#define MSG_USB_RESUME        0x83
/* RE_ENUMERATE: drop the device and enumerate it again. No payload. */
#define MSG_RE_ENUMERATE      0x84
/* ENTER_BOOTLOADER: jump to the factory bootloader (0xF400). The nRF normally uses the
 * hardware path instead (XI low + RST pulse), which also works with a broken application. */
#define MSG_ENTER_BOOTLOADER  0x85
/* SLEEP: power down while there is nothing to do: no device (the nRF keeps VBUS_KB off then), or
 * the bus suspended. Ignored otherwise. Wakes on a USB event (attach, detach, remote wakeup) or
 * on RXD low: the nRF sends one 0x00 byte and waits LINK_WAKE_MS before its next frame. Answers
 * with STATUS, at once when it does not sleep, else after waking. The nRF switches its UART off
 * meanwhile: when the CH559 wakes by itself it first sends one 0x00 byte and waits
 * 2 x LINK_WAKE_MS. No payload. */
#define MSG_SLEEP             0x86
#define LINK_WAKE_MS          5

/* DEV_DETACH error codes */
#define ENUM_OK               0x00
#define ENUM_ERR_DEV_DESC     0x01
#define ENUM_ERR_ADDRESS      0x02
#define ENUM_ERR_CFG_DESC     0x03
#define ENUM_ERR_SET_CFG      0x04
#define ENUM_ERR_REPORT_DESC  0x05
#define ENUM_ERR_NO_HID       0x06

/* STATUS states */
#define STATE_IDLE            0           /* nothing attached */
#define STATE_ENUMERATING     1
#define STATE_RUNNING         2
#define STATE_FAILED          3           /* enumeration failed: waits for a re-plug (or a VBUS cycle) */

#endif
