/**
 * BLE Service Implementation for DC Monitor Node
 * NimBLE GATT server with sensor data + command characteristics
 */

#include "ble_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include <string.h>

static const char *TAG = "BLE_SVC";

// Current sensor data buffer
static char sensor_data[SENSOR_DATA_MAX_LEN] = "D:0%,V:0.000,I:0.00,P:0.0";
static uint16_t sensor_data_len = 0;

// Command callback
static command_callback_t cmd_callback = NULL;

// BLE connection handle for notifications
static uint16_t conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t sensor_char_val_handle;

// Forward declarations
static int sensor_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg);
static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg);

// GATT service definition
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(DC_MONITOR_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                // Sensor Data characteristic - Read + Notify
                .uuid = BLE_UUID16_DECLARE(SENSOR_DATA_CHAR_UUID),
                .access_cb = sensor_data_access_cb,
                .val_handle = &sensor_char_val_handle,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {
                // Command characteristic - Write
                .uuid = BLE_UUID16_DECLARE(COMMAND_CHAR_UUID),
                .access_cb = command_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
            },
            { 0 }, // End of characteristics
        },
    },
    { 0 }, // End of services
};

// Sensor data read callback
static int sensor_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                  struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        int rc = os_mbuf_append(ctxt->om, sensor_data, sensor_data_len);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

// Command write callback
static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
        char buf[COMMAND_MAX_LEN + 1];
        
        if (len > COMMAND_MAX_LEN) len = COMMAND_MAX_LEN;
        
        int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, NULL);
        if (rc != 0) return BLE_ATT_ERR_UNLIKELY;
        
        buf[len] = '\0';
        
        ESP_LOGI(TAG, "Command from gateway: %s", buf);
        
        // Forward to registered callback
        if (cmd_callback) {
            cmd_callback(buf, len);
        }
    }
    return 0;
}

// GAP event handler
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Gateway connected!");
            } else {
                ESP_LOGI(TAG, "Connection failed, restarting advertising");
                conn_handle = BLE_HS_CONN_HANDLE_NONE;
                ble_service_init();
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Gateway disconnected, restarting advertising");
            conn_handle = BLE_HS_CONN_HANDLE_NONE;
            ble_service_init();
            break;
            
        case BLE_GAP_EVENT_SUBSCRIBE:
            if (event->subscribe.attr_handle == sensor_char_val_handle) {
                ESP_LOGI(TAG, "Gateway subscribed to sensor notifications");
            }
            break;
    }
    return 0;
}

// Start advertising
static void ble_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    
    memset(&fields, 0, sizeof(fields));
    
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    
    fields.uuids16 = (ble_uuid16_t[]){ BLE_UUID16_INIT(DC_MONITOR_SERVICE_UUID) };
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    
    ble_gap_adv_set_fields(&fields);
    
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, 
                       &adv_params, ble_gap_event, NULL);
    
    ESP_LOGI(TAG, "Advertising started as '%s'", name);
}

static void ble_on_sync(void) {
    ESP_LOGI(TAG, "BLE stack synchronized");
    ble_advertise();
}

static void ble_host_task(void *param) {
    ESP_LOGI(TAG, "BLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_service_init(void) {
    esp_err_t ret;
    
    ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init NimBLE: %d", ret);
        return;
    }
    
    ble_svc_gap_init();
    ble_svc_gatt_init();
    
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT count failed: %d", rc);
        return;
    }
    
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "GATT add services failed: %d", rc);
        return;
    }
    
    ble_hs_cfg.sync_cb = ble_on_sync;
    
    nimble_port_freertos_init(ble_host_task);
    
    ESP_LOGI(TAG, "BLE service initialized");
}

void ble_update_sensor_data(const char *data, uint16_t len) {
    if (len >= SENSOR_DATA_MAX_LEN) len = SENSOR_DATA_MAX_LEN - 1;
    
    memcpy(sensor_data, data, len);
    sensor_data[len] = '\0';
    sensor_data_len = len;
    
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(sensor_data, sensor_data_len);
        if (om) {
            ble_gatts_notify_custom(conn_handle, sensor_char_val_handle, om);
        }
    }
}

void ble_set_command_callback(command_callback_t cb) {
    cmd_callback = cb;
}
