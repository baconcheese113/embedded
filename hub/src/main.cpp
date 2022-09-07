#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#include <cJSON.h>

#include "utilities.h"
#include "network.h"
#include "location.h"
#include "ble.h"

const uint16_t VERSION = 1;

Network network;
Location location;

void main(void)
{
  int64_t boot_time = k_uptime_get();
  printk("############### Sketch version %d ###############\n", VERSION);
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


  printk("Intializing BLE peripheral, RTC, and button driven interrupts...\n");
  if (init_ble() == 0) {
    printk("\t✔️  BLE, RTC, and IRQs ready\n");
  } else return;

  printk("Checking peristent storage for saved configs...\n");
  network.initialize_access_token();
  printk("\t✔️  Persistent storage ready\n");

  if (/*network.has_token() &&*/ network.set_power_on_and_wait_for_reg()) {
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

  while (1) {

    // TODO check currentCommand for StartHubUpdate

    // TODO if advertising, try to connect to phone
    // TODO if we already have phone, listen for commands

    // TODO if no peripheral, scan for sensor
    // TODO else if peripheral not connected then connect
    // TODO else monitor sensor 

    // TODO Update GPS
    // TODO Update Battery level

    k_msleep(10);
  }
}
