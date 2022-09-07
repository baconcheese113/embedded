#ifndef HUB_BLE_H
#define HUB_BLE_H


#ifdef __cplusplus
extern "C" {
#endif

  // 10 available address slots
  extern char* known_sensor_addrs[10];
  extern uint8_t known_sensor_addrs_len;

  // Enables Bluetooth, must be called before any other ble functions
  int init_ble(void);

  /** Starts advertising
   * @return 0 on success
   */
  int advertise_start(void);

  /** Stops advertising
   * @return 0 on success
   */
  int advertise_stop(void);

  void start_scan(void);

  /**
   * @brief Add a single sensor address to the known_sensor_addrs array
   */
  void add_known_sensor(char* addr);

#ifdef __cplusplus
}
#endif

#endif