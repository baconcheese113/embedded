#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <cJSON.h>

#include "utilities.h"
#include "battery.h"
#include "network_requests.h"
#include "network.h"
#include "location.h"
#include "ble.h"
#include "version.h"

Network network;
Location location;
NetworkRequests network_requests;

void main(void)
{
  int64_t boot_time = k_uptime_get();
  printk("############### Sketch version %s ###############\n", VERSION);
  printk("Booting...\n");
  printk("Board: %s\n", CONFIG_BOARD);
  Utilities::setup_pins();
  k_msleep(1000);

  printk("Starting dance\n");
  Utilities::happy_dance();
  printk("\t✔️  Dance complete\n");

  printk("Checking for peripheral connections to SIM module...\n");
  if (network.init() != 0) {
    printk("\tSIM module failed to initialize\n");
    return;
  }
  printk("\t✔️   SIM peripherals ready\n");

  network.set_power(true);

  while (!network.wait_for_power_on()) {
    printk("\tSIM module failed to power on, retrying\n");
  }

  location.set_gps_power(false);

  printk("\tLocation turned off\n");
  while (!network.configure_modem()) {
    printk("\tSIM module failed to be configured, retrying\n");
  }

  while (strlen(network.device_imei) < 1) {
    network.get_imei();
    printk("\tDevice IMEI: %s\n", network.device_imei);
  }
  printk("\t✔️  SIM module setup complete\n");

  network_requests.init(&network);
  location.init(&network, &network_requests);

  printk("Initializing Battery functionality...\n");
  if (battery_init(&network_requests)) {
    printk("Battery init failed\n");
  } else printk("\tBattery reading starting from %dmV\n", battery_read().real_mV);

  printk("Intializing BLE peripheral, RTC, and button driven interrupts...\n");
  if (init_ble(&network_requests) == 0) {
    printk("\t✔️  BLE, RTC, and IRQs ready\n");
  } else return;

  printk("Checking peristent storage for saved configs...\n");
  network.initialize_access_token();
  printk("\t✔️  Persistent storage ready\n");

  if (network.has_token() && network.set_power_on_and_wait_for_reg()) {
    char sensor_query[] = "{\"query\":\"query getMySensors{hubViewer{sensors{serial}}}\",\"variables\":{}}\r";
    cJSON* doc = network.send_request(sensor_query);
    cJSON* sensors = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "hubViewer"), "sensors");
    // const char* const path[] = { "data", "hubViewer", "sensors" };
    // cJSON* sensors = Utilities::cJSON_GetNested(doc, path);
    if (cJSON_GetArraySize(sensors)) {
      cJSON* sensor;
      printk("Array size is %d\n", cJSON_GetArraySize(sensors));
      cJSON_ArrayForEach(sensor, sensors) {
        printk("Sensor id %d, and serial is %s\n",
          cJSON_GetObjectItem(sensor, "id")->valueint,
          cJSON_GetObjectItem(sensor, "serial")->valuestring
        );
        add_known_sensor(cJSON_GetObjectItem(sensor, "serial")->valuestring);
      }
    }
    cJSON_Delete(doc);
  }

  network.set_power(false);

  printk("\n>>>>> Setup complete in %lli(ms)! <<<<<\n\n", k_uptime_delta(&boot_time));

  k_msleep(1000);
  printk(">>>> Starting main loop <<<<\n");
  k_msleep(2000);
  start_scan();

  while (1) {

    if (battery_should_send_update()) {
      battery_update();
    }
    if (location.should_send_update()) {
      location.send_update();
    }

    k_msleep(10 * 1000);
  }
}
