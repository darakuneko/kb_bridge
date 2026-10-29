// KB Bridge nRF: flash the CH559 through its factory serial bootloader (WCH ISP protocol over
// UART0 on P0.2/P0.3), entered from the running application (MSG_ENTER_BOOTLOADER).
#pragma once
#include <Arduino.h>

enum Ch559IspResult : uint8_t {
  ISP_OK = 0,
  ISP_NO_RESPONSE,     // bootloader did not answer IDENTIFY
  ISP_WRONG_CHIP,
  ISP_CONFIG,
  ISP_KEY,
  ISP_ERASE,
  ISP_PROGRAM,
  ISP_VERIFY,
};

// Resets the CH559 into its bootloader, erases, programs and verifies `image`, then restarts it.
// The link UART is reconfigured to 115200 during the update and must be re-opened afterwards.
Ch559IspResult ch559_isp_flash(const uint8_t *image, uint32_t len, bool app_running);

void ch559_reset();                          // RST pulse, application starts (XI released)


