#include <zephyr.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#include "utilities.h"
#include "network.h"

Network network;

void main(void)
{
  LOG_INF("Booting...");
  LOG_INF("Board: %s", CONFIG_BOARD);
  Utilities::setup_pins();

  Utilities::happy_dance();

  // TODO Start UART1 connection to SIM7000

  // TODO network.set_power(true);

  // TODO network.wait_for_power_on();


  network.initialize_access_token();
  while (1) {

    k_msleep(10);
  }

  // TODO get interrupt for pair button
}
