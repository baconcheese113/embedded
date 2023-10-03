#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/gatt_dm.h>
#include <bluetooth/scan.h>
#include <bluetooth/services/bas_client.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/sys/printk.h>

// DFU OTA
#include "version.h"

#include "ble.h"
#include "battery.h"
#include "alarm.h"
#include "utilities.h"
#include "network_requests.h"
#include "network.h"
#include "diagnostic.h"

#define DEVICE_NAME			  CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

#define PERIPHERAL_NAME	"HandleIt Client"

#define BLE_COOLDOWN_MS   30 * 1000
#define ADV_DURATION_MS		30 * 1000

#define BT_UUID_HUB_SERVICE_VAL      BT_UUID_128_ENCODE(0x0000181a, 0x0000, 0x1000, 0x8000, 0x00805f9b34fc)

static struct bt_uuid_128 hub_svc_uuid = BT_UUID_INIT_128(BT_UUID_HUB_SERVICE_VAL);
static struct bt_uuid_128 command_char_uuid = BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x00002A58, 0x0000, 0x1000, 0x8000, 0x00805f9b34fd));
#define FIRMWARE_VERSION_CHAR   BT_UUID_DIS_FIRMWARE_REVISION

struct k_work work;
struct k_work_q ble_work_q;
K_THREAD_STACK_DEFINE(ble_stack_area, 4096);
const k_work_queue_config ble_work_q_config = {
  .name = "ble_work_q",
};

// Don't change these during discovery!
static char command_char_val[210];
static char version[] = VERSION;

#define MAC_ADDR_LEN      18
char hub_mac[MAC_ADDR_LEN];
/**
 *  Weird hack to support iOS devices
 * Storing MAC in Manufacturer data so iOS can read it
 * First 2 bytes are mock company ID, then 6 bytes of mac address
 */
#define MANU_ID_LEN           2
#define MANU_DATA_LEN         BT_ADDR_SIZE + MANU_ID_LEN
static uint8_t hub_mac_bytes[MANU_DATA_LEN] = {0, 0};

const char* COMMAND_START_SENSOR_SEARCH = "StartSensorSearch";
const char* COMMAND_SENSOR_CONNECT = "SensorConnect";
const char* COMMAND_START_DIAGNOSTIC = "StartDiagnostic";
const char* COMMAND_DIAGNOSTIC_RESULT = "DiagnosticResult";

static struct bt_conn* phone_conn;
static struct bt_conn* sensor_conn;

// Have to declare here to avoid "taking address of temporary array" error
const struct bt_le_adv_param* adv_param = BT_LE_ADV_CONN;
const struct bt_le_scan_param* scan_param = BT_LE_SCAN_ACTIVE;
const struct bt_conn_le_create_param* create_param = BT_CONN_LE_CREATE_CONN;
const struct bt_le_conn_param* conn_param = BT_LE_CONN_PARAM_DEFAULT;

static NetworkRequests* network_reqs;
static Network* network;

int64_t adv_start_time;
int64_t last_event_time;
bool is_adding_new_sensor = false;
bool is_making_network_request = false;

// 10 available address slots
char known_sensor_addrs[10][50];
uint8_t known_sensor_addrs_len;

static struct sensor_details_t sensor_details;
static uint8_t door_column;
static uint8_t door_row;

static void handle_sensor_search_work(struct k_work* work_item) {
  printk("handling sensor search work\n");
  is_adding_new_sensor = true;
  start_scan();
}

static void handle_add_sensor_work(struct k_work* work_item) {
  printk("handling add sensor work\n");
  char addr[MAC_ADDR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(sensor_conn), addr, sizeof(addr));
  addr[MAC_ADDR_LEN - 1] = '\0';

  size_t err_size = 210;
  char err_msg[err_size] = "";
  is_making_network_request = true;
  int err = network_reqs->handle_add_new_sensor(addr, &sensor_details, door_column, door_row, err_msg);
  is_making_network_request = false;
  if (err) {
    printk("Unable to add sensor\n");
    snprintk(command_char_val, err_size, "Error:%s", err_msg);
  } else {
    add_known_sensor(addr);
    if (phone_conn) {
      // TODO Use notify
      strcpy(command_char_val, "SensorAdded:1");
    }
  }
  bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
}

static void diagnostic_work(struct k_work* work_item) {
  printk("handling diagnostic work\n");
  char result_msg[210] = "";
  diagnostic_run(result_msg);
  k_msleep(200);
  while(diagnostic_running) {
    k_msleep(100);
    snprintk(command_char_val, 200, "%s:%s", COMMAND_DIAGNOSTIC_RESULT, result_msg);
  }
  k_msleep(2000);
  snprintk(command_char_val, 30, "%s:END", COMMAND_DIAGNOSTIC_RESULT);
}

static ssize_t read_command_char(struct bt_conn* conn, const struct bt_gatt_attr* attr,
  void* buf, uint16_t len, uint16_t offset)
{
  const char* value = (const char*)attr->user_data;
  printk("Read command attempt and it's %s\n", command_char_val);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_command_char(struct bt_conn* conn, const struct bt_gatt_attr* attr,
  const void* buf, uint16_t len, uint16_t offset, uint8_t flags)
{
  uint8_t* value = (uint8_t*)attr->user_data;

  if (offset + len > 30) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  memcpy(value + offset, buf, len);
  value[offset + len] = 0;

  printk("\nWrite command attempt: %s\n", (char*)value);
  if (strlen((char*)value)) {
    Command command = Utilities::parse_raw_command((char*)value);
    if (strcmp(command.type, COMMAND_START_SENSOR_SEARCH) == 0) {
      k_work_init(&work, handle_sensor_search_work);
      k_work_submit_to_queue(&ble_work_q, &work);
    } else if (strcmp(command.type, COMMAND_SENSOR_CONNECT) == 0) {
      door_column = command.value[0] - '0';
      door_row = command.value[1] - '0';
      printk("Connecting to sensor at door_column %u, door_row %u\n", door_column, door_row);
      k_work_init(&work, handle_add_sensor_work);
      k_work_submit_to_queue(&ble_work_q, &work);
    } else if (strcmp(command.type, COMMAND_START_DIAGNOSTIC) == 0) {
      printk("Starting diagnostic\n");
      k_work_init(&work, diagnostic_work);
      k_work_submit_to_queue(&ble_work_q, &work);
    }
  }

  return len;
}

static ssize_t read_version_char(struct bt_conn* conn, const struct bt_gatt_attr* attr,
  void* buf, uint16_t len, uint16_t offset)
{
  const char* value = (const char*)attr->user_data;
  printk("Read version attempt and version: %s\n", value);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

BT_GATT_SERVICE_DEFINE(hub_svc,
  BT_GATT_PRIMARY_SERVICE(&hub_svc_uuid),
  BT_GATT_CHARACTERISTIC(&command_char_uuid.uuid,
    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
    read_command_char, write_command_char, command_char_val),
  BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
  BT_GATT_CHARACTERISTIC(FIRMWARE_VERSION_CHAR,
    BT_GATT_CHRC_READ,
    BT_GATT_PERM_READ,
    read_version_char, NULL, version),
  );

static const struct bt_data ad[] = {
  BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
  BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static struct bt_data sd[] = {
  BT_DATA_BYTES(BT_DATA_UUID128_SOME, BT_UUID_HUB_SERVICE_VAL),
  {
    .type = BT_DATA_MANUFACTURER_DATA,
    .data_len = MANU_DATA_LEN,
    .data = hub_mac_bytes,
  },
};


bool ble_is_busy() {
  return adv_start_time > 0 || phone_conn || sensor_conn || last_event_time > 0 || was_pressed;
}

int advertise_start(void) {
  if (adv_start_time > 0) {
    printk("Already advertising\n");
    return -1;
  }
  int err;
  printk("Starting to advertise\n");
  err = bt_le_scan_stop();
  if (err) {
    printk("Error stopping scan (err %d)\n", err);
  }
  err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
  if (err) {
    printk("Error starting to advertise (err %d)\n", err);
    return err;
  }
  network->set_power(true);
  adv_start_time = k_uptime_get();
  alarm_adv_counter_set();
  return 0;
}

int advertise_stop(void) {
  int err = bt_le_adv_stop();
  if (err) {
    printk("Error stopping advertisement (err %d)\n", err);
    return err;
  }
  printk("Stopped advertising after %lld seconds\n", (k_uptime_get() - adv_start_time) / 1000);
  adv_start_time = 0;
  Utilities::write_rgb(0, 0, 0);
  if (!phone_conn && !sensor_conn) {
    start_scan();
    network->set_power(false);
  }
  return 0;
}

int adv_led_interval_cb(void) {
  if (adv_start_time == 0) return -1;
  if (k_uptime_get() - adv_start_time >= ADV_DURATION_MS) {
    advertise_stop();
    return 1;
  }
  if ((k_uptime_get() / 1000) % 2 == 0) Utilities::write_rgb(75, 0, 130);
  else Utilities::write_rgb(75, 0, 80);
  alarm_adv_counter_set();
  return 0;
}


void scan_match(struct bt_scan_device_info* device_info, struct bt_scan_filter_match* filter_match, bool connectable) {
  if (last_event_time && k_uptime_get() < last_event_time + BLE_COOLDOWN_MS) {
    printk("-");
    return;
  } else last_event_time = 0;

  const bt_addr_le_t* addr_le = device_info->recv_info->addr;
  char addr_str[MAC_ADDR_LEN];
  bt_addr_le_to_str(addr_le, addr_str, sizeof(addr_str));
  addr_str[MAC_ADDR_LEN - 1] = '\0';
  printk("\t\t\t📱 Scanned MAC: %s, rssi: %d, connectable: %d\n",
    addr_str, device_info->recv_info->rssi, connectable);

  if (sensor_conn) {
    printk("Already connected to a sensor\n");
    return;
  }

  // determine if known sensor
  bool is_known_sensor = false;
  for (uint8_t i = 0; i < known_sensor_addrs_len; i++) {
    printk("Checking for a match with: %s\n", known_sensor_addrs[i]);
    if (strcmp(addr_str, known_sensor_addrs[i]) == 0) {
      is_known_sensor = true;
      break;
    }
  }

  // if we're not adding new sensors and it's unknown
  if (!is_adding_new_sensor && !is_known_sensor) {
    printk("Sensor hasn't been registered to this hub\n");
    return;
  }
  // if we're adding new sensors and it's already added
  if (is_adding_new_sensor && is_known_sensor) {
    printk("Sensor already registered to this hub\n");
    return;
  }

  // We found a Sensor!
  Utilities::write_rgb(255, 30, 0);
  printk("\nSENSOR ELIGIBLE FOR CONNECTION\n");

  int err = bt_le_scan_stop();
  if (err) {
    printk("Error stopping BLE scan (err %d)\n", err);
    return;
  }
  err = bt_conn_le_create(addr_le, create_param, conn_param, &sensor_conn);
  if (err) {
    Utilities::write_rgb(255, 0, 0);
    printk("\tError connecting to sensor 😭 Restarting scan...\n");
    k_msleep(1000L);
    start_scan();
    return;
  }

  if (is_adding_new_sensor) {
    printk("Waiting for command to connect~~~");
    // TODO Use notify
    snprintk(command_char_val, 13 + sizeof(addr_str), "SensorFound:%s", addr_str);
  }
}

void scan_error(struct bt_scan_device_info* device_info) {
  printk("scan_error\n");
}

void scan_conn(struct bt_scan_device_info* device_info, struct bt_conn* conn) {
  printk("scan_conn\n");
  sensor_conn = bt_conn_ref(conn);
}
BT_SCAN_CB_INIT(scan_cb, scan_match, NULL, scan_error, scan_conn);

static int scan_init(void) {
  int err;
  struct bt_scan_init_param scan_init_param = {
    .connect_if_match = 0,
  };

  bt_scan_init(&scan_init_param);
  bt_scan_cb_register(&scan_cb);

  err = bt_scan_filter_add(BT_SCAN_FILTER_TYPE_NAME, PERIPHERAL_NAME);
  if (err) {
    printk("Scanning filters cannot be set (err %d)\n", err);
    return err;
  }
  err = bt_scan_filter_enable(BT_SCAN_NAME_FILTER, true);
  if (err) {
    printk("Filters cannot be turned on (err %d)\n", err);
    return err;
  }

  return 0;
}

void start_scan(void)
{
  int err;

  err = bt_le_scan_start(scan_param, NULL);
  if (err) {
    printk("Scanning failed to start (err %d)\n", err);
    return;
  }
  printk("Hub scanning for peripheral...\n");
}


static void dis_discovery_completed_cb(struct bt_gatt_dm *dm, void *context);
static void bas_discovery_completed_cb(struct bt_gatt_dm *dm, void *context);

static const struct bt_gatt_dm_cb bas_discovery_cb = {
  .completed = bas_discovery_completed_cb,
};

const bt_gatt_dm_attr *batt_level_char;
const bt_gatt_dm_attr *batt_volts_char;
const bt_gatt_dm_attr *software_rev_char;
static struct bt_uuid_16 battery_svc_uuid = BT_UUID_INIT_16(BT_UUID_BAS_VAL);
static struct bt_uuid_16 device_info_uuid = BT_UUID_INIT_16(BT_UUID_DIS_VAL);

// 0x2a28 Software Revision String - value:(0x) 30-2E-31-2E-33-00 "0.1.3" recieved - response 02 12 00 28 2a
static uint8_t read_firmware_version_cb(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params,
				    const void *data, uint16_t length)
{
  printk("Checking read_firmware_version...\n");
  if (err) {
    printk("Error reading firmware version characteristic (err %d)\n", err);
    return -1;
  }
  if (length > 0) {
    const char* firmware_version = (const char*)data;
    printk("Firmware version: %s\n", firmware_version); // "0.1.3"
    strncpy(sensor_details.firmware_version, firmware_version, sizeof(sensor_details.firmware_version));
  }
  return 0;
}

// 0x2a19 Battery Level - value:(0x) 64,"d" ... "100%" recieved - response 02 35 00 18 2b
static uint8_t read_battery_level_cb(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params,
				    const void *data, uint16_t length)
{
  printk("Checking read_battery_level...\n");
  for(int i = 0; i < length; i++) {
    // print hex values of bytes in data
    printk("%02x ", ((uint8_t*)data)[i]);
  }
  printk("\n");
  if (err) {
    printk("Error reading battery level characteristic (err %d)\n", err);
    return -1;
  }
  if (length > 0) {
    uint8_t battery_level = *(uint8_t*)data;
    printk("Battery level: %u\n", battery_level);
    sensor_details.battery_level = battery_level;
  }
  return 0;
}

// 0x2b18 Voltage - "(0x) 0C-E3" recieved
static uint8_t read_battery_volts_cb(struct bt_conn *conn, uint8_t err,
				    struct bt_gatt_read_params *params,
				    const void *data, uint16_t length)
{
  printk("Checking read_battery_volts...\n");
  for(int i = 0; i < length; i++) {
    // print hex values of bytes in data
    printk("%02x ", ((uint8_t*)data)[i]);
  }
  printk("\n");
  if (err) {
    printk("Error reading battery level characteristic (err %d)\n", err);
    return -1;
  }
  if (length > 0) {
    // Assuming the data is big-endian
    const uint8_t *raw_data = (const uint8_t *)data;
    const uint16_t voltage = (raw_data[0] << 8) | raw_data[1];
    printk("Voltage is %u\n", voltage);
    sensor_details.battery_volts = voltage;
  }
  return 0;
}

struct bt_uuid *software_rev_uuid = BT_UUID_DIS_SOFTWARE_REVISION;
struct bt_uuid *batt_volts_uuid = BT_UUID_GATT_V;
struct bt_uuid *batt_level_uuid = BT_UUID_BAS_BATTERY_LEVEL;

// create params for bt_gatt_read
struct bt_gatt_read_params batt_level_read_params = {
  .func = &read_battery_level_cb,
  .handle_count = 1,
  .single = { .offset = 0 },
};
struct bt_gatt_read_params batt_volts_read_params = {
  .func = &read_battery_volts_cb,
  .handle_count = 1,
  .single = { .offset = 0 },
};
struct bt_gatt_read_params software_rev_read_params = {
  .func = &read_firmware_version_cb,
  .handle_count = 1,
  .single = { .offset = 0 },
};

static void bas_discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
  printk("Found service 180f - Battery Service UUID\n");

  batt_level_char = bt_gatt_dm_char_by_uuid(dm, batt_level_uuid);
  batt_volts_char = bt_gatt_dm_char_by_uuid(dm, batt_volts_uuid);

  if(!batt_level_char) {
    printk("Unable to find BT_UUID_BAS_BATTERY_LEVEL\n");
    return;
  }
  if(!batt_volts_char) {
    printk("Unable to find BT_UUID_BAS_BATTERY_VOLTAGE\n");
    return;
  }

  batt_level_read_params.single.handle = batt_level_char->handle + 1;
  batt_volts_read_params.single.handle = batt_volts_char->handle + 1;

  if(bt_gatt_read(sensor_conn, &batt_level_read_params)) {
    printk("Error reading batt_level_char\n");
  }
  if(bt_gatt_read(sensor_conn, &batt_volts_read_params)) {
    printk("Error reading batt_volts_char\n");
  }

  printk("Releasing DM\n");
  bt_gatt_dm_data_release(dm);
  printk("Finished service 0x180f\n");

}

static const struct bt_gatt_dm_cb dis_discovery_cb = {
  .completed = dis_discovery_completed_cb,
};


static void dis_discovery_completed_cb(struct bt_gatt_dm *dm, void *context)
{
  printk("Found service 180a - Device Information Service\n");

  
  software_rev_char = bt_gatt_dm_char_by_uuid(dm, software_rev_uuid);

  if(!software_rev_char) {
    printk("Unable to find BT_UUID_DIS_SOFTWARE_REVISION\n");
    return;
  }
  printk("Found software_rev_char...[0x%04X]\n", software_rev_char->handle);
  software_rev_read_params.single.handle = software_rev_char->handle + 1;
  
  if(bt_gatt_read(sensor_conn, &software_rev_read_params)) {
    printk("Error reading software_rev_char\n");
  }
  printk("Releasing DM\n");
  bt_gatt_dm_data_release(dm);
  printk("Finished service 0x180a\n");

}

static void handle_sensor_connected_work(struct k_work* work_item) {
  char addr[MAC_ADDR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(sensor_conn), addr, sizeof(addr));
  addr[MAC_ADDR_LEN - 1] = '\0';
  Utilities::write_rgb(255, 100, 200);
  printk("\nPeripheral connected!\n");
  
  int err;
  int start_time = k_uptime_get();
  
  if ((err = bt_gatt_dm_start(sensor_conn, &device_info_uuid.uuid, &dis_discovery_cb, NULL))) {
    printk("Could not start the discovery procedure for Device Information Service, error code: %d\n", err);
  }
  while(strlen(sensor_details.firmware_version) == 0 && k_uptime_get() < start_time + 5000LL) {
    printk("Waiting for dis discovery to complete...\n");
    k_msleep(100);
  }
  if ((err = bt_gatt_dm_start(sensor_conn, &battery_svc_uuid.uuid, &bas_discovery_cb, NULL))) {
    printk("Could not start the discovery procedure for Battery Service, error code: %d\n", err);
  }

  if (!is_adding_new_sensor) {
    is_making_network_request = true;
    int err = network_reqs->handle_send_event(addr, &sensor_details);
    is_making_network_request = false;
    if (err) {
      printk("Unable to send event\n");
      bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return;
    }
    bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    advertise_start();
  }
}

static void handle_phone_connected_work(struct k_work* work_item) {
  printk("handling phone connection work\n");
  if (bt_bas_set_battery_level(battery_read().percent)) {
    printk("Unable to write battery level char\n");
  }
  // Grab access_token from userId of connected phone
  int64_t start_time = k_uptime_get();
  printk("Trying to read userid...");
  // TODO decrease timeout
  while (strlen(command_char_val) < 7 && phone_conn && k_uptime_get() < start_time + 20000LL) {
    printk(".");
    k_msleep(100);
  }
  if (!phone_conn) return;
  if (!strlen(command_char_val)) {
    bt_conn_disconnect(phone_conn, BT_HCI_ERR_UNACCEPT_CONN_PARAM);
    return;
  }
  printk("\nUserID value: %s\n", command_char_val);

  Command command = Utilities::parse_raw_command(command_char_val);
  if (strcmp(command.type, "UserId") != 0) {
    printk("Error: command.type is not equal to UserId, command.type: %s\n", command.type);
    bt_conn_disconnect(phone_conn, BT_HCI_ERR_UNACCEPT_CONN_PARAM);
    return;
  }
  printk("command.type is UserId\n");

  uint16_t hub_id;
  size_t err_size = 210;
  char err_msg[err_size] = "";
  is_making_network_request = true;
  int err = network_reqs->handle_get_token_and_hub_id(command.value, hub_mac, &hub_id, err_msg);
  is_making_network_request = false;
  if (err) {
    printk("Unable to get token and hub_id\n");
    snprintk(command_char_val, err_size, "Error:%s", err_msg);
    return;
  }
  if (phone_conn) {
    snprintk(command_char_val, 10, "HubId:%d", hub_id++);
    printk("Changed command_char_val to %s\n", command_char_val);
    // TODO Use notify to the command char
    // err = bt_gatt_notify(NULL, &hub_svc.attrs[0], &command_char_val, sizeof(command_char_val));
    // if (err) {
    //   printk("Failed to notify (err 0x%x)\n", err);
    // }
  }
}

static void connected(struct bt_conn* conn, uint8_t err) {
  char addr[MAC_ADDR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  addr[MAC_ADDR_LEN - 1] = '\0';
  bool is_sensor = sensor_conn && sensor_conn == conn;
  if (err) {
    printk("Failed to connect to %s (err %u)\n", addr, err);
    if (is_sensor) {
      bt_conn_unref(sensor_conn);
      sensor_conn = NULL;
      start_scan();
    }
    return;
  }

  printk("\n>>> BLE Connected to %s -- MAC: %s\n", is_sensor ? "SENSOR" : "PHONE", addr);

  if (is_sensor) {
    advertise_stop();
    k_work_init(&work, handle_sensor_connected_work);
    k_work_submit_to_queue(&ble_work_q, &work);
  } else {
    phone_conn = bt_conn_ref(conn);
    advertise_stop();
    err = bt_le_scan_stop();
    if (err) {
      printk("Failed to stop scan\n");
    }
    if (!network->has_token()) {
      k_work_init(&work, handle_phone_connected_work);
      err = k_work_submit_to_queue(&ble_work_q, &work);
      if (err < 0) {
        printk("Failed to submit to queue (err 0x%x)\n", err);
      }
    }
  }
  alarm_adv_counter_cancel();
}

static void disconnected(struct bt_conn* conn, uint8_t reason) {
  char addr[MAC_ADDR_LEN];
  if (conn != sensor_conn && conn != phone_conn) {
    return;
  }
  if (!(sensor_conn && phone_conn) && !is_making_network_request) network->set_power(false);
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  addr[MAC_ADDR_LEN - 1] = '\0';
  bool is_sensor = sensor_conn && sensor_conn == conn;
  printk("\n>>> BLE Disconnecting from %s -- MAC: %s (reason 0x%02x)\n", is_sensor ? "SENSOR" : "PHONE", addr, reason);

  if (is_sensor) {
    Utilities::write_rgb(0, 0, 0);
    bt_conn_unref(sensor_conn);
    sensor_conn = NULL;
    last_event_time = k_uptime_get();
    printk("Cooling down to prevent peripheral reconnection---");
  } else {
    bt_conn_unref(phone_conn);
    phone_conn = NULL;
    is_adding_new_sensor = false;
    memset(command_char_val, 0, sizeof(command_char_val));
    if (sensor_conn) bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
  }
  if (!phone_conn) start_scan();
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
  .connected = connected,
  .disconnected = disconnected,
};

void add_known_sensor(char* addr) {
  strcpy(known_sensor_addrs[known_sensor_addrs_len], addr);
  printk("\tAdded known_sensor_addr: %s\n", known_sensor_addrs[known_sensor_addrs_len]);
  known_sensor_addrs_len++;
}

int diagnostic_trigger(void) {
  return diagnostic_run();
}

int init_ble(NetworkRequests* network_requests, Network* net) {
  network_reqs = network_requests;
  network = net;
  diagnostic_init(network_reqs, network);
  int err = bt_enable(NULL);
  if (IS_ENABLED(CONFIG_TEST)) k_msleep(100);
  if (err) {
    printk("Bluetooth init failed (err %d)\n", err);
    return err;
  } else printk("\tBLE enabled\n");

  err = scan_init();
  if (err) {
    printk("BLE scan init failed (err %d)\n", err);
  } else printk("\tBLE scan initialized\n");

  // Set hub MAC address
  size_t size = 1;
  bt_addr_le_t addrs[size];
  bt_id_get(addrs, &size);
  size = sizeof(addrs[0].a.val);
  // Store bytes backwards to make it easier to read, see declaration for + 2 explanation
  for(int i = 0; i < (uint8_t)size; i++) {
    hub_mac_bytes[i + MANU_ID_LEN] = addrs[0].a.val[size - 1 - i];
  }
  bt_addr_le_to_str(&addrs[0], hub_mac, sizeof(hub_mac));
  hub_mac[MAC_ADDR_LEN - 1] = '\0';
  printk("\tHub MAC initialized as (%s)\n", hub_mac);

  k_work_queue_start(&ble_work_q, ble_stack_area,
    K_THREAD_STACK_SIZEOF(ble_stack_area),
    CONFIG_SYSTEM_WORKQUEUE_PRIORITY + 1, &ble_work_q_config);

  err = alarm_init(&advertise_start, &adv_led_interval_cb, &diagnostic_trigger);
  if (err) {
    printk("Error during alarm_init\n");
  }
  return err;
}