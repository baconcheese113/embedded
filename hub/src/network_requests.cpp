#include <zephyr/sys/printk.h>
#include "stdint.h"
#include "stddef.h"
#include "string.h"

#include "network_requests.h"

void NetworkRequests::init(Network* network_ptr) {
  network = network_ptr;
}

int NetworkRequests::handle_get_token_and_hub_id(char* user_id, uint16_t* out_hub_id) {
  printk("Preparing to login as Hub....\n");
  int ret = -1;
  char addr[] = "00:00:12:34:56:78";
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 100 + strlen(user_id) + strlen(network->device_imei);
    char mutation[len];
    snprintk(mutation, len, "{\"query\":\"mutation loginAsHub{loginAsHub(userId:%s, serial:\\\"%s\\\", imei:\\\"%s\\\")}\",\"variables\":{}}", user_id, addr, network->device_imei);
    cJSON* doc = network->send_request(mutation);
    cJSON* token = cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "loginAsHub");
    if (token) {
      const char* token_str = (const char*)(token->valuestring);
      printk("loginAsHub token is: %s\nAnd strlen: %llu\n", token_str, strlen(token_str));
      network->set_access_token(token_str);
      // TODO check if token is different from existing token in flash storage, if so replace it
      ret = 0;
    } else {
      printk("doc->token not valid\n");
    }
    cJSON_Delete(doc);
  } else {
    printk("Unable to get network connection\n");
  }
  // return early if we already failed
  if (ret) {
    network->set_power(false);
    return ret;
  }
  printk("Preparing to get HubViewer...\n");
  ret = -1;
  char query[] = "{\"query\":\"query getHubViewer{hubViewer{id}}\",\"variables\":{}}";
  cJSON* doc = network->send_request(query);
  cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "hubViewer"), "id");
  if (id) {
    const uint16_t id_int = (const uint16_t)(id->valueint);
    printk("getHubViewer id: %u\n", id_int);
    *out_hub_id = id_int;
    ret = 0;
  } else {
    printk("doc->id not valid\n");
  }
  cJSON_Delete(doc);
  network->set_power(false);
  return ret;
}


int NetworkRequests::handle_send_event(char* sensor_addr) {
  printk("Preparing to send event...\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 100 + strlen(sensor_addr);
    char mutation[len];
    snprintk(mutation, len, "{\"query\":\"mutation CreateEvent{createEvent(serial:\\\"%s\\\"){ id }}\",\"variables\":{}}\n", sensor_addr);
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

int NetworkRequests::handle_add_new_sensor(char* sensor_addr) {
  printk("Preparing to add new sensor....\n");
  int ret = -1;
  if (network->set_power_on_and_wait_for_reg()) {
    size_t len = 155 + strlen(sensor_addr);
    char mutation[len];
    snprintk(mutation, len, "{\"query\":\"mutation createSensor{createSensor(doorColumn: 0, doorRow: 0, isOpen: false, isConnected: true, serial:\\\"%s\\\"){id}}\",\"variables\":{}}", sensor_addr);
    cJSON* doc = network->send_request(mutation);
    cJSON* id = cJSON_GetObjectItem(cJSON_GetObjectItem(cJSON_GetObjectItem(doc, "data"), "createSensor"), "id");
    if (id) {
      const uint16_t id_int = (const uint16_t)(id->valueint);
      printk("createSensor id: %u\nAdding to known_sensor_addrs: %s\n", id_int, sensor_addr);
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