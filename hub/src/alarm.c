
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/sys/printk.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>

#include "alarm.h"

#define HOLD_DELAY 3 * 1000 * 1000
#define ADV_TIMEOUT 1 * 1000 * 1000
#define BUTTON_HOLD_CHANNEL_ID 0
#define ADV_DURATION_CHANNEL_ID 1

static const struct device* counter_dev = DEVICE_DT_GET(DT_NODELABEL(rtc2));

static struct counter_alarm_cfg button_hold_cfg;
static struct counter_alarm_cfg adv_timeout_cfg;

static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static struct gpio_callback button_cb_data;

static bool adv_alarm_running = false;
bool was_pressed = false;

typedef int (*callback_ptr_t)(void);
struct work_info {
  struct k_work work;
  callback_ptr_t callback;
} cb_work;

static callback_ptr_t default_cb = NULL;
static callback_ptr_t* multi_tap_cb = &default_cb;

static void cb_work_fn(struct k_work* work_item) {
  struct work_info* button_struct = CONTAINER_OF(work_item, struct work_info, work);
  button_struct->callback();
}

void alarm_adv_counter_set(void) {
  counter_start(counter_dev);
  adv_alarm_running = true;
  counter_set_channel_alarm(counter_dev, ADV_DURATION_CHANNEL_ID, &adv_timeout_cfg);
}

void alarm_adv_counter_cancel(void) {
  counter_stop(counter_dev);
  adv_alarm_running = false;
  counter_cancel_channel_alarm(counter_dev, ADV_DURATION_CHANNEL_ID);
}

static uint8_t diagnostic_trigger_count = 0;
static int64_t last_trigger_time = 0;
static void register_press(void) {
  int64_t ms = k_uptime_get();
  if (ms - last_trigger_time < 500) {
    diagnostic_trigger_count++;
  } else {
    diagnostic_trigger_count = 1;
  }
  last_trigger_time = ms;
  if (diagnostic_trigger_count == 4) {
    printk("\n**Diagnostic mode triggered**\n\n");
    diagnostic_trigger_count = 0;
    (*multi_tap_cb)();
  }
}

static void button_pressed(const struct device* port, struct gpio_callback* cb, gpio_port_pins_t pins) {
  int is_pressed_now = gpio_pin_get_dt(&button);
  uint32_t ticks;
  counter_get_value(counter_dev, &ticks);
  uint32_t us = counter_ticks_to_us(counter_dev, ticks);
  printk("[%us] Button %s\n", us / 1000 / 1000, is_pressed_now ? "pressed" : "released");
  int ret = 0;

  if (is_pressed_now && !was_pressed) {
    ret |= counter_start(counter_dev);
    ret |= counter_set_channel_alarm(counter_dev, BUTTON_HOLD_CHANNEL_ID, &button_hold_cfg);
    if (ret != 0) printk("Error setting counter alarms\n");
  } else if (!is_pressed_now && was_pressed) {
    if (!adv_alarm_running) counter_stop(counter_dev);
    counter_cancel_channel_alarm(counter_dev, BUTTON_HOLD_CHANNEL_ID);
    // TODO reset counter somehow
  }
  was_pressed = is_pressed_now;

  if(!is_pressed_now) register_press();
}

// Internal helper callback that meets the callback function signature
// to simplify exposed interface
static void internal_adv_timeout_cb(const struct device* counter_dev,
  uint8_t chan_id, uint32_t ticks, void* user_data)
{
  callback_ptr_t adv_timeout_cb = user_data;
  cb_work.callback = adv_timeout_cb;
  k_work_submit(&cb_work.work);
}

// Internal helper callback that meets the callback function signature
// to simplify exposed interface
static void internal_button_hold_cb(const struct device* counter_dev,
  uint8_t chan_id, uint32_t ticks, void* user_data)
{
  callback_ptr_t button_hold_cb = user_data;
  // In an IRQ, so the heavy lifting here needs to be done on a work queue
  cb_work.callback = button_hold_cb;
  k_work_submit(&cb_work.work);
}

int alarm_init(int (*button_hold_cb)(void), int (*adv_timeout_cb)(void), int (*diagnostic_trigger_cb)(void)) {
  if (!device_is_ready(button.port)) {
    printk("Button not ready\n");
    return -1;
  }
  if(!device_is_ready(counter_dev)) {
    printk("Counter not ready\n");
    return -1;
  }

  int ret = 0;
  ret |= gpio_pin_configure_dt(&button, GPIO_INPUT);
  ret |= gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
  if (ret != 0) {
    printk("error configuring gpio_pins\n");
    return ret;
  }
  gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
  ret |= gpio_add_callback(button.port, &button_cb_data);
  if (ret != 0) {
    printk("error adding gpio callbacks\n");
    return ret;
  }
  printk("\tButton GPIO online\n");

  button_hold_cfg.flags = 0;
  button_hold_cfg.ticks = counter_us_to_ticks(counter_dev, HOLD_DELAY);
  button_hold_cfg.callback = internal_button_hold_cb;
  button_hold_cfg.user_data = button_hold_cb;

  adv_timeout_cfg.flags = 0;
  adv_timeout_cfg.ticks = counter_us_to_ticks(counter_dev, ADV_TIMEOUT);
  adv_timeout_cfg.callback = internal_adv_timeout_cb;
  adv_timeout_cfg.user_data = adv_timeout_cb;

  *multi_tap_cb = diagnostic_trigger_cb;

  k_work_init(&cb_work.work, cb_work_fn);

  printk("\tCounters using RTC2 online\n");

  printk("\t\t%d alarm channels available on RTC2\n", counter_get_num_of_channels(counter_dev));
  printk("\t\tRTC2 frequency: %dKHz\n", counter_get_frequency(counter_dev));

  return 0;
}