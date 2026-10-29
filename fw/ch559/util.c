/* KB Bridge CH559: clock, millisecond tick, delays, watchdog. */
#include "util.h"

uint32_t fsys = FSYS_12MHZ;
static volatile uint16_t tick_ms;
static uint16_t t0_reload;

void clock_init(uint32_t hz)
{
    /* Crystal first (the host needs better than the internal oscillator's +-1.5 %), then PLL:
     * Fpll = 12 MHz x 24 = 288 MHz, Fusb4x = 288 / 6 = 48 MHz (USB, independent of Fsys),
     * Fsys = 288 / 24 = 12 MHz or 288 / 6 = 48 MHz.
     * Safe mode lasts only 13-23 Fsys cycles: every protected write gets its own window and its
     * value is computed beforehand (a late write is silently ignored: a lost divider write leaves
     * update mode at 12 MHz, and the CDC bridge then runs at a quarter of its baud rate). */
    uint8_t cfg;
    safe_mode_open();
    CLOCK_CFG |= bOSC_EN_XT;
    safe_mode_close();
    delay_us(10000);                                    /* crystal start-up (still on the internal clock) */
    safe_mode_open();
    CLOCK_CFG &= (uint8_t)~bOSC_EN_INT;                 /* switch to the crystal */
    safe_mode_close();
    safe_mode_open();
    PLL_CFG = (uint8_t)((24 << 0) | (6 << 5));
    safe_mode_close();
    cfg = (uint8_t)((CLOCK_CFG & (uint8_t)~MASK_SYS_CK_DIV) | (hz == FSYS_48MHZ ? 6 : 24));
    safe_mode_open();
    CLOCK_CFG = cfg;
    safe_mode_close();
    fsys = hz;
}

/* timer0 at Fsys (bTMR_CLK | bT0_CLK), mode 1 (16 bit), reloaded in the ISR: Fsys / 1000 counts */
void tick_init(void)
{
    t0_reload = (uint16_t)(65536UL - fsys / 1000UL);
    T2MOD |= bTMR_CLK | bT0_CLK;
    TMOD = (TMOD & (uint8_t)~MASK_T0_MOD) | bT0_M0;
    TH0 = (uint8_t)(t0_reload >> 8);
    TL0 = (uint8_t)t0_reload;
    TF0 = 0;
    ET0 = 1;
    TR0 = 1;
}

void timer0_isr(void) __interrupt(INT_NO_TMR0)
{
    TR0 = 0;
    TH0 = (uint8_t)(t0_reload >> 8);
    TL0 = (uint8_t)t0_reload;
    TR0 = 1;
    tick_ms++;
}

uint16_t millis(void)
{
    uint16_t t;
    ET0 = 0;
    t = tick_ms;
    ET0 = 1;
    return t;
}

void delay_ms(uint16_t ms)
{
    uint16_t start = millis();
    while ((uint16_t)(millis() - start) < ms)
        wdog_feed();
}

/* About 1 us per iteration: ~12 cycles of loop overhead at 12 MHz, padded with NOPs at 48 MHz.
 * Not exact; only used where the USB spec gives generous minimums. */
void delay_us(uint16_t us)
{
    if (fsys == FSYS_48MHZ) {
        while (us--) {
            __asm
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
                nop
            __endasm;
        }
    } else {
        while (us--)
            ;
    }
}

void wdog_enable(void)
{
    WDOG_COUNT = 0;
    safe_mode_open();
    GLOBAL_CFG |= bWDOG_EN;
    safe_mode_close();
}
