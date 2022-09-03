#include <zephyr.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include "utilities.h"

void main(void)
{
  LOG_INF("Booting...");
  LOG_INF("Board: %s", CONFIG_BOARD);
  Utilities.setup_pins();

  Utilities.happy_dance();
  Utilities.rgb_write(0, 0, 0, true);

  // TODO Start UART1 connection to SIM7000

  // TODO network.setPower(true);

  // TODO network.waitForConnection();


  while (1) {

    k_msleep(10);
  }

  // TODO get interrupt for pair button
}
