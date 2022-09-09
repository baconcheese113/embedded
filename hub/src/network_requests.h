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
};

#endif
