#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/printk.h>

#include "ble.h"
#include "alarm.h"
#include "utilities.h"

#define DEVICE_NAME			CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN		(sizeof(DEVICE_NAME) - 1)

#define ADV_DURATION_MS		10 * 1000

// 1000181a-0000-1000-8000-00805f9b34fb
#define BT_UUID_SENSOR_SERVICE_VAL 0xFB349B5F80000080001000001A180010
// 10002A58-0000-1000-8000-00805f9b34fb
#define BT_UUID_VOLT_CHAR_VAL 0xFB349B5F8000008000100000582A0010

// TODO Use battery service provided by nordic
// const char* BATTERY_SERVICE_UUID = "0000180f-0000-1000-8000-00805f9b34fb";
// const char* BATTERY_LEVEL_CHARACTERISTIC_UUID = "00002a19-0000-1000-8000-00805f9b34fb";

// 0000181a-0000-1000-8000-00805f9b34fc
#define BT_UUID_HUB_SERVICE_VAL 0xFC349B5F80000080001000001A180000
// 00002A58-0000-1000-8000-00805f9b34fd
#define BT_UUID_COMMAND_CHAR_VAL 0xFD349B5F8000008000100000582A0000
// 00002A58-0000-1000-8000-00805f9b34fe
#define BT_UUID_TRANSFER_CHAR_VAL 0xFE349B5F8000008000100000582A0000
// TODO Use OTA DFU service
// const char* FIRMWARE_CHARACTERISTIC_UUID = "2A26";

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
BT_DATA_BYTES(BT_DATA_UUID128_ALL,
		0x84, 0xaa, 0x60, 0x74, 0x52, 0x8a, 0x8b, 0x86,
		0xd3, 0x4c, 0xb7, 0x1d, 0x1d, 0xdc, 0x53, 0x8d),
};

struct work_info {
	struct k_work work;
	char name[25];
} ble_work;

// Have to declare here to avoid "taking address of temporary array" error
const struct bt_le_adv_param* adv_param = BT_LE_ADV_CONN;
const struct bt_le_scan_param* scan_param = BT_LE_SCAN_PASSIVE;

int64_t adv_start_time;

int advertise_start(void) {
	if (adv_start_time > 0) {
		printk("Already advertising\n");
		return -1;
	}
	int err = bt_le_adv_start(adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err != 0) {
		printk("Error starting to advertise\n");
		return err;
	}
	printk("Starting to advertise\n");
	adv_start_time = k_uptime_get();
	alarm_adv_counter_set();
	return 0;
}

int advertise_stop(void) {
	int err = bt_le_adv_stop();
	if (err != 0) {
		printk("Error stopping advertisement\n");
		return err;
	}
	printk("Stopped advertising after %lld seconds\n", (k_uptime_get() - adv_start_time) / 1000);
	adv_start_time = 0;
	Utilities::rgb_write(0, 0, 0);
	return 0;
}

int adv_led_interval_cb(void) {
	if (adv_start_time == 0) return -1;
	if (k_uptime_get() - adv_start_time >= ADV_DURATION_MS) {
		advertise_stop();
		return 1;
	}
	if ((k_uptime_get() / 1000) % 2 == 0) Utilities::rgb_write(75, 0, 130);
	else Utilities::rgb_write(75, 0, 80, true);
	alarm_adv_counter_set();
	return 0;
}

int init_ble(void) {
	int err = bt_enable(NULL);
	if (err != 0) {
		printk("Bluetooth init failed (err %d)\n", err);
		return err;
	} else printk("\tBLE online\n");

	err = alarm_init(&advertise_start, &adv_led_interval_cb);
	if (err != 0) {
		printk("Error during alarm_init\n");
	}
	return err;
}

static void device_found(const bt_addr_le_t* addr, int8_t rssi, uint8_t type, struct net_buf_simple* ad) {
	// no-op
	printk("Device found\n");
}

void start_scan(void)
{
	int err;

	err = bt_le_scan_start(scan_param, device_found);
	if (err) {
		printk("Scanning failed to start (err %d)\n", err);
		return;
	}
	printk("Scanning successfully started\n");
}

static void connected(struct bt_conn* conn, uint8_t err) {
	if (err) {
		printk("Connection failed (err %u)\n", err);
		return;
	}
	alarm_adv_counter_cancel();
	advertise_stop();

	printk("Connected\n");
}

static void disconnected(struct bt_conn* conn, uint8_t reason) {
	printk("Disconnected (reason %u)\n", reason);
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
	.connected = connected,
	.disconnected = disconnected,
};