#ifndef HUB_BLE_H
#define HUB_BLE_H


#ifdef __cplusplus
extern "C" {
#endif
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

#ifdef __cplusplus
}
#endif

#endif