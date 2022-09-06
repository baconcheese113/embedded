#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

#include "network.h"
#include "token_settings.h"
#include "utilities.h"
#include "serial.h"

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

// DynamicJsonDocument Network::send_request(char* query, BLELocalDevice* BLE) {
//     Utilities::analogWriteRGB(0, 0, 60);
//     Serial.println("Sending request");
//     Serial.println(query);

//     while (Serial1.available()) Serial.print(Serial1.read());
//     char authCommand[55 + strlen(token_data.access_token)]{};
//     if (token_data.is_valid) {
//         sprintf(authCommand, "AT+HTTPPARA=\"USERDATA\",\"Authorization:Bearer %s\"", token_data.access_token);
//     } else {
//         strcpy(authCommand, "AT+HTTPPARA=\"USERDATA\",\"\"");
//     }

//     char urlCommand[30 + strlen(API_URL)]{};
//     sprintf(urlCommand, "AT+HTTPPARA=\"URL\",\"%s\"", API_URL);

//     char lenCommand[30]{};
//     sprintf(lenCommand, "AT+HTTPDATA=%d,%d", strlen(query), 5000);

//     const char* const commands[] = {
//         // "AT+SAPBR=3,1,\"APN\",\"hologram\"",
//         // "AT+SAPBR=3,1,\"Contype\",\"GPRS\"",
//         "AT+SAPBR=1,1",
//         "AT+HTTPINIT",
//         "AT+HTTPPARA=\"CID\",1",
//         authCommand,
//         urlCommand,
//         "AT+HTTPPARA=\"CONTENT\",\"application/json\"",
//         lenCommand,
//         "AT+HTTPACTION=1",
//         "AT+HTTPREAD",
//         "AT+HTTPTERM",
//         "AT+SAPBR=0,1",
//     };

//     char response[RESPONSE_SIZE]{};
//     int size;
//     unsigned int commandsLen = sizeof commands / sizeof * commands;
//     unsigned long timeout;
//     DynamicJsonDocument doc(RESPONSE_SIZE);
//     Serial.print("Commands to iterate through: ");
//     Serial.println(commandsLen);
//     for (uint8_t attempt = 0; attempt < 3; attempt++) {
//         for (uint8_t i = 0; i < commandsLen; i++) {
//             // Required so that services can be read for some reason
//             // FIXME - https://github.com/arduino-libraries/ArduinoBLE/issues/175
//             // https://github.com/arduino-libraries/ArduinoBLE/issues/236
//             BLE->poll();
//             memset(buffer, 0, RESPONSE_SIZE);
//             size = 0;

//             Serial1.println(commands[i]);
//             Serial1.flush();
//             timeout = millis() + 1200;
//             if (i == AT_HTTPDATA_IDX) { // send query to HTTPDATA command
//                 char str[40]{};
//                 uint8_t len = 0;
//                 while (millis() < timeout) {
//                     if (Serial1.available()) {
//                         str[len++] = Serial1.read();
//                         if (len >= 30) {
//                             str[len] = '\0';
//                             if (strcmp(str + (len - 10), "DOWNLOAD\r\n") == 0) break;
//                         }
//                     }
//                 }
//                 Serial.println(str);
//                 Utilities::bleDelay(900, BLE); // receive NO CARRIER response without waiting this amount
//                 Serial1.write(query);
//                 Serial1.flush();
//             }
//             timeout = millis() + 5000;
//             while (millis() < timeout) {
//                 if (Serial1.available()) {
//                     BLE->poll();
//                     buffer[size] = Serial1.read();
//                     Serial.write(buffer[size]);
//                     size++;
//                     if (size >= 6
//                         && buffer[size - 1] == 10
//                         && buffer[size - 2] == 13
//                         && buffer[size - 5] == 10 && buffer[size - 4] == 'O' && buffer[size - 3] == 'K' // OK
//                         ) {
//                         if (i == AT_HTTPACTION_IDX) { // special case for AT+HTTPACTION response responding OK before query resolve :/
//                             while (Serial1.available() < 1 && millis() < timeout) { BLE->poll(); }
//                             while (Serial1.available() > 0 && millis() < timeout) {
//                                 BLE->poll();
//                                 buffer[size] = Serial1.read();
//                                 Serial.write(buffer[size]);
//                                 size++;
//                             }
//                         }
//                         buffer[size] = '\0';
//                         if (i == AT_HTTPREAD_IDX) { // special case for AT+HTTPREAD to extract the response
//                             int16_t responseIdxStart = -1;
//                             for (int idx = 0; idx < size - 7; idx++) {
//                                 BLE->poll();
//                                 if (responseIdxStart == -1 && buffer[idx] == '{') responseIdxStart = idx;
//                                 if (responseIdxStart > -1) response[idx - responseIdxStart] = buffer[idx];
//                                 if (responseIdxStart > -1 && idx == size - 8) response[idx - responseIdxStart + 1] = '\0';
//                             }
//                         }
//                         break;
//                     }
//                 }
//             }
//             if (millis() >= timeout) {
//                 Utilities::analogWriteRGB(70, 5, 0);
//                 Serial.println(">>Network Request Timeout<<");
//             }
//         }
//         Serial.print("Request complete\nResponse is: ");
//         Serial.println(response);

//         DeserializationError error = deserializeJson(doc, (const char*)response);
//         if (error) {
//             Serial.print("deserializeJson() failed: ");
//             Serial.println(error.f_str());
//             if (attempt < 2) {
//                 Serial.print("Retrying. Attempt ");
//                 Serial.println(attempt + 2);
//             } else {
//                 Serial.println("All attempts failed");
//             }
//         } else {
//             Utilities::analogWriteRGB(0, 25, 0);
//             if (doc["errors"] && doc["errors"][0]["extensions"]["code"]) {
//                 // TODO clear knownSensorAddrs
//                 if (strcmp(doc["errors"][0]["extensions"]["code"], "UNAUTHENTICATED") == 0) {
//                     Serial.println("Unauthenticated: Clearing access_token");
//                     memset(token_data.access_token, 0, 100);
//                     token_data.is_valid = false;
//                     kv_set(storeTokenKey, &token_data, sizeof(token_data), 0);
//                     Serial.println("access_token cleared");
//                 }
//             }
//             break;
//         }
//     }
//     return doc;
// }

void Network::set_fun_mode(bool full_functionality) {
    char command[11]{};
    snprintk(command, 11, "AT+CFUN=%d\r", full_functionality ? 1 : 4);
    serial_print_uart(command);
    bool success = serial_did_return_ok(2000LL);
    if (!success) printk("Error setting fun mode\n");
}

bool Network::get_imei() {
    memset(buffer, 0, MSG_SIZE);
    serial_print_uart("AT+GSN\r");
    int64_t timeout = k_uptime_get() + 2000LL;
    size_t len = 0;
    while (k_uptime_get() < timeout)
    {
        if (k_msgq_get(&uart_msgq, &buffer, K_NO_WAIT) == 0) {
            if (strlen(device_imei) > 10 && strcmp(buffer, "OK") == 0) {
                return true;
            } else {
                len = strlen(buffer);
                strncpy(device_imei, buffer, len);
                device_imei[len] = '\0';
            }
        }
    }
    memset(device_imei, 0, len);
    return false;
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
    char tx_buf[MSG_SIZE]{};
    int64_t timeout = k_uptime_get() + 2000LL;
    while (k_uptime_get() < timeout) {
        if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
            if (strcmp("SMS Ready", tx_buf) == 0) {
                printk("\tPowered On! Took %llims\n", k_uptime_get() - startTime);
                return true;
            }
        }
    }
    return false;
}

int8_t Network::get_reg_status() {
    char resp[4]{};
    serial_purge();

    char tx_buf[MSG_SIZE];
    serial_print_uart("AT+CREG?\r");
    int64_t timeout = k_uptime_get() + 2000LL;
    while (k_uptime_get() < timeout)
    {
        if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
            if (strlen(resp) > 1 && strcmp(tx_buf, "OK") == 0) {
                return true;
            } else if (strncmp(tx_buf, "+CREG: ", 7) == 0) {
                strncpy(resp, tx_buf + 7, 4);
                resp[3] = '\0';
            }
        }
    }

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
        } else printk("Registration Status: %s\n", details);
    }
    return status;
}

int8_t Network::get_acc_tech(void) {
    if (last_status != 1 && last_status != 5) return -1;
    char resp[30]{};
    char tx_buf[MSG_SIZE]{};
    bool success = false;
    serial_purge();
    serial_print_uart("AT+CREG=2\r");
    if (!serial_did_return_ok(2000LL)) return -1;

    serial_print_uart("AT+CREG?\r");
    // Utilities::readUntilResp("AT+CREG?\r\r\n+CREG: ", resp);
    success = false;
    int64_t timeout = k_uptime_get() + 2000LL;
    while (k_uptime_get() < timeout) {
        if (k_msgq_get(&uart_msgq, &tx_buf, K_NO_WAIT) == 0) {
            if (strlen(resp) > 10 && strcmp(tx_buf, "OK") == 0) {
                success = true;
                break;
            } else if (strncmp(tx_buf, "+CREG: ", 7) == 0) {
                strncpy(resp, tx_buf + 7, 4);
                resp[3] = '\0';
            }
        }
    }
    if (!success) return -1;

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
        set_power(false);
        return false;
    }
    if (get_acc_tech() == -1) {
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
