#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <cJSON.h>
#include <string.h>

#include "network.h"
#include "token_settings.h"
#include "utilities.h"
#include "serial.h"
#include "ble.h"
#include "conf.cpp"

#define MAX_NETWORK_ATTEMPTS    1

uint8_t AT_CNACT_IDX = 0;
uint8_t AT_SNI_IDX = 4;
uint8_t AT_SHREQ_IDX = 18;


static const struct gpio_dt_spec mosfet_sim = GPIO_DT_SPEC_GET(DT_NODELABEL(mosfet_sim), gpios);
static const struct device* uart1 = DEVICE_DT_GET(DT_NODELABEL(uart1));

int Network::init(void) {
  if (!device_is_ready(uart1)) {
    printk("UART device not ready\n");
    return 1;
  }
  if (!device_is_ready(mosfet_sim.port)) {
    printk("MOSFET_SIM not ready\n");
    return 1;
  }

  if (gpio_pin_configure_dt(&mosfet_sim, GPIO_OUTPUT_INACTIVE) == 0) printk("\tMOSFET_SIM pin online\n");
  serial_init();
  return 0;
}

int Network::unescaped_len(char* str) {
  int count = 0;
  for (int idx = 0; idx < (int)strlen(str); idx++) {
    if (str[idx] == '\\') idx++;
    count++;
  }
  return count;
}

int Network::initialize_access_token(void) {
  return initialize_token();
}

void Network::set_access_token(const char new_access_token[100]) {
  save_token(new_access_token);
}

bool Network::has_token() {
  return token_data.is_valid;
}

cJSON* Network::send_request(char* query, char* out_result_msg) {
  Utilities::write_rgb(0, 0, 60);
  // leave off line break to catch mistakes
  printk("Sending request:\n%s\nOf size: %d\n", query, strlen(query));

  serial_purge();
  size_t command_size = 55 + strlen(token_data.access_token);
  char auth_command[command_size]{};
  if (token_data.is_valid) {
    snprintk(auth_command, command_size,
      "AT+SHAHEAD=\"authorization\",\"Bearer %s\"\r", token_data.access_token);
  } else {
    strcpy(auth_command, "AT+SHAHEAD=\"authorization\",\"\"\r");
  }

  command_size = 25 + strlen(API_URL);
  char sni_command[command_size]{};
  snprintk(sni_command, command_size, "AT+CSSLCFG=\"sni\",1,\"%s\"\r", API_URL);

  command_size = 30 + strlen(API_URL);
  char url_command[command_size]{};
  snprintk(url_command, command_size, "AT+SHCONF=\"URL\",\"https://%s\"\r", API_URL);

  command_size = 18 + strlen(query);
  char body_command[command_size]{};
  snprintk(body_command, command_size, "AT+SHBOD=\"%s\",%d\r", query, unescaped_len(query));

  const char* const commands[] = {
    "AT+CNACT=1,\"hologram\"\r",
    "AT+CNACT?\r",
    "AT+CSSLCFG=\"sslversion\",1,3\r",
    "AT+CSSLCFG=\"ignorertctime\",1,1\r",
    sni_command,
    "AT+SHSSL=1,\"\"\r",
    "AT+SHCONF=\"BODYLEN\",1024\r",
    "AT+SHCONF=\"HEADERLEN\",350\r",
    url_command,
    "AT+SHCONN\r",
    "AT+SHCHEAD\r",
    "AT+SHAHEAD=\"Content-type\",\"application/json\"\r",
    "AT+SHAHEAD=\"User-Agent\",\"curl/7.47.0\"\r",
    "AT+SHAHEAD=\"Cache-control\",\"no-cache\"\r",
    "AT+SHAHEAD=\"Connection\",\"keep-alive\"\r",
    "AT+SHAHEAD=\"Accept\",\"*/*\"\r",
    auth_command,
    body_command,
    "AT+SHREQ=\"/\",3\r",
    "AT+SHDISC\r",
  };

  uint16_t commands_len = sizeof(commands) / sizeof(*commands);
  int64_t timeout = 20000LL;
  cJSON* doc;

  printk("Commands to iterate through: %u\n", commands_len);
  for (uint8_t attempt = 0; attempt < MAX_NETWORK_ATTEMPTS; attempt++) {
    serial_purge();
    memset(buffer, 0, RESPONSE_SIZE);
    uint16_t response_len = 0;
    for (uint8_t i = 0; i < commands_len; i++) {
      if(i == AT_SNI_IDX && !USE_SNI) continue;
      serial_print_uart(commands[i]);
      bool success = false;

      if (i == AT_CNACT_IDX) {
        // AT+CNACT=1,"hologram"
        // OK
        // +APP PDP:ACTIVE
        success = serial_did_return_str("+APP PDP:", timeout);
      } else if (i == AT_SHREQ_IDX) {
        // AT+SHREQ="https://site.com",3
        // OK
        // +SHREQ: "POST",200,593
        if (serial_did_return_ok(timeout)) {
          char response[30]{};
          // This is where we wait for the server response
          serial_read_queue(response, timeout);
          if (strlen(response) > 19) {
            response_len = strtoumax(response + 19, NULL, 10);
            success = true;
          }
        }
        // Since AT+SHREAD requires the length from previous response, need special case
        if (success) {
          success = false;
          command_size = 30;
          char read_command[command_size]{};
          snprintk(read_command, command_size, "AT+SHREAD=0,%d\r", response_len);
          serial_print_uart(read_command);
          // AT+SHREAD=0,593
          // OK
          // +SHREAD: 593
          // {"errors":[{"mess
          if (serial_did_return_str("+SHREAD", timeout)) {
            success = serial_read_queue(buffer, timeout);
          } else {
            printk("DOWNLOAD failed\n");
          }
        }
      } else {
        // AT+BOOFAR=LEET
        // OK
        success = serial_did_return_ok(timeout);
      }
      if (!success) {
        Utilities::write_rgb(70, 5, 0);
        printk(">>Network Request Timeout<<\n");
        timeout = 1000LL;
      }
    }
    printk("Request complete\nResponse is: %s\n", buffer);

    const char* error_msg = NULL;
    doc = cJSON_ParseWithOpts(buffer, &error_msg, true);
    if (strlen(error_msg) || !doc) {
      printk("parseWithOpts() failed: %s\n", error_msg);
      if(out_result_msg) {
        strncpy(out_result_msg, error_msg, 199);
      }
      if (attempt < MAX_NETWORK_ATTEMPTS - 1) {
        printk("Retrying. Attempt %d\n", attempt + 2);
      } else {
        printk("All attempts failed\n");
        cJSON_Delete(doc);
        return NULL;
      }
    } else {
      Utilities::write_rgb(0, 25, 0);
      cJSON* errors = cJSON_GetObjectItem(doc, "errors");

      if (errors) {
        printk("Access errors returned\n");
        cJSON* error0 = cJSON_GetArrayItem(errors, 0);
        cJSON* extensions = cJSON_GetObjectItem(error0, "extensions");
        char* code = cJSON_GetObjectItem(extensions, "code")->valuestring;
        if(out_result_msg) {
          strncpy(out_result_msg, buffer, 199);
        }

        if (strcmp(code, "UNAUTHENTICATED") == 0) {
          printk("Unauthenticated: Clearing access_token\n");
          set_access_token("");
          printk("Clearing %u known sensor address(es)\n", known_sensor_addrs_len);
          memset(known_sensor_addrs, 0, sizeof(known_sensor_addrs));
          known_sensor_addrs_len = 0;
          printk("access_token and known_sensor_addrs cleared\n");
        }
        cJSON_Delete(doc);
        return NULL;
      }
      break;
    }
  }
  return doc;
}

void Network::set_fun_mode(bool full_functionality) {
  char command[11]{};
  snprintk(command, 11, "AT+CFUN=%d\r", full_functionality ? 1 : 4);
  serial_print_uart(command);
  bool success = serial_did_return_ok(2000LL);
  if (!success) printk("Error setting fun mode\n");
}

bool Network::get_imei() {
  char buf[IMEI_LEN]{};
  serial_purge();
  serial_print_uart("AT+GSN\r");
  serial_did_return_str("AT+GSN", 2000LL);
  if (!serial_read_queue(buf, 500LL)) return false;


  if (buf[0] >= '0' && buf[0] <= '9') {
    strncpy(device_imei, buf, sizeof(device_imei));
  } else return false;
  if (!serial_did_return_ok(500LL)) {
    memset(device_imei, 0, sizeof(device_imei));
    return false;
  }
  return true;
}

bool Network::configure_modem(void) {
  if (!is_powered_on()) return false;
  serial_print_uart("AT+IPR=115200\r");
  bool ret = serial_did_return_ok(2000LL);
  if (!ret) printk("Unable to configure IPR\n");
  return ret;
}

bool Network::wait_for_power_on(void) {
  if (is_powered_on()) {
    printk("\tAlready powered on\n");
    return true;
  }
  int64_t startTime = k_uptime_get();
  // Usually takes around 6 seconds from cold boot
  if (!serial_did_return_str("SMS Ready", 20000LL)) return false;

  printk("\tPowered On! Took %llims\n", k_uptime_get() - startTime);
  return true;
}

int8_t Network::get_reg_status() {
  char resp[5]{};
  serial_purge();

  char tx_buf[MSG_SIZE];
  serial_print_uart("AT+CREG?\r");

  if (!serial_did_return_str("AT+CREG?", 1000LL, false)) return -1;
  if (!serial_read_queue(tx_buf, 2000LL, false)) return -1;
  // tx_buf should have "+CREG: 0,5"
  strncpy(resp, tx_buf + 7, 5);
  if (!serial_did_return_ok(500LL, false)) return -1;

  int8_t status = resp[2] - '0';
  if (status != last_status) {
    char details[35];
    // Only print status if it has changed
    if (status == 0) strcpy(details, "Not registered, not searching");
    else if (status == 1) strcpy(details, "Registered, Home network");
    else if (status == 2) strcpy(details, "Not registered, searching");
    else if (status == 3) strcpy(details, "Registration denied");
    else if (status == 4) strcpy(details, "Unknown, possibly out of range");
    else if (status == 5) strcpy(details, "Registered, roaming");
    else strcpy(details, "ERROR");
    last_status = status;

    if (strcmp(details, "ERROR") == 0) {
      printk("Registration status unknown\n");
    } else printk("\nRegistration Status: %s\n", details);
  }
  return status;
}

int8_t Network::get_acc_tech(void) {
  if (last_status != 1 && last_status != 5) return -1;
  char resp[30]{};
  char tx_buf[MSG_SIZE]{};
  serial_purge();
  serial_print_uart("AT+CREG=2\r");
  if (!serial_did_return_ok(2000LL)) return -1;

  serial_print_uart("AT+CREG?\r");

  if (!serial_did_return_str("AT+CREG?", 1000LL)) return -1;
  if (!serial_read_queue(tx_buf, 2000LL)) return -1;
  // tx_buf should have "+CREG: 0,5,...."
  strncpy(resp, tx_buf + 7, 30);
  if (!serial_did_return_ok(500LL)) return -1;

  int8_t status = resp[2] - '0';
  int8_t accTech = -1;

  if (status == 1 || status == 5) {
    char details[30]{};
    // We're registered, print access technology
    accTech = resp[strlen(resp) - 1] - '0';
    if (accTech == 0) strcpy(details, "GSM");
    else if (accTech == 2) strcpy(details, "UTRAN");
    else if (accTech == 3) strcpy(details, "GSM w/EGPRS");
    else if (accTech == 4) strcpy(details, "UTRAN w/HSDPA");
    else if (accTech == 5) strcpy(details, "UTRAN w/HSUPA");
    else if (accTech == 6) strcpy(details, "UTRAN w/HSDPA and w/HSUPA");
    else if (accTech == 7) strcpy(details, "E-UTRAN");
    else strcpy(details, "ERROR");

    if (strcmp(details, "ERROR") == 0) {
      printk("Registration network unknown\n");
    } else printk("Registration on network: %s\n", details);
  }

  serial_print_uart("AT+CREG=0\r");
  if (!serial_did_return_ok(2000LL)) return -1;
  return accTech;
}

void Network::set_power(bool on) {
  serial_purge();
  gpio_pin_set_dt(&mosfet_sim, on ? 1 : 0);
  if (on) {
    printk("Powering on SIM module...\n");
    last_status = -1;
  } else {
    printk("Powering off SIM module...\n");
  }
}

bool Network::is_powered_on(void) {
  serial_print_uart("AT\r");
  return serial_did_return_ok(100LL);
}

bool Network::set_power_on_and_wait_for_reg(void) {
  int64_t start_time = k_uptime_get();
  set_power(true);
  if (!wait_for_power_on()) {
    printk("\tWait for power on failed\n");
    set_power(false);
    return false;
  }

  serial_print_uart("AT+CNMP?\r");
  if (!serial_did_return_str("+CNMP: ", 4000LL)) return false;

  int8_t regStatus = -1;
  while (k_uptime_get() < start_time + 60000LL) {
    regStatus = get_reg_status();
    if (regStatus == 5 || regStatus == 1) {
      printk("\tRegistered!\n");
      break;
    }
    k_msleep(50);
  }
  if (regStatus != 5 && regStatus != 1) {
    printk("\tRegStatus %i not valid\n", regStatus);
    set_power(false);
    return false;
  }
  int8_t accTech = get_acc_tech();
  if (accTech == -1) {
    printk("\tAccess tech %i not valid\n", accTech);
    set_power(false);
    return false;
  }
  printk("Registered! Total Boot up time(ms): %lld\n", k_uptime_get() - start_time);
  return true;
}

bool Network::set_preferred_mode(PreferredMode mode) {
  serial_purge();
  serial_print_uart("AT+CNMP?\r");
  if (!serial_did_return_str("+CNMP: ", 4000LL)) return false;
  // tx_buf should have "+CNMP: 2" or some other mode
  char command[12]{};
  snprintk(command, 12, "AT+CNMP=%u\r", (uint8_t) mode);
  serial_print_uart(command);
  return serial_did_return_ok(4000LL);
}

bool Network::send_test_request(void) {
  serial_purge();

  const char* const commands[] = {
    "AT+CNACT=1,\"hologram\"\r", // +APP PDP
    "AT+CNACT?\r",
    "AT+CSSLCFG=\"sslversion\",1,3\r",
    "AT+CSSLCFG=\"ignorertctime\",1,1\r",
    "AT+CSSLCFG=\"sni\",1,\"httpbin.org\"\r",
    "AT+SHSSL=1,\"\"\r",
    "AT+SHCONF=\"BODYLEN\",1024\r",
    "AT+SHCONF=\"HEADERLEN\",350\r",
    "AT+SHCONF=\"URL\",\"https://httpbin.org\"\r",
    "AT+SHCONN\r",
    "AT+SHCHEAD\r",
    "AT+SHAHEAD=\"User-Agent\",\"curl/7.47.0\"\r",
    "AT+SHAHEAD=\"Cache-control\",\"no-cache\"\r",
    "AT+SHAHEAD=\"Connection\",\"keep-alive\"\r",
    "AT+SHAHEAD=\"Accept\",\"*/*\"\r",
    "AT+SHREQ=\"/get?user=jack&password=123\", 1\r", // +SHREQ
    "AT+SHREAD=0,250\r", // +SHREAD
    "AT+SHDISC\r",
    "AT+CNACT=0\r"
  };
  
  uint16_t commands_len = sizeof(commands) / sizeof(*commands);
  int64_t timeout = 15000LL;
  bool success = false;
  for (uint8_t i = 0; i < commands_len; i++) {
    serial_print_uart(commands[i]);

    if(i == 0) {
      success = serial_did_return_str("+APP PDP", timeout);
    } else if(i == 15) {
      success = serial_did_return_str("+SHREQ", timeout);
    } else if(i == 16) {
      success = serial_did_return_str("+SHREAD", timeout);
      k_msleep(50);
      serial_purge();
    } else {
      success = serial_did_return_ok(timeout);
    }

    if (!success) {
      Utilities::write_rgb(70, 5, 0);
      printk(">>Network Request Timeout<<\n");
      timeout = 1000LL;
    }
  }
  return success;
}
// IMEI example
// 065 084 043 071 083 078 013 013 010 
// 056 054 057 057 058 049 048 053 053 057 057 055 056 057 049 013 010
// 013 010
// 079 075 013 010

// AT+CNACT=1, "hologram"
// OK
// +APP PDP:ACTIVE
// AT+CNACT?
// +CNACT:1, "xxx.xx.xxx.116"
// OK
// AT+CSSLCFG="sslversion",1,3
// OK
// AT+CSSLCFG="ignorertctime",1,1
// OK
// AT+CSSLCFG="sni",1,"domain.com"
// OK
// AT+SHSSL=1,""
// OK
// AT+SHCONF="BODYLEN",1024
// OK
// AT+SHCONF="HEADERLEN",350
// OK
// AT+SHCONF="URL", "https://httpbin.org"
// OK
// AT+SHCONN
// OK
// AT+SHCHEAD
// OK
// AT+SHAHEAD="Content-type","application/json"
// OK
// AT+SHAHEAD="User-Agent","curl/7.47.0"
// OK
// AT+SHAHEAD="Cache-control","no-cache"
// OK
// AT+SHAHEAD="Connection","keep-alive"
// OK
// AT+SHAHEAD="Accept","*/*"
// OK
// AT+SHAHEAD="authorization","Bearer eyJhbGciOiJIUzI1NiJ9ao2918391938-19189283"
// OK
// AT+SHBOD="{\"query\":\"query getMySensors{hubViewer{sensors{serial}}}\",\"variables\":{}}",73
// OK
// AT+SHREQ="/",3
// OK
// +SHREQ: "POST",200,68
// AT+SHREAD=0,68
// OK
// +SHREAD: 68
// {"data":{"hubViewer":{"sensors":[{"serial":"12:23:34:40:7B:23"}]}}}
// AT+SHDISC
// OK
