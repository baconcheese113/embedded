#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <bluetooth/scan.h>
#include <zephyr/bluetooth/services/bas.h>
#include <zephyr/sys/printk.h>

// DFU OTA
#include <mgmt/mcumgr/smp_bt.h>
#include <os_mgmt/os_mgmt.h>
#include <img_mgmt/img_mgmt.h>
#include "version.h"

#include "ble.h"
#include "battery.h"
#include "alarm.h"
#include "utilities.h"
#include "network_requests.h"

#define DEVICE_NAME			  CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

#define PERIPHERAL_NAME	"HandleIt Client"

#define ADV_DURATION_MS		30 * 1000

#define BT_UUID_SENSOR_SERVICE_VAL  BT_UUID_128_ENCODE(0x1000181a, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_VOLT_CHAR_VAL       BT_UUID_128_ENCODE(0x10002A58, 0x0000, 0x1000, 0x8000, 0x00805f9b34fb)
#define BT_UUID_HUB_SERVICE_VAL      BT_UUID_128_ENCODE(0x0000181a, 0x0000, 0x1000, 0x8000, 0x00805f9b34fc)

static struct bt_uuid_128 hub_svc_uuid = BT_UUID_INIT_128(BT_UUID_HUB_SERVICE_VAL);
static struct bt_uuid_128 command_char_uuid = BT_UUID_INIT_128(BT_UUID_128_ENCODE(0x00002A58, 0x0000, 0x1000, 0x8000, 0x00805f9b34fd));
#define FIRMWARE_VERSION_CHAR   BT_UUID_DIS_FIRMWARE_REVISION

struct k_work work;

// Don't change these during discovery!
static char command_char_val[30];
static char version[] = VERSION;

static int cur_id = 0;
static ssize_t read_command_char(struct bt_conn* conn, const struct bt_gatt_attr* attr,
  void* buf, uint16_t len, uint16_t offset)
{
  const char* value = (const char*)attr->user_data;
  printk("Read command attempt\n");

  // TODO remove
  snprintk(command_char_val, 10, "UserId:%d", cur_id++);

  return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

static ssize_t write_command_char(struct bt_conn* conn, const struct bt_gatt_attr* attr,
  const void* buf, uint16_t len, uint16_t offset,
  uint8_t flags)
{
  uint8_t* value = (uint8_t*)attr->user_data;

  if (offset + len > 30) {
    return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
  }

  memcpy(value + offset, buf, len);
  value[offset + len] = 0;

  printk("\nWrite command attempt: %s and %s\n", (char*)value, (char*)buf);

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
    BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
    BT_GATT_PERM_READ | BT_GATT_PERM_WRITE,
    read_command_char, write_command_char, command_char_val),
  BT_GATT_CHARACTERISTIC(FIRMWARE_VERSION_CHAR,
    BT_GATT_CHRC_READ,
    BT_GATT_PERM_READ,
    read_version_char, NULL, version),
  );

static const struct bt_data ad[] = {
  BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
  BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
BT_DATA_BYTES(BT_DATA_UUID128_SOME, BT_UUID_HUB_SERVICE_VAL),
};

static struct bt_conn* phone_conn;
static struct bt_conn* sensor_conn;

struct work_info {
  struct k_work work;
  char name[25];
} ble_work;

// Have to declare here to avoid "taking address of temporary array" error
const struct bt_le_adv_param* adv_param = BT_LE_ADV_CONN;
const struct bt_le_scan_param* scan_param = BT_LE_SCAN_ACTIVE;
const struct bt_conn_le_create_param* create_param = BT_CONN_LE_CREATE_CONN;
const struct bt_le_conn_param* conn_param = BT_LE_CONN_PARAM_DEFAULT;

static NetworkRequests* network_reqs;

int64_t adv_start_time;
bool is_adding_new_sensor = false;

// 10 available address slots
char known_sensor_addrs[10][50];
uint8_t known_sensor_addrs_len;

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
  if (!phone_conn && !sensor_conn) start_scan();
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
  const bt_addr_le_t* addr_le = device_info->recv_info->addr;
  // TODO only get MAC, not full string
  char addr_str[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(addr_le, addr_str, sizeof(addr_str));
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
  // TODO only connect under certain conditions
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

void register_event() {
  // TODO stop scanning until cooldown complete and start advertising
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

static void handle_sensor_connected_work(struct k_work* work_item) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(sensor_conn), addr, sizeof(addr));
  Utilities::write_rgb(255, 100, 200);
  printk("\nPeripheral connected!\n");
  // TODO print information about ATT

  if (!is_adding_new_sensor) {
    int err = network_reqs->handle_send_event(addr);
    if (err) {
      printk("Unable to send event\n");
      bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
      return;
    }
    bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    advertise_start();
    printk("Cooling down to prevent peripheral reconnection---\n");
    // TODO handle cooldown
    // lastEventTime = epochMillis();
    // lastScanTime = lastEventTime + BLE_COOLDOWN;
  } else {
    int err = network_reqs->handle_add_new_sensor(addr);
    if (err) {
      printk("Unable to add sensor\n");
    }
    add_known_sensor(addr);
    bt_conn_disconnect(sensor_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    if (phone_conn) {
      // TODO Use notify
      strcpy(command_char_val, "SensorAdded:1");
    }
  }
}

static void handle_phone_connected_work(struct k_work* work_item) {
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
    printk("Error: command.type is not equal to UserId, command.type: %s", command.type);
    bt_conn_disconnect(phone_conn, BT_HCI_ERR_UNACCEPT_CONN_PARAM);
    return;
  }
  printk("command.type is UserId");

  uint16_t hub_id;
  int err = network_reqs->handle_get_token_and_hub_id(command.value, &hub_id);
  if (err) {
    printk("Unable to get token and hub_id\n");
  }
}

static void connected(struct bt_conn* conn, uint8_t err) {
  char addr[BT_ADDR_LE_STR_LEN];
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
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
    k_work_submit(&work);
  } else {
    phone_conn = bt_conn_ref(conn);
    advertise_stop();
    err = bt_le_scan_stop();
    if (err) {
      printk("Failed to stop scan\n");
    }
    k_work_init(&work, handle_phone_connected_work);
    k_work_submit(&work);
  }
  alarm_adv_counter_cancel();
}

static void disconnected(struct bt_conn* conn, uint8_t reason) {
  char addr[BT_ADDR_LE_STR_LEN];
  if (conn != sensor_conn && conn != phone_conn) {

    return;
  }
  bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
  bool is_sensor = sensor_conn && sensor_conn == conn;
  printk("\n>>> BLE Disconnecting from %s -- MAC: %s (reason 0x%02x)\n", is_sensor ? "SENSOR" : "PHONE", addr, reason);

  if (is_sensor) {
    Utilities::write_rgb(0, 0, 0);
    bt_conn_unref(sensor_conn);
    sensor_conn = NULL;
  } else {
    bt_conn_unref(phone_conn);
    phone_conn = NULL;
    is_adding_new_sensor = false;
    memset(command_char_val, 0, sizeof(command_char_val));
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

int init_ble(NetworkRequests* network_requests) {
  network_reqs = network_requests;
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


  // DFU OTA
  os_mgmt_register_group();
  img_mgmt_register_group();
  smp_bt_register();

  err = alarm_init(&advertise_start, &adv_led_interval_cb);
  if (err) {
    printk("Error during alarm_init\n");
  }
  return err;
}