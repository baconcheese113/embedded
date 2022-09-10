#ifndef HUB_BATTERY_H
#define HUB_BATTERY_H

#include <stdint.h>
#include "network_requests.h"


#ifdef __cplusplus
extern "C" {
#endif

  struct batt_reading_t {
    // raw the raw reading from the pin 0 - 1023
    int raw;
    // halved_mV conversion from raw to millivolts
    int halved_mV;
    // real_mV the estimated actual mV of the battery, based on batt config
    int real_mV;
    // percent the remaining percentage of battery, based on batt config
    uint8_t percent;
  };

  /**
   * @return Whether we meet the criteria at this moment to send a battery update
   */
  bool battery_should_send_update(void);

  /**
   * @brief Blocks while performing a read and then sending the battery level
   * @return 0 on success
   */
  int battery_update(void);

  /**
   * @brief Initialize the ADC. Needed before reading voltage
   * @param network_requests ptr to the NetworkRequest instance for updating the bat level
   * @return 0 on success
   */
  int battery_init(NetworkRequests* network_requests);

  /**
   * @brief Perform a blocking read of ADC2 (pin 4) and get results
   * @return batt_reading_t struct containing mV readings and percent remaining
   */
  struct batt_reading_t battery_read(void);

#ifdef __cplusplus
}
#endif

#endif
