/* KB Bridge CH559: clock, millisecond tick, delays, watchdog. */
#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include "ch559_regs.h"

#define FSYS_12MHZ 12000000UL      /* host mode: low power, UART link 250 kbaud exact */
#define FSYS_48MHZ 48000000UL      /* update mode (CDC bridge): 115200 baud within 0.2 % */

extern uint32_t fsys;              /* current system clock */

void clock_init(uint32_t hz);      /* 12 MHz crystal -> PLL 288 MHz -> USB 48 MHz, Fsys = hz (12 / 48 MHz) */
void tick_init(void);              /* timer0, 1 ms interrupt (uses fsys) */
uint16_t millis(void);             /* wraps every 65.5 s: compare with (uint16_t)(a - b) */
void delay_ms(uint16_t ms);        /* feeds the watchdog */
void delay_us(uint16_t us);        /* approximate, for short USB timings */

/* Watchdog: WDOG_COUNT counts Fsys / 262144 and resets the chip on overflow (256 counts):
 * 5.6 s at 12 MHz, 1.4 s at 48 MHz. wdog_feed() restarts the count. */
void wdog_enable(void);
#define wdog_feed() (WDOG_COUNT = 0)

/* Board revision (Makefile: BOARD_REV=6|7). v0.7: P3.2 drives an N-FET that pulls the nRF RESET
 * low (push-pull, HIGH = reset); v0.6: P3.2 is wired to RESET (quasi-bidirectional, LOW = reset). */
#ifndef BOARD_REV
#define BOARD_REV 6
#endif
#if BOARD_REV >= 7
#define NRF_RESET_IDLE 0
#else
#define NRF_RESET_IDLE 1
#endif

void timer0_isr(void) __interrupt(INT_NO_TMR0);

#endif
