#ifndef HUB_ALARM_H
#define HUB_ALARM_H

#ifdef __cplusplus
extern "C" {
#endif

  // If the ble button is currently pressed
  extern bool was_pressed;

  /**
   * @brief Setup button GPIO configuration and initialize alarms
   * @param button_hold_cb Pointer to callback which will be called when button
   * has been held long enough to start advertising
   * @param adv_timeout_cb Pointer to callback which will be called when advertising
   * exceeds timeout duration
   * @param diagnostic_trigger_cb Pointer to callback which will be called when diagnostic
   * mode is triggered (button tapped 4 times)
   * @return 0 on success, other numbers on error
   */
  int alarm_init(int (*button_hold_cb)(void), int (*adv_timeout_cb)(void), int (*diagnostic_trigger_cb)(void));

  /**
   * @brief Starts the advertising counter, callback set in alarm_init will be called
   * if advertising exceeds duration
   */
  void alarm_adv_counter_set(void);

  /**
   * @brief Manually cancel the advertising counter
   */
  void alarm_adv_counter_cancel(void);

#ifdef __cplusplus
}
#endif

#endif