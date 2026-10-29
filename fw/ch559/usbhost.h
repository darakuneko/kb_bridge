/* KB Bridge CH559: USB host on root port 1 (HP/HM = keyboard connector J2). */
#ifndef USBHOST_H
#define USBHOST_H

#include <stdint.h>

void usbhost_init(void);
void usbhost_poll(void);                         /* attach/detach, enumeration, IN polling */
void usbhost_out_report(const uint8_t __xdata *msg, uint8_t len);   /* MSG_OUT_REPORT payload */
void usbhost_re_enumerate(void);
void usbhost_suspend(void);
void usbhost_resume(void);
/* Power-down (see usbhost.c). SLEEP_WOKE_SELF: the nRF's UART is off, announce with a 0x00 byte. */
#define SLEEP_NOT          0
#define SLEEP_WOKE_BY_NRF  1
#define SLEEP_WOKE_SELF    2
uint8_t usbhost_sleep(void);
void usbhost_status(uint8_t __xdata *m);       /* 7 bytes: MSG_STATUS payload */

#endif
