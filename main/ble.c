#include "ble.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "esp_mac.h"
#include <string.h>

#define DEVICE_NAME "Heltec-ADSB"

/* Nordic UART Service (NUS) 128-bit UUIDs */
static const ble_uuid128_t nus_svc_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_chr_rx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t nus_chr_tx_uuid =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0,
                     0x93, 0xf3, 0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

static uint16_t nus_tx_handle;
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool notify_state = false;
static ble_rx_handler_t rx_callback = NULL;

static int nus_access_cb(uint16_t conn_h, uint16_t attr_h,
                         struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[128];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        if (rc == 0) {
            buf[len] = '\0';
            while (len > 0 && (buf[len-1] == '\r' || buf[len-1] == '\n')) {
                buf[--len] = '\0';
            }
            if (rx_callback && len > 0) {
                rx_callback(buf);
            }
        }
        return 0;
    }
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &nus_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &nus_chr_rx_uuid.u,
                .access_cb = nus_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = &nus_chr_tx_uuid.u,
                .access_cb = nus_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &nus_tx_handle,
            },
            { 0 }
        },
    },
    { 0 }
};

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            conn_handle = event->connect.conn_handle;
        } else {
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            start_advertising();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        conn_handle = BLE_HS_CONN_HANDLE_NONE;
        notify_state = false;
        start_advertising();
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == nus_tx_handle) {
            notify_state = event->subscribe.cur_notify != 0;
        }
        break;
    default:
        break;
    }
    return 0;
}

static uint8_t own_addr_type = BLE_OWN_ADDR_RANDOM;

static void start_advertising(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;

    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;

    ble_gap_adv_set_fields(&fields);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER,
                      &adv_params, gap_event_cb, NULL);
}

static void on_sync(void) {
    // Assign a dedicated static random BLE MAC for ADS-B so Android OS
    // never confuses it with MeshCore's bonded public MAC address.
    uint8_t rnd_addr[6] = {0};
    esp_read_mac(rnd_addr, ESP_MAC_BT);
    uint8_t ble_rnd[6];
    for (int i = 0; i < 6; i++) {
        ble_rnd[i] = rnd_addr[5 - i];
    }
    ble_rnd[5] |= 0xC0; // Set two MSBs for valid BLE static random address
    ble_rnd[0] ^= 0xA5; // Differentiate LSB

    ble_hs_id_set_rnd(ble_rnd);
    ble_hs_id_infer_auto(0, &own_addr_type);

    start_advertising();
}

static void ble_host_task(void *param) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ble_service_init(void) {
    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) return err;

    ble_hs_cfg.sync_cb = on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ble_svc_gap_device_name_set(DEVICE_NAME);

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    nimble_port_freertos_init(ble_host_task);
    return ESP_OK;
}

bool ble_is_connected(void) {
    return conn_handle != BLE_HS_CONN_HANDLE_NONE && notify_state;
}

void ble_send_line(const char *line) {
    if (conn_handle == BLE_HS_CONN_HANDLE_NONE || !notify_state) return;
    size_t len = strlen(line);
    size_t offset = 0;
    while (offset < len) {
        size_t chunk = len - offset;
        if (chunk > 20) chunk = 20;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(line + offset, chunk);
        if (om) {
            ble_gatts_notify_custom(conn_handle, nus_tx_handle, om);
        }
        offset += chunk;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (len == 0 || line[len-1] != '\n') {
        struct os_mbuf *om = ble_hs_mbuf_from_flat("\n", 1);
        if (om) ble_gatts_notify_custom(conn_handle, nus_tx_handle, om);
    }
}

void ble_set_rx_handler(ble_rx_handler_t handler) {
    rx_callback = handler;
}
