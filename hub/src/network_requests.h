#ifndef HUB_NETWORK_REQUESTS_H
#define HUB_NETWORK_REQUESTS_H

#include "network.h"

class NetworkRequests
{
private:
  Network* network;

public:

  /**
   * @brief Initialize this class with a ptr to network
   * @param network the ptr which will be used for direct network requests
   */
  void init(Network* network);

  /**
   * @brief Callback for when a phone is trying to register this hub
  * @param user_id the user id sent from the phone during connection
  * @param out_hub_id a pointer to the location to store the hub_id on success
  * @return 0 on success, -1 if failed to send
  */
  int handle_get_token_and_hub_id(char* user_id, uint16_t* out_hub_id);
  /**
   * @brief Callback for when an event should be sent
   * @param sensor_addr MAC address of sensor responsible for event
   * @return 0 on success, -1 if failed to send
   */
  int handle_send_event(char* sensor_addr);

  /**
   * @brief Callback for adding new sensor
   * @return 0 on success, -1 if failed to send
   */
  int handle_add_new_sensor(char* sensor_addr);

  /**
   * @brief Callback for notifying the server of the current battery level
   * @param real_mV the millivolts of the battery (double the measured millivolts)
   * @param percent the estimated percentage remaining (0 - 100)
   * @return 0 on success, -1 if failed to send
   */
  int handle_update_battery_level(int real_mV, uint8_t percent);

  /**
   * @brief Callback for notifying the server of the current gps location data
   * @param lat Latitude as 45.1234
   * @param lng Longitude as 105.1234
   * @param hdop Horizontal Dilution of Precision as [0,99.9]
   * @param speed KM/hour as [0,999.99]
   * @param course Course Over Ground as [0,360.00]
   * @return 0 on success, -1 if failed to send
   */
  int handle_update_gps_loc(float lat, float lng, float hdop, float speed, float course);
};

#endif
