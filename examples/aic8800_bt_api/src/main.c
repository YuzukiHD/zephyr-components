/*
 * AIC8800 蓝牙：bt_enable、本地地址、广播、扫描，以及连接对端的示例。
 *
 * 没有设置 CONNECT_ADDR 时不连接任何设备，连接那一段只编译、不运行。
 */

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* 要连接的对端地址，大写，和扫描打印的一致，例如 "2C:19:5C:E0:6E:60"；空串表示不连接 */
#define CONNECT_ADDR	""

#define ADV_SECONDS	5
#define SCAN_SECONDS	10
#define MAX_DEVICES	32

/* ------------------------------------------------------------------ bt_enable */

static int enable_bluetooth(void)
{
	int err = bt_enable(NULL);

	if (err != 0) {
		printk("bt_enable failed: %d\n", err);
		return err;
	}
	printk("bt_enable ok\n");

	return 0;
}

/* ------------------------------------------------------------------ 本地地址 */

static void show_identity(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t count = ARRAY_SIZE(addrs);
	char str[BT_ADDR_LE_STR_LEN];

	bt_id_get(addrs, &count);
	for (size_t i = 0; i < count; i++) {
		bt_addr_le_to_str(&addrs[i], str, sizeof(str));
		printk("identity %u: %s\n", (unsigned int)i, str);
	}
	printk("device name: %s\n", bt_get_name());
}

/* ------------------------------------------------------------------ 广播 */

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static const struct bt_data scan_rsp_data[] = {
	BT_DATA_BYTES(BT_DATA_UUID16_ALL, BT_UUID_16_ENCODE(BT_UUID_GAP_VAL)),
};

static int start_advertising(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data),
				  scan_rsp_data, ARRAY_SIZE(scan_rsp_data));

	if (err != 0) {
		printk("bt_le_adv_start failed: %d\n", err);
		return err;
	}
	printk("advertising as \"%s\"\n", CONFIG_BT_DEVICE_NAME);

	return 0;
}

static int stop_advertising(void)
{
	int err = bt_le_adv_stop();

	if (err != 0) {
		printk("bt_le_adv_stop failed: %d\n", err);
		return err;
	}
	printk("advertising stopped\n");

	return 0;
}

/* ------------------------------------------------------------------ 扫描 */

struct dev {
	bt_addr_le_t addr;
	int8_t rssi;
	uint16_t reports;
	bool connectable;
	char name[24];
};

static struct dev devs[MAX_DEVICES];
static int ndev;
static unsigned int total;

/* bt_data_parse 对广播数据里的每个字段调用一次 */
static bool ad_field(struct bt_data *data, void *user_data)
{
	char *name = user_data;

	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		size_t n = MIN(data->data_len, sizeof(devs[0].name) - 1U);

		memcpy(name, data->data, n);
		name[n] = '\0';
	}
	return true;
}

/* 每收到一个广播报告调用一次，在蓝牙 RX 线程里 */
static void scan_report(const bt_addr_le_t *addr, int8_t rssi, uint8_t type,
			struct net_buf_simple *ad)
{
	char name[sizeof(devs[0].name)] = "";
	struct dev *d = NULL;

	total++;
	for (int i = 0; i < ndev; i++) {
		if (bt_addr_le_cmp(&devs[i].addr, addr) == 0) {
			d = &devs[i];
			break;
		}
	}
	if (d == NULL) {
		if (ndev == MAX_DEVICES) {
			return;
		}
		d = &devs[ndev++];
		d->addr = *addr;
		d->rssi = rssi;
	}

	bt_data_parse(ad, ad_field, name);
	d->reports++;
	if (rssi > d->rssi) {
		d->rssi = rssi;
	}
	if (type == BT_GAP_ADV_TYPE_ADV_IND || type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		d->connectable = true;
	}
	if (name[0] != '\0') {
		strcpy(d->name, name);
	}
}

static void print_table(void)
{
	bool used[MAX_DEVICES] = { false };

	printk("%u reports, %d devices\n", total, ndev);
	for (int n = 0; n < ndev; n++) {
		int best = -1;
		char str[BT_ADDR_LE_STR_LEN];

		for (int i = 0; i < ndev; i++) {
			if (!used[i] && (best < 0 || devs[i].rssi > devs[best].rssi)) {
				best = i;
			}
		}
		used[best] = true;
		bt_addr_le_to_str(&devs[best].addr, str, sizeof(str));
		printk("%2d  %s  rssi %4d  reports %3u  %-11s  %s\n", n + 1, str,
		       devs[best].rssi, devs[best].reports,
		       devs[best].connectable ? "connectable" : "-", devs[best].name);
	}
}

static int start_scan(void)
{
	struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err = bt_le_scan_start(&param, scan_report);

	if (err != 0) {
		printk("bt_le_scan_start failed: %d\n", err);
		return err;
	}
	printk("scanning\n");

	return 0;
}

static int stop_scan(void)
{
	int err = bt_le_scan_stop();

	if (err != 0) {
		printk("bt_le_scan_stop failed: %d\n", err);
		return err;
	}
	print_table();

	return 0;
}

/* ------------------------------------------------------------------ 连接 */

static K_SEM_DEFINE(connected_sem, 0, 1);
static K_SEM_DEFINE(disc_sem, 0, 1);
static int conn_err;

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	conn_err = err;
	k_sem_give(&connected_sem);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	printk("disconnected, reason 0x%02x\n", reason);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	const struct bt_gatt_service_val *svc;
	char uuid[BT_UUID_STR_LEN];

	if (attr == NULL) {
		k_sem_give(&disc_sem);
		return BT_GATT_ITER_STOP;
	}
	svc = attr->user_data;
	bt_uuid_to_str(svc->uuid, uuid, sizeof(uuid));
	printk("  service %s  handles 0x%04x..0x%04x\n", uuid, attr->handle, svc->end_handle);

	return BT_GATT_ITER_CONTINUE;
}

static int connect_to(const char *want)
{
	struct bt_gatt_discover_params params = {
		.uuid = NULL,
		.func = discover_cb,
		.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.type = BT_GATT_DISCOVER_PRIMARY,
	};
	struct bt_conn *conn;
	int err;

	for (int i = 0; i < ndev; i++) {
		char str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&devs[i].addr, str, sizeof(str));
		if (strncmp(str, want, 17) != 0) {
			continue;
		}
		printk("connecting to %s\n", str);
		err = bt_conn_le_create(&devs[i].addr, BT_CONN_LE_CREATE_CONN,
					BT_LE_CONN_PARAM_DEFAULT, &conn);
		if (err != 0) {
			printk("bt_conn_le_create failed: %d\n", err);
			return err;
		}
		if (k_sem_take(&connected_sem, K_SECONDS(15)) != 0) {
			printk("no connection after 15 s\n");
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			bt_conn_unref(conn);
			return -ETIMEDOUT;
		}
		if (conn_err != 0) {
			printk("connection failed, error 0x%02x\n", conn_err);
			bt_conn_unref(conn);
			return -EIO;
		}
		printk("connected, discovering services\n");
		err = bt_gatt_discover(conn, &params);
		if (err == 0) {
			k_sem_take(&disc_sem, K_SECONDS(15));
		} else {
			printk("bt_gatt_discover failed: %d\n", err);
		}
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
		k_sleep(K_SECONDS(1));
		return err;
	}
	printk("%s was not seen in the scan\n", want);

	return -ENOENT;
}

/* ------------------------------------------------------------------ main */

int main(void)
{
	printk("AIC8800 Bluetooth API demo\n");

	if (enable_bluetooth() != 0) {
		return 0;
	}
	show_identity();

	if (start_advertising() == 0) {
		k_sleep(K_SECONDS(ADV_SECONDS));
		stop_advertising();
	}

	if (start_scan() == 0) {
		k_sleep(K_SECONDS(SCAN_SECONDS));
		stop_scan();
	}

	if (CONNECT_ADDR[0] != '\0') {
		connect_to(CONNECT_ADDR);
	} else {
		printk("CONNECT_ADDR is empty, no connection tried\n");
	}
	printk("done\n");

	return 0;
}
