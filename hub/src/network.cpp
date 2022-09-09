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
// #include "conf.cpp"
const char* API_URL = "http://ptsv2.com/t/7ahpc-1662517472/post";

uint8_t AT_SAPBR_IDX = 0;
uint8_t AT_HTTPDATA_IDX = 6;
uint8_t AT_HTTPACTION_IDX = 7;
uint8_t AT_HTTPREAD_IDX = 8;


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

int Network::initialize_access_token(void) {
  return initialize_token();
}

void Network::set_access_token(const char new_access_token[100]) {
  save_token(new_access_token);
}

bool Network::has_token() {
  return token_data.is_valid;
}

cJSON* Network::send_request(char* query) {
  Utilities::write_rgb(0, 0, 60);
  // leave off line break to catch mistakes
  printk("Sending request:\n%s", query);

  serial_purge();
  size_t command_size = 55 + strlen(token_data.access_token);
  char auth_command[command_size]{};
  if (token_data.is_valid) {
    snprintk(auth_command, command_size,
      "AT+HTTPPARA=\"USERDATA\",\"Authorization:Bearer %s\"\r", token_data.access_token);
  } else {
    strcpy(auth_command, "AT+HTTPPARA=\"USERDATA\",\"\"\r");
  }

  command_size = 30 + strlen(API_URL);
  char url_command[30 + strlen(API_URL)]{};
  snprintk(url_command, command_size, "AT+HTTPPARA=\"URL\",\"%s\"\r", API_URL);

  command_size = 30;
  char len_command[command_size]{};
  snprintk(len_command, command_size, "AT+HTTPDATA=%d,%d\r", strlen(query), 5000);

  const char* const commands[] = {
    // "AT+SAPBR=3,1,\"APN\",\"hologram\"",
    // "AT+SAPBR=3,1,\"Contype\",\"GPRS\"",
    "AT+SAPBR=1,1\r",
    "AT+HTTPINIT\r",
    "AT+HTTPPARA=\"CID\",1\r",
    auth_command,
    url_command,
    "AT+HTTPPARA=\"CONTENT\",\"application/json\"\r",
    len_command,
    "AT+HTTPACTION=1\r",
    "AT+HTTPREAD\r",
    "AT+HTTPTERM\r",
    "AT+SAPBR=0,1\r",
  };

  uint16_t commands_len = sizeof(commands) / sizeof(*commands);
  const int64_t TIMEOUT = 5000LL;
  cJSON* doc;

  printk("Commands to iterate through: %u\n", commands_len);
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    memset(buffer, 0, RESPONSE_SIZE);
    for (uint8_t i = 0; i < commands_len; i++) {
      serial_purge();
      k_msleep(20);
      serial_print_uart(commands[i]);
      bool success = false;

      if (i == AT_HTTPDATA_IDX) {
        // AT+HTTPDATA=120,5000
        // DOWNLOAD
        // OK
        // TODO gather the confidence to break on this failure
        if (serial_did_return_str("DOWNLOAD", 1000LL)) {
          // printk("DOWNLOAD received\n");
        }
        // receive NO CARRIER response without waiting this amount
        // TODO maybe remove Utilities::bleDelay(900, BLE); 
        k_msleep(500);
        // printk("Awake\n");
        serial_print_uart(query);
        // printk("Printed query\n");
        k_msleep(100);
        // printk("Napped\n");
        success = serial_did_return_ok(5000LL);
        // printk("Success is %i\n", success);
      } else if (i == AT_HTTPACTION_IDX) {
        // AT+HTTPACTION=1
        // OK
        // +HTTPACTION: 1,200,27
        if (!serial_did_return_ok(TIMEOUT)) break;
        success = serial_did_return_str("+HTTPACTION:", TIMEOUT);
      } else if (i == AT_HTTPREAD_IDX) {
        // AT+HTTPREAD
        // +HTTPREAD: 27
        // {"data":{"user":{"id":1}}}
        // OK
        if (!serial_did_return_str("+HTTPREAD:", TIMEOUT)) break;
        success = serial_read_raw_until("OK", buffer, TIMEOUT);
      } else {
        // AT+BOOFAR=LEET
        // OK
        // Long delays possible setting SAPBR with GSM
        int64_t timeout = i == AT_SAPBR_IDX ? 10000LL : TIMEOUT;
        success = serial_did_return_ok(timeout);
      }
      if (!success) {
        Utilities::write_rgb(70, 5, 0);
        printk(">>Network Request Timeout<<\n");
      }
    }
    printk("Request complete\nResponse is: %s\n", buffer);

    const char* error_msg = NULL;
    doc = cJSON_ParseWithOpts(buffer, &error_msg, true);
    if (strlen(error_msg) || !doc) {
      printk("parseWithOpts() failed: %s\n", error_msg);
      if (attempt < 2) {
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
        // TODO clear knownSensorAddrs
        if (strcmp(code, "UNAUTHENTICATED") == 0) {
          printk("Unauthenticated: Clearing access_token\n");
          set_access_token("");
          printk("access_token cleared\n");
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
    device_imei[strlen(buf)] = '\0';
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
  if (!serial_did_return_str("SMS Ready", 10000LL)) return false;

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
  int8_t regStatus = -1;
  while (k_uptime_get() < start_time + 30000LL) {
    regStatus = get_reg_status();
    if (regStatus == 5 || regStatus == 1) break;
    k_msleep(50);
  }
  if (regStatus != 5 && regStatus != 1) {
    printk("\tRegStatus not valid\n");
    set_power(false);
    return false;
  }
  if (get_acc_tech() == -1) {
    printk("\tAccess tech not valid\n");
    set_power(false);
    return false;
  }
  printk("Registered! Total Boot up time(ms): %lld\n", k_uptime_get() - start_time);
  return true;
}
// IMEI example
// 065 084 043 071 083 078 013 013 010 
// 056 054 057 057 058 049 048 053 053 057 057 055 056 057 049 013 010
// 013 010
// 079 075 013 010

// https://robu.in/sim800l-interfacing-with-arduino/
// https://github.com/stephaneAG/SIM800L
// AT+SAPBR=3,1,"Contype","GPRS"
// OK
// AT+SAPBR=1,1
// OK
// AT+HTTPINIT
// OK
// AT+HTTPPARA="CID",1
// OK
// AT+HTTPPARA="URL","http://thisshould.behidden.com"
// OK
// AT+HTTPPARA="CONTENT","application/json"
// OK
// AT+HTTPDATA=120,5000
// DOWNLOAD
// OK
// AT+HTTPACTION=1
// OK
// +HTTPACTION: 1,200,27
// AT+HTTPREAD
// +HTTPREAD: 27
// {"data":{"user":{"id":1}}}
// OK
// AT+HTTPTERM
// OK
// AT+SAPBR=0,1
// OK
