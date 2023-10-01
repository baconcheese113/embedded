#include <zephyr/sys/printk.h>
#include "stdint.h"
#include "stddef.h"
#include "string.h"

#include "network_requests.h"
#include "version.h"

void NetworkRequests::init(Network* network_ptr) {
  network = network_ptr;
}

int NetworkRequests::handle_get_token_and_hub_id(char* user_id, char* hub_addr, uint16_t* out_hub_id, char* out_result_msg) {
  printk("Preparing to login as Hub....\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 200 + strlen(user_id) + strlen(hub_addr) + strlen(network->device_imei);
    char mutation[len];
    snprintk(mutation, len, "{\\\"query\\\":\\\"mutation loginAndFetchHub{loginAndFetchHub(userId:%s,serial:\\\\\"%s\\\\\",imei:\\\\\"%s\\\\\",version:\\\\\"%s\\\\\"){hub{id},token}}\\\",\\\"variables\\\":{}}", user_id, hub_addr, network->device_imei, VERSION);
    cJSON* doc = network->send_request(mutation, out_result_msg);
    cJSON* resp = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "loginAndFetchHub");
    cJSON* token = cJSON_GetObjectItem(resp, "token");
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(resp, "hub"), "id");
    if (token) {
      const char* token_str = (const char*)(token->valuestring);
      printk("loginAsHub token is: %s\nAnd strlen: %u\n", token_str, strlen(token_str));
      network->set_access_token(token_str);
      // TODO check if token is different from existing token in flash storage, if so replace it
      ret = 0;
    } else {
      printk("doc->token not valid\n");
    }
    if(id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("loginAndFetchHub hub->id: %u\n", id_int);
      *out_hub_id = id_int;
      ret = 0;
    } else {
      printk("doc->hub->id not\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  network->set_power(false);
  return ret;
}

int NetworkRequests::handle_send_event(char* sensor_addr, sensor_details_t* sensor_details) {
  printk("Preparing to send event...\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 160 + strlen(sensor_addr);
    char mutation[len];
    snprintk(mutation, len, "{\\\"query\\\":\\\"mutation CreateEvent{createEvent(serial:\\\\\"%s\\\\\",batteryLevel:%u,batteryVolts:%u,version:\\\\\"%s\\\\\"){ id }}\\\",\\\"variables\\\":{}}", sensor_addr, sensor_details->battery_level, sensor_details->battery_volts, sensor_details->firmware_version);
    cJSON* doc = network->send_request(mutation);
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "createEvent"), "id");
    if (id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("created event id: %u for sensor: %s\n", id_int, sensor_addr);
      ret = 0;
    } else {
      printk("doc->id not valid\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  network->set_power(false);
  return ret;
}

int NetworkRequests::handle_add_new_sensor(char* sensor_addr, sensor_details_t* sensor_details, uint8_t door_column, uint8_t door_row, char* out_result_msg) {
  printk("Preparing to add new sensor....\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 190 + strlen(sensor_addr);
    char mutation[len];
    snprintk(mutation, len, "{\\\"query\\\":\\\"mutation CreateSensor{createSensor(doorColumn:%u,doorRow:%u,serial:\\\\\"%s\\\\\",batteryLevel:%u,batteryVolts:%u,version:\\\\\"%s\\\\\"){id}}\\\",\\\"variables\\\":{}}", door_column, door_row, sensor_addr, sensor_details->battery_level, sensor_details->battery_volts, sensor_details->firmware_version);
    cJSON* doc = network->send_request(mutation, out_result_msg);
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "createSensor"), "id");
    if (id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("createSensor id: %u\nSensor addr: %s\n", id_int, sensor_addr);
      ret = 0;
    } else {
      printk("doc->id not valid\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  network->set_power(false);
  return ret;
}

int NetworkRequests::handle_update_battery_level(int real_mV, uint8_t percent) {
  printk("Preparing to update battery level...\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 150;
    char mutation[len];
    snprintk(mutation, len, "{\\\"query\\\":\\\"mutation UpdateHubBatteryLevel{updateHubBatteryLevel(volts:%.5f,percent:%d,version:\\\\\"%s\\\\\"){id}}\\\",\\\"variables\\\":{}}", (float)real_mV / 1000.0, percent, VERSION);
    cJSON* doc = network->send_request(mutation);
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "updateHubBatteryLevel"), "id");
    if (id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("updateHubBatteryLevel hub id: %u\n", id_int);
      ret = 0;
    } else {
      printk("doc->id not valid\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  network->set_power(false);
  return ret;
}

int NetworkRequests::handle_update_gps_loc(float lat, float lng, float hdop, float speed, float course, int real_mV, uint8_t percent) {
  printk("Preparing to update gps location...\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 230;
    char mutation[len];
    snprintk(mutation, len, "{\\\"query\\\":\\\"mutation CreateLocation{createLocation(lat:%.5f,lng:%.5f,hdop:%.2f,speed:%.2f,course:%.2f,age:0){ id },updateHubBatteryLevel(volts:%.5f,percent:%d,version:\\\\\"%s\\\\\"){id}}\\\",\\\"variables\\\":{}}", lat, lng, hdop, speed, course, (float)real_mV / 1000.0, percent, VERSION);
    cJSON* doc = network->send_request(mutation);
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "createLocation"), "id");
    if (id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("createLocation id: %u\n", id_int);
      ret = 0;
    } else {
      printk("doc->id not valid\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  network->set_power(false);
  return ret;
}
