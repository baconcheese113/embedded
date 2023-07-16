#include <zephyr/drivers/adc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>
#include <hal/nrf_saadc.h>

#include "battery.h"
#include "network_requests.h"

#define ADC_RESOLUTION        10
// Offset needed to align with multimeter
#define ADC_OFFSET            1
#define ADC_GAIN              ADC_GAIN_1_4
// Max pin reading millivolts - 0.6(ref) / 1/4(gain) = 2.4v
#define ADC_MAX_MV            2400
// With voltage dividing in half from a 4.2v(max) battery
#define BATT_MAX_MV           2100
#define BATT_MIN_MV           1600
static const int BATT_RANGE = BATT_MAX_MV - BATT_MIN_MV;
#define ADC_REFERENCE         ADC_REF_INTERNAL
#define ADC_ACQUISITION_TIME  ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 10)
// Channels are pre-mapped to pins, 5 is AIN5
// https://infocenter.nordicsemi.com/topic/ps_nrf52840/pin.html?cp=4_0_0_6_0_0#aqfn73
// https://infocenter.nordicsemi.com/topic/ug_nrf52840_dk/UG/dk/hw_analog_pins.html
#define ADC_CHANNEL_ID        5

static const struct device* adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));

static const struct adc_channel_cfg channel_cfg = {
  .gain = ADC_GAIN,
  .reference = ADC_REFERENCE,
  .acquisition_time = ADC_ACQUISITION_TIME,
  .channel_id = ADC_CHANNEL_ID,
  #if defined(CONFIG_ADC_CONFIGURABLE_INPUTS)
  // This dictates which pin to use
  .input_positive = NRF_SAADC_INPUT_AIN5,
#endif
};

static int16_t m_sample_buffer[1];
static int64_t battery_last_update;
static NetworkRequests* network_reqs;

const struct adc_sequence sequence = {
  .channels = BIT(ADC_CHANNEL_ID),
  .buffer = m_sample_buffer,
  .buffer_size = sizeof(m_sample_buffer),
  .resolution = ADC_RESOLUTION,
  .oversampling = 8,
  .calibrate = true,
};

bool battery_should_send_update(void) {
  return battery_last_update == 0 || k_uptime_get() > battery_last_update + BATTERY_UPDATE_INTERVAL;
}

int battery_update(void) {
  batt_reading_t r = battery_read();
  if (r.real_mV < 1) return -1;

  int err = network_reqs->handle_update_battery_level(r.real_mV, r.percent);
  if (err) {
    printk("Failed to update the battery level, skipping this period\n");
  }
  battery_last_update = k_uptime_get();
  return err;
}

int battery_init(NetworkRequests* network_requests) {
  int ret;

  network_reqs = network_requests;

  if (!device_is_ready(adc_dev)) {
    printk("ADC not ready\n");
    return -1;
  }

  ret = adc_channel_setup(adc_dev, &channel_cfg);
  if (ret) {
    printk("Error in adc setup: %d\n", ret);
  }
  return ret;
}

struct batt_reading_t battery_read(void) {
  struct batt_reading_t r;

  int ret = adc_read(adc_dev, &sequence);
  if (ret) {
    printk("Error in adc sampling: %d\n", ret);
  } else {
    r.raw = m_sample_buffer[0] + ADC_OFFSET;
    r.halved_mV = r.raw * ADC_MAX_MV / 1023;
    int remaining_mV = CLAMP(r.halved_mV - BATT_MIN_MV, 0, BATT_RANGE);
    r.percent = (remaining_mV * 100) / BATT_RANGE;
    r.real_mV = r.halved_mV * 2; // account for my external voltage divider

    printk("\tADC raw: %d, mV: %d, batt: %d, %d%%\n",
      r.raw, r.halved_mV, r.real_mV, r.percent);
  }

  return r;
}
