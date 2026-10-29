/*
 * CH559 registers used by the KB Bridge firmware (SDCC).
 * Addresses and bit values from the WCH CH559 datasheet (CH559DS1). Only what this firmware uses.
 */
#ifndef CH559_REGS_H
#define CH559_REGS_H

#include <stdint.h>

#define SFR(name, addr)        __sfr __at(addr) name
#define SBIT(name, addr, bit)  __sbit __at((addr) + (bit)) name
#define XREG(name, addr)       volatile __xdata __at(addr) uint8_t name

/* ---- system ------------------------------------------------------------------------------ */
SFR(SAFE_MOD,   0xA1);          /* write 0x55, 0xAA to open safe mode; read = CHIP_ID */
SFR(GLOBAL_CFG, 0xB1);
#define bBOOT_LOAD   0x20
#define bSW_RESET    0x10
#define bWDOG_EN     0x01
SFR(PCON,       0x87);
#define SMOD         0x80
#define PD           0x02          /* power-down, cleared by the wake-up hardware */
SFR(SLEEP_CTRL, 0xEA);         /* write in safe mode: 1 = peripheral clock off */
#define bSLP_OFF_ADC   0x40
#define bSLP_OFF_UART1 0x20
#define bSLP_OFF_P1S1  0x10
#define bSLP_OFF_SPI0  0x08
#define bSLP_OFF_TMR3  0x04
#define bSLP_OFF_LED   0x02
SFR(WAKE_CTRL,  0xEB);         /* write in safe mode */
#define bWAK_BY_USB  0x80
#define bWAK_RXD0_LO 0x01
SFR(PLL_CFG,    0xB2);          /* [4:0] PLL multiple, [7:5] USB 4x divisor */
SFR(CLOCK_CFG,  0xB3);
#define bOSC_EN_INT  0x80
#define bOSC_EN_XT   0x40
#define MASK_SYS_CK_DIV 0x1F
SFR(WDOG_COUNT, 0xFF);

/* ---- interrupts -------------------------------------------------------------------------- */
SFR(IE,         0xA8);
SBIT(EA,        0xA8, 7);
SBIT(ES,        0xA8, 4);
SBIT(ET0,       0xA8, 1);
SFR(IP,         0xB8);
SFR(IE_EX,      0xE8);
SBIT(IE_USB,    0xE8, 2);
#define INT_NO_TMR0   1
#define INT_NO_UART0  4
#define INT_NO_USB    8

/* ---- ports ------------------------------------------------------------------------------- */
SFR(P0,         0x80);
SFR(P0_DIR,     0xC4);
SFR(P0_PU,      0xC5);
#define bTXD_        0x08          /* P0.3 */
#define bRXD_        0x04          /* P0.2 */
SFR(P3,         0xB0);
SBIT(P3_2,      0xB0, 2);          /* -> nRF RESET (P0.21) */
SBIT(P3_3,      0xB0, 3);          /* <- PAIR button (low = pressed) */
SFR(P3_DIR,     0xBE);
SFR(P3_PU,      0xBF);
SFR(P4_DIR,     0xC2);
SFR(P4_PU,      0xC3);
SFR(PORT_CFG,   0xC6);
#define bP3_OC       0x08
#define bP0_OC       0x01
SFR(PIN_FUNC,   0xCE);
#define bUART0_PIN_X 0x10          /* RXD0/TXD0 on P0.2/P0.3 */

/* ---- timers / UART0 ---------------------------------------------------------------------- */
SFR(TCON,       0x88);
SBIT(TR1,       0x88, 6);
SBIT(TF0,       0x88, 5);
SBIT(TR0,       0x88, 4);
SFR(TMOD,       0x89);
#define bT1_M1       0x20
#define MASK_T1_MOD  0x30
#define bT1_GATE     0x80
#define bT1_CT       0x40
#define bT0_M0       0x01
#define MASK_T0_MOD  0x0F
SFR(TL0,        0x8A);
SFR(TH0,        0x8C);
SFR(TH1,        0x8D);
SFR(T2MOD,      0xC9);
#define bTMR_CLK     0x80
#define bT1_CLK      0x20
#define bT0_CLK      0x10
SFR(SCON,       0x98);
SBIT(SM0,       0x98, 7);
SBIT(SM1,       0x98, 6);
SBIT(SM2,       0x98, 5);
SBIT(REN,       0x98, 4);
SBIT(TI,        0x98, 1);
SBIT(RI,        0x98, 0);
SFR(SBUF,       0x99);

/* ---- USB ----------------------------------------------------------------------------------- */
SFR(USB_RX_LEN, 0xD1);
SFR(UEP1_CTRL,  0xD2);
SFR(UEP1_T_LEN, 0xD3);
SFR(UEP2_CTRL,  0xD4);
SFR(UEP2_T_LEN, 0xD5);
SFR(UEP3_CTRL,  0xD6);
SFR(UEP3_T_LEN, 0xD7);
SFR(USB_INT_FG, 0xD8);
SBIT(U_IS_NAK,    0xD8, 7);
SBIT(U_TOG_OK,    0xD8, 6);
SBIT(UIF_SUSPEND, 0xD8, 2);
SBIT(UIF_TRANSFER,0xD8, 1);
SBIT(UIF_DETECT,  0xD8, 0);
SBIT(UIF_BUS_RST, 0xD8, 0);
SFR(USB_INT_ST, 0xD9);
#define MASK_UIS_TOKEN  0x30
#define MASK_UIS_ENDP   0x0F
#define MASK_UIS_H_RES  0x0F
#define UIS_TOKEN_OUT   0x00
#define UIS_TOKEN_IN    0x20
#define UIS_TOKEN_SETUP 0x30
SFR(USB_MIS_ST, 0xDA);
#define bUMS_SUSPEND    0x04
SFR(USB_HUB_ST, 0xDB);
#define bUHS_H1_ATTACH  0x80
#define bUHS_HM_LEVEL   0x40
#define bUHS_HP_PIN     0x20
#define bUHS_HM_PIN     0x10
SFR(UEP0_CTRL,  0xDC);
SFR(UEP0_T_LEN, 0xDD);
SFR(USB_INT_EN, 0xE1);
#define bUIE_SUSPEND    0x04
#define bUIE_TRANSFER   0x02
#define bUIE_DETECT     0x01
#define bUIE_BUS_RST    0x01
SFR(USB_CTRL,   0xE2);
#define bUC_HOST_MODE   0x80
#define bUC_LOW_SPEED   0x40
#define bUC_DEV_PU_EN   0x20
#define bUC_INT_BUSY    0x08
#define bUC_RESET_SIE   0x04
#define bUC_CLR_ALL     0x02
#define bUC_DMA_EN      0x01
SFR(USB_DEV_AD, 0xE3);
#define bUDA_GP_BIT     0x80
SFR(UDEV_CTRL,  0xE4);             /* = UHUB0_CTRL in host mode */
#define UHUB0_CTRL      UDEV_CTRL
#define bUD_DP_PD_DIS   0x20
#define bUD_DM_PD_DIS   0x10
#define bUD_LOW_SPEED   0x04
#define bUD_PORT_EN     0x01
SFR(UHUB1_CTRL, 0xE5);
#define bUH1_DISABLE    0x80
#define bUH_LOW_SPEED   0x04
#define bUH_BUS_RESET   0x02
#define bUH_PORT_EN     0x01

/* device-mode endpoint control bits (UEPn_CTRL) */
#define bUEP_R_TOG      0x80
#define bUEP_T_TOG      0x40
#define bUEP_AUTO_TOG   0x10
#define MASK_UEP_R_RES  0x0C
#define UEP_R_RES_ACK   0x00
#define UEP_R_RES_NAK   0x08
#define UEP_R_RES_STALL 0x0C
#define MASK_UEP_T_RES  0x03
#define UEP_T_RES_ACK   0x00
#define UEP_T_RES_NAK   0x02
#define UEP_T_RES_STALL 0x03

/* host-mode aliases of the endpoint registers */
#define UH_SETUP        UEP1_CTRL
#define bUH_PRE_PID_EN  0x80
#define bUH_SOF_EN      0x40
#define UH_RX_CTRL      UEP2_CTRL
#define bUH_R_TOG       0x80
#define bUH_R_AUTO_TOG  0x10
#define UH_EP_PID       UEP2_T_LEN
#define UH_TX_CTRL      UEP3_CTRL
#define bUH_T_TOG       0x40
#define bUH_T_AUTO_TOG  0x10
#define UH_TX_LEN       UEP3_T_LEN

/* xSFR (xdata) */
XREG(UEP4_1_MOD,  0x2446);
#define bUEP1_TX_EN     0x40
XREG(UEP2_3_MOD,  0x2447);
#define bUEP2_RX_EN     0x08
#define bUEP2_TX_EN     0x04
#define UH_EP_MOD       UEP2_3_MOD
#define bUH_EP_TX_EN    0x40
#define bUH_EP_RX_EN    0x08
XREG(UEP0_DMA_H,  0x2448);
XREG(UEP0_DMA_L,  0x2449);
XREG(UEP1_DMA_H,  0x244A);
XREG(UEP1_DMA_L,  0x244B);
XREG(UEP2_DMA_H,  0x244C);
XREG(UEP2_DMA_L,  0x244D);
XREG(UEP3_DMA_H,  0x244E);
XREG(UEP3_DMA_L,  0x244F);
#define UH_RX_DMA_H     UEP2_DMA_H
#define UH_RX_DMA_L     UEP2_DMA_L
#define UH_TX_DMA_H     UEP3_DMA_H
#define UH_TX_DMA_L     UEP3_DMA_L

/* ---- USB protocol constants ------------------------------------------------------------ */
#define USB_PID_OUT     0x01
#define USB_PID_ACK     0x02
#define USB_PID_DATA0   0x03
#define USB_PID_IN      0x09
#define USB_PID_NAK     0x0A
#define USB_PID_DATA1   0x0B
#define USB_PID_SETUP   0x0D
#define USB_PID_STALL   0x0E

static inline void safe_mode_open(void) { SAFE_MOD = 0x55; SAFE_MOD = 0xAA; }
static inline void safe_mode_close(void) { SAFE_MOD = 0x00; }

#endif
