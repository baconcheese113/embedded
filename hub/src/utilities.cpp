#include <zephyr.h>
#include <device.h>
#include <drivers/pwm.h>
#include <math.h>
#include <logging/log.h>
LOG_MODULE_REGISTER(utilities, CONFIG_LOG_DEFAULT_LEVEL);

#include "utilities.h"

#define STEP_SIZE PWM_MSEC(20U) / 256

namespace Utilities {

  static const struct pwm_dt_spec red_pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(red_pwm_led));
  static const struct pwm_dt_spec green_pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(green_pwm_led));
  static const struct pwm_dt_spec blue_pwm_led = PWM_DT_SPEC_GET(DT_NODELABEL(blue_pwm_led));

  int setup_pins(void) {
    if (!device_is_ready(red_pwm_led.dev) ||
      !device_is_ready(green_pwm_led.dev) ||
      !device_is_ready(blue_pwm_led.dev)) {
      LOG_ERR("Error: one or more PWM devices not ready\n");
      return -1;
    }
    return 0;
  }

  void rgb_write(uint8_t r, uint8_t g, uint8_t b, bool print) {
    if (print) {
      LOG_DBG("Writing rgb value: %u, %u, %u", r, g, b);
    }
    uint8_t divisor = 5;
    pwm_set_pulse_dt(&red_pwm_led, r * STEP_SIZE / divisor);
    pwm_set_pulse_dt(&green_pwm_led, g * STEP_SIZE / divisor);
    pwm_set_pulse_dt(&blue_pwm_led, b * STEP_SIZE / divisor);
  }

  void happy_dance(void) {
    uint8_t red, green, blue;
    for (uint16_t angle = 0; angle < 360; angle++) {
      if (angle < 60) {
        red = 255; green = round(angle * 4.25 - 0.01); blue = 0;
      } else if (angle < 120) {
        red = round((120 - angle) * 4.25 - 0.01); green = 255; blue = 0;
      } else if (angle < 180) {
        red = 0, green = 255; blue = round((angle - 120) * 4.25 - 0.01);
      } else if (angle < 240) {
        red = 0, green = round((240 - angle) * 4.25 - 0.01); blue = 255;
      } else if (angle < 300) {
        red = round((angle - 240) * 4.25 - 0.01), green = 0; blue = 255;
      } else {
        red = 255, green = 0; blue = round((360 - angle) * 4.25 - 0.01);
      }
      rgb_write(red, green, blue, false);
      k_msleep(2);
    }
    rgb_write(0, 0, 0, false);
  }

  Command parse_raw_command(char* raw_cmd) {
    LOG_DBG("in parse_raw_cmd");
    Command res;
    int8_t value_start_idx = -1;
    for (uint8_t i = 0; i < strlen(raw_cmd); i++) {
      if (raw_cmd[i] == ':' && value_start_idx < 0) { // if delimeter
        value_start_idx = i + 1;
      } else if (value_start_idx >= 0) { // if parsing value (after delimeter)
        res.value[i - value_start_idx] = raw_cmd[i];
      } else { // if parsing type (before delimeter)
        res.type[i] = raw_cmd[i];
      }
    }
    if (!strlen(res.type)) LOG_WRN("Error: Couldn't parse type");
    if (!strlen(res.value)) LOG_WRN("Error: Couldn't parse value");
    return res;
  }

}