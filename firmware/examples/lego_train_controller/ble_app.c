#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <limits.h>
#include "drv_device.h"
#include "bflb_platform.h"
#include "bl702_glb.h"
#include "hal_common.h"
#include "hal_gpio.h"

#include "bluetooth.h"
#include "conn.h"
#include "gatt.h"
#include "hci_core.h"
#include "hci_driver.h"
#include "ble_lib_api.h"
#include "bl702_sec_eng.h"
#include "ring_buffer.h"
#include "gatt.h"

#define TO_BLE_INTERVAL(x)  ((x) * 0.625)

#define DEVICE_WRITE_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00070002, 0x0745, 0x4650, 0x8d93, 0xdf59be2fc10a))
#define DEVICE_READ_CHAR BT_UUID_DECLARE_128(BT_UUID_128_ENCODE(0x00070001, 0x0745, 0x4650, 0x8d93, 0xdf59be2fc10a))
#define SCAN_CODE       "SCAN"
#define RESP_OK_CODE    "OK"
#define SCAN_CMD_LENGTH 10

#define BLE_ADV_MOTOR_CODE              0x3456

#define BLE_STATUS_FOUND_ADDRESS        0x01
#define BLE_STATUS_CONNECTED            0x02
#define BLE_STATUS_DISCOVERED_CHAR      0x04
#define BLE_STATUS_DISCOVERED_DES       0x08
#define BLE_STATUS_SUBCRIBED            0x10
#define BLE_STATUS_DEVICE_CONFIRMED     0x20
#define BLE_STATUS_BT_PRESSED           0x40

#define BUTTON_PIN                      28

static struct bt_conn *ble_bl_conn = NULL;
static TaskHandle_t cur_tsk;
static bt_addr_le_t dev_addr;
static struct bt_gatt_discover_params discover_params;
static struct bt_gatt_subscribe_params subscribe_params;
static uint16_t wr_hdl = 0;
static uint16_t rd_hdl = 0;
static uint16_t ccc_hdl = 0;
static uint8_t adv_buf[sizeof(uint16_t) + sizeof(uint8_t)];

static u8_t notify_func(struct bt_conn *conn,
                        struct bt_gatt_subscribe_params *params,
                        const void *data, u16_t length)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    if (!params->value) {
        MSG("Unsubscribed\r\n");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    if (length == 0) {
        xTaskNotifyFromISR(cur_tsk, BLE_STATUS_SUBCRIBED, eSetBits, &xHigherPriorityTaskWoken);
    }

    if ((length == (sizeof(RESP_OK_CODE) - 1)) &&
            (!memcmp(data, RESP_OK_CODE, sizeof(RESP_OK_CODE) - 1))) {
        xTaskNotifyFromISR(cur_tsk, BLE_STATUS_DEVICE_CONFIRMED, eSetBits, &xHigherPriorityTaskWoken);
    }

    return BT_GATT_ITER_CONTINUE;
}

static void write_func(struct bt_conn *conn, u8_t err,
                       struct bt_gatt_write_params *params)
{
    MSG("Write complete: err %u \r\n", err);
}

static int ble_subscribe(void)
{
    if (!ble_bl_conn) {
        MSG("Not connected\r\n");
        return -1;
    }

    subscribe_params.ccc_handle = ccc_hdl;
    subscribe_params.value_handle = rd_hdl;
    subscribe_params.value = 1;
    subscribe_params.notify = notify_func;

    int err = bt_gatt_subscribe(ble_bl_conn, &subscribe_params);

    if (err) {
        MSG("Subscribe failed (err %d)\r\n", err);
    } else {
        MSG("Subscribed\r\n");
    }

    return err;
}
#include "uuid.h"
static uint8_t ble_discover_func(struct bt_conn *conn, const struct bt_gatt_attr *attr, struct bt_gatt_discover_params *params)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    struct bt_gatt_chrc *gatt_chrc;
    char str[37];

    if (!attr) {
        MSG("Discover complete\r\n");
        if (ccc_hdl) {
            xTaskNotifyFromISR(cur_tsk, BLE_STATUS_DISCOVERED_DES, eSetBits, &xHigherPriorityTaskWoken);
        } else {
            xTaskNotifyFromISR(cur_tsk, BLE_STATUS_DISCOVERED_CHAR, eSetBits, &xHigherPriorityTaskWoken);
        }
        return BT_GATT_ITER_STOP;
    }

    if (params == NULL) {
        MSG("ble_discover_func_PARAMS\r\n");
    }

    switch (params->type) {
        case BT_GATT_DISCOVER_PRIMARY:
            break;

        case BT_GATT_DISCOVER_SECONDARY:
            break;

        case BT_GATT_DISCOVER_CHARACTERISTIC:
            gatt_chrc = attr->user_data;
            bt_uuid_to_str(gatt_chrc->uuid, str, sizeof(str));

            if (!bt_uuid_cmp(gatt_chrc->uuid, DEVICE_WRITE_CHAR)) {
                wr_hdl = gatt_chrc->value_handle;
            } else if (!bt_uuid_cmp(gatt_chrc->uuid, DEVICE_READ_CHAR)) {
                rd_hdl = gatt_chrc->value_handle;
            }

            break;

        case BT_GATT_DISCOVER_INCLUDE:
            break;

        case BT_GATT_DISCOVER_DESCRIPTOR:
            if (!bt_uuid_cmp(attr->uuid, BT_UUID_GATT_CCC)) {
                ccc_hdl = attr->handle;
            }
            break;

        default:
            break;
    }

    return BT_GATT_ITER_CONTINUE;
}

static int ble_discover(u8_t type, u16_t start_handle)
{
    int err;

    if (!ble_bl_conn) {
        return -1;
    }

    discover_params.func = ble_discover_func;
    discover_params.start_handle = start_handle;
    discover_params.end_handle = 0xffff;
    discover_params.type = type;
    discover_params.uuid = NULL;

    err = bt_gatt_discover(ble_bl_conn, &discover_params);

    if (err) {
        MSG("Discover failed (err %d)\r\n", err);
    } else {
        MSG("Discover pending\r\n");
    }

    return err;
}

static void bl_connected(struct bt_conn *conn, uint8_t err)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    int tx_octets = 0x00fb;
    int tx_time = 0x0848;

	if (err) {

	} else {
        ble_bl_conn = conn;

        bt_le_set_data_len(ble_bl_conn, tx_octets, tx_time);

        xTaskNotifyFromISR(cur_tsk, BLE_STATUS_CONNECTED, eSetBits, &xHigherPriorityTaskWoken);
	}
}

static void bl_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ble_bl_conn = NULL;
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_SVC_DATA16, adv_buf, sizeof(adv_buf)),
    
};

static struct bt_conn_cb conn_callbacks = {
	.connected = bl_connected,
	.disconnected = bl_disconnected,
};

void bt_enable_cb(int err)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    bt_addr_le_t adv_addr;
    char str[100] = {0};
    
    if (!err) {
        bt_get_local_public_address(&adv_addr);
        sprintf(str, "lego_train_ctrl_%02X%02X", adv_addr.a.val[0], adv_addr.a.val[1]);
        
        bt_set_name(str);
        bt_conn_cb_register(&conn_callbacks);

        xTaskNotifyFromISR(cur_tsk, 0, eNoAction, &xHigherPriorityTaskWoken);
    }
}

void ble_app_init(void)
{
    cur_tsk = xTaskGetCurrentTaskHandle();

    GLB_Set_EM_Sel(GLB_EM_8KB);
    ble_controller_init(configMAX_PRIORITIES - 1);
    // Initialize BLE Host stack
    hci_driver_init();

    bt_enable(bt_enable_cb);

    xTaskNotifyWait(0, ULONG_MAX, NULL, portMAX_DELAY);
}

static bool data_cb(struct bt_data *data, void *user_data)
{
#define NAME_LEN 30

    char *name = user_data;
    u8_t len;

    switch (data->type) {
        case BT_DATA_NAME_SHORTENED:
        case BT_DATA_NAME_COMPLETE:
            len = (data->data_len > NAME_LEN - 1) ? (NAME_LEN - 1) : (data->data_len);
            memcpy(name, data->data, len);
            return false;
        default:
            return true;
    }
}

static void device_found(const bt_addr_le_t *addr, s8_t rssi, u8_t evtype,
                         struct net_buf_simple *buf)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    char le_addr[BT_ADDR_LE_STR_LEN];
    char name[30];

    (void)memset(name, 0, sizeof(name));
    bt_data_parse(buf, data_cb, name);
    bt_addr_le_to_str(addr, le_addr, sizeof(le_addr));

    MSG("[DEVICE]: %s, AD evt type %u, RSSI %i %s\r\n", le_addr, evtype, rssi, name);

    if (strstr(name, "lego_train_")) {
        bt_le_scan_stop();
        dev_addr = *addr;
        xTaskNotifyFromISR(cur_tsk, BLE_STATUS_FOUND_ADDRESS, eSetBits, &xHigherPriorityTaskWoken);
    }
}

void ble_app_find_device(void)
{
    int is_found_dev = 0;
    int err;
    struct bt_gatt_write_params write_params;
    uint8_t buf[SCAN_CMD_LENGTH];
    bt_addr_le_t adv_addr;
    TickType_t timeout;
    uint32_t find_status;
    struct bt_le_scan_param scan_param = {
        .type = BT_LE_SCAN_TYPE_ACTIVE,
        .filter_dup = BT_LE_SCAN_FILTER_DUPLICATE,
        .interval = BT_GAP_SCAN_FAST_INTERVAL,
        .window = BT_GAP_SCAN_FAST_WINDOW,
    };
    bt_le_scan_start(&scan_param, device_found);
    timeout = portMAX_DELAY;

    while (is_found_dev == 0) {
        if (xTaskNotifyWait(0, ULONG_MAX, &find_status, timeout) == pdTRUE) {
            if (find_status & BLE_STATUS_FOUND_ADDRESS) {
                struct bt_conn *conn;
                struct bt_le_conn_param param = {
                    .interval_min = BT_GAP_INIT_CONN_INT_MIN,
                    .interval_max = BT_GAP_INIT_CONN_INT_MAX,
                    .latency = 0,
                    .timeout = 400,
                };

                conn = bt_conn_create_le(&dev_addr, &param);

                if (!conn) {
                    MSG("Connection failed\r\n");
                    timeout = portMAX_DELAY;
                } else {
                    MSG("Connection pending\r\n");
                    timeout = pdMS_TO_TICKS(20000);
                }
            }

            if (find_status & BLE_STATUS_CONNECTED) {
                wr_hdl = 0;
                rd_hdl = 0;
                ccc_hdl = 0;
                //discover after connected.
                if (ble_discover(BT_GATT_DISCOVER_CHARACTERISTIC, 0x0001)) {
                    timeout = portMAX_DELAY;
                }
            }

            if (find_status & BLE_STATUS_DISCOVERED_CHAR) {
                //discover after connected.
                if ((wr_hdl == 0) || (rd_hdl == 0) || (ble_discover(BT_GATT_DISCOVER_DESCRIPTOR, 0x0001))) {
                    timeout = portMAX_DELAY;
                }
            }

            if (find_status & BLE_STATUS_DISCOVERED_DES) {
                if ((ccc_hdl == 0) || (ble_subscribe())) {
                    timeout = portMAX_DELAY;
                }
            }

            if (find_status & BLE_STATUS_SUBCRIBED) {
                bt_get_local_public_address(&adv_addr);
                memcpy(buf, SCAN_CODE, sizeof(SCAN_CODE) - 1);
                memcpy(&buf[sizeof(SCAN_CODE) - 1], adv_addr.a.val, 6);

                memset(&write_params, 0, sizeof(write_params));
                write_params.data = buf;
                write_params.length = SCAN_CMD_LENGTH;
                write_params.handle = wr_hdl;
                write_params.func = write_func;
                write_params.offset = 0;

                err = bt_gatt_write(ble_bl_conn, &write_params);

                if (err) {
                    MSG("Write failed with err %d\r\n", err);
                    timeout = portMAX_DELAY;
                } else {
                    MSG("Write success\r\n");
                }
            }

            if (find_status & BLE_STATUS_DEVICE_CONFIRMED) {
                MSG("Device confirmed\r\n");
                is_found_dev = 1;
                bt_conn_disconnect(ble_bl_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                bt_conn_unref(ble_bl_conn);
            }

            if (timeout == portMAX_DELAY) {
                if (ble_bl_conn) {
                    bt_conn_disconnect(ble_bl_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                    bt_conn_unref(ble_bl_conn);
                }
                bt_le_scan_start(&scan_param, device_found);
            }
        } else {
            if (ble_bl_conn) {
                bt_conn_disconnect(ble_bl_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
                bt_conn_unref(ble_bl_conn);
            }
            bt_le_scan_start(&scan_param, device_found);
            timeout = portMAX_DELAY;
        }
    }
}

static void bt_press(uint32_t pin)
{
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(cur_tsk, BLE_STATUS_BT_PRESSED, eSetBits, &xHigherPriorityTaskWoken);
}

void ble_app_process(void)
{
    struct bt_le_adv_param adv_param = {
        .options = BT_LE_ADV_OPT_USE_NAME,
        .interval_min = BT_GAP_ADV_FAST_INT_MIN_1 / 2,
        .interval_max = BT_GAP_ADV_FAST_INT_MAX_1 / 2
    };
    uint32_t status;

    adv_buf[0] = BLE_ADV_MOTOR_CODE & 0xFF;
    adv_buf[1] = BLE_ADV_MOTOR_CODE >> 8;
    adv_buf[2] = 0x00;
    bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), NULL, 0);

    gpio_set_mode(BUTTON_PIN, GPIO_SYNC_RISING_TRIGER_INT_MODE);
    gpio_attach_irq(BUTTON_PIN, bt_press);
    gpio_irq_enable(BUTTON_PIN, ENABLE);

    while (1) {
        xTaskNotifyWait(0, ULONG_MAX, &status, portMAX_DELAY);

        if (status & BLE_STATUS_BT_PRESSED) {
            if ((adv_buf[2] & 0x01) == 0) {
                adv_buf[2] |= 0x01;
            } else if ((adv_buf[2] & 0x02) == 0) {
                adv_buf[2] |= 0x02;
            } else if (adv_buf[2] & 0x03) {
                adv_buf[2] = 0;
            } else {
                adv_buf[2] = 0;
            }

            bt_le_adv_update_data(
                    ad,
                    ARRAY_SIZE(ad),
                    NULL,
                    0);
        }
    }
}