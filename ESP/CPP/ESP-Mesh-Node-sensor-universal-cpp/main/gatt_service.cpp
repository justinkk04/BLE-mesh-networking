#include <string.h>

#include "gatt_service.h"
#include "command_parser.h"

#include "esp_log.h"

// NimBLE GATT includes
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

namespace {
    constexpr char TAG[] = "GATT";

    uint16_t sensor_char_val_handle;
    char     sensor_data[SENSOR_DATA_MAX_LEN] = "NODE_READY";
    uint16_t sensor_data_len                  = 10;
}

uint16_t gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;

// ============== GATT Notify Function ==============
// MTU through BLE Mesh GATT Proxy is hard-limited to 23 (20 bytes payload).
// Messages > 20 bytes are split into chunks:
//   - Continuation chunks: '+' prefix + 19 bytes data
//   - Final (or only) chunk: no prefix, up to 20 bytes
// Pi 5 reassembles by accumulating '+'-prefixed chunks.

void gatt_notify_sensor_data(const char *data, uint16_t len) {
  if (gatt_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
    ESP_LOGW(TAG, "GATT notify skipped (no connection): %.*s", len, data);
    return;
  }

  if (len >= SENSOR_DATA_MAX_LEN)
    len = SENSOR_DATA_MAX_LEN - 1;
  memcpy(sensor_data, data, len);
  sensor_data[len] = '\0';
  sensor_data_len = len;

  if (len <= GATT_MAX_PAYLOAD) {
    // Fits in a single notification
    struct os_mbuf *om = ble_hs_mbuf_from_flat(sensor_data, len);
    if (om) {
      int rc = ble_gatts_notify_custom(gatt_conn_handle, sensor_char_val_handle, om);
      if (rc == 0) {
        ESP_LOGI(TAG, "GATT notify (%d bytes): %s", len, sensor_data);
      } else {
        ESP_LOGW(TAG, "GATT notify failed (rc=%d), clearing conn_handle", rc);
        gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
      }
    }
  } else {
    // Split into chunked notifications
    uint16_t offset = 0;
    int chunk_num = 0;
    while (offset < len) {
      uint8_t chunk[GATT_MAX_PAYLOAD];
      uint16_t chunk_len;
      uint16_t remaining = len - offset;

      if (remaining > GATT_MAX_PAYLOAD) {
        // Continuation chunk: '+' prefix + 19 bytes of data
        chunk[0] = '+';
        uint16_t data_in_chunk = GATT_MAX_PAYLOAD - 1;
        memcpy(chunk + 1, sensor_data + offset, data_in_chunk);
        chunk_len = GATT_MAX_PAYLOAD;
        offset += data_in_chunk;
      } else {
        // Final chunk: no prefix, just data
        memcpy(chunk, sensor_data + offset, remaining);
        chunk_len = remaining;
        offset += remaining;
      }

      struct os_mbuf *om = ble_hs_mbuf_from_flat(chunk, chunk_len);
      if (om) {
        int rc = ble_gatts_notify_custom(gatt_conn_handle, sensor_char_val_handle, om);
        if (rc != 0) {
          ESP_LOGW(TAG, "GATT chunk %d notify failed (rc=%d)", chunk_num, rc);
          gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
          return;
        }
      }
      chunk_num++;
    }
    ESP_LOGI(TAG, "GATT notify chunked (%d chunks, %d bytes): %s",
             chunk_num, len, sensor_data);
  }
}

// ============== GATT Callbacks ==============
static int sensor_data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                 struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    // Capture connection handle from proxy connection (GAP event may not fire)
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        gatt_conn_handle == BLE_HS_CONN_HANDLE_NONE) {
      gatt_conn_handle = conn_handle;
      ESP_LOGI(TAG, "GATT conn_handle captured from read: %d", conn_handle);
    }
    int rc = os_mbuf_append(ctxt->om, sensor_data, sensor_data_len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
  }
  return 0;
}

static int command_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                             struct ble_gatt_access_ctxt *ctxt, void *arg) {
  if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
    // Capture connection handle from proxy connection (GAP event may not fire)
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      if (gatt_conn_handle != conn_handle) {
        gatt_conn_handle = conn_handle;
        ESP_LOGI(TAG, "GATT conn_handle captured from write: %d", conn_handle);
      }
    }

    uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
    char buf[COMMAND_MAX_LEN + 1];

    if (len > COMMAND_MAX_LEN)
      len = COMMAND_MAX_LEN;

    int rc = ble_hs_mbuf_to_flat(ctxt->om, buf, len, nullptr);
    if (rc != 0)
      return BLE_ATT_ERR_UNLIKELY;

    buf[len] = '\0';

    // Process command from Pi 5
    process_gatt_command(buf, len);
  }
  return 0;
}

// BLE_UUID16_DECLARE takes the address of a C99 compound literal — invalid in C++.
// Declare persistent static objects and use &obj.u as the uuid pointer instead.
static ble_uuid16_t uuid_svc         = BLE_UUID16_INIT(DC_MONITOR_SERVICE_UUID);
static ble_uuid16_t uuid_sensor_data = BLE_UUID16_INIT(SENSOR_DATA_CHAR_UUID);
static ble_uuid16_t uuid_command     = BLE_UUID16_INIT(COMMAND_CHAR_UUID);

// Characteristics array defined separately (NimBLE writes val_handle at registration).
// Fields must be in ble_gatt_chr_def declaration order for C++20 designated init:
// uuid, access_cb, arg, descriptors, flags, min_key_size, val_handle, cpfd
static struct ble_gatt_chr_def dc_monitor_chars[] = {
    {
        .uuid       = &uuid_sensor_data.u,
        .access_cb  = sensor_data_access_cb,
        .flags      = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
        .val_handle = &sensor_char_val_handle,
    },
    {
        .uuid      = &uuid_command.u,
        .access_cb = command_access_cb,
        .flags     = BLE_GATT_CHR_F_WRITE,
    },
    {0}, // terminator
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &uuid_svc.u,
        .characteristics = dc_monitor_chars,
    },
    {0}, // terminator
};

static int ble_gap_event(struct ble_gap_event *event, void *arg) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      gatt_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Pi 5 connected!");
      gatt_notify_sensor_data("GATEWAY_CONNECTED", 17);
    } else {
      gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Pi 5 disconnected");
    gatt_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    // Restart advertising
    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, nullptr,
                      ble_gap_event, nullptr);
    break;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(TAG, "Pi 5 subscribed to notifications (handle=%d)",
             event->subscribe.conn_handle);
    // Also capture handle from subscribe event (proxy connection fallback)
    if (event->subscribe.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      gatt_conn_handle = event->subscribe.conn_handle;
    }
    break;

  case BLE_GAP_EVENT_MTU:
    ESP_LOGI(TAG, "MTU updated: conn_handle=%d, mtu=%d",
             event->mtu.conn_handle, event->mtu.value);
    break;
  }
  return 0;
}

static void gatt_advertise(void) {
  struct ble_gap_adv_params adv_params = {};
  struct ble_hs_adv_fields fields      = {};

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  const char *name = "DC-Monitor";
  fields.name = reinterpret_cast<const uint8_t *>(name);
  fields.name_len = static_cast<uint8_t>(strlen(name));
  fields.name_is_complete = 1;

  // Compound literal (invalid C++) replaced with a named local array
  static const ble_uuid16_t svc_uuid_list[] = {BLE_UUID16_INIT(DC_MONITOR_SERVICE_UUID)};
  fields.uuids16 = svc_uuid_list;
  fields.num_uuids16 = 1;
  fields.uuids16_is_complete = 1;

  ble_gap_adv_set_fields(&fields);

  adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

  ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv_params,
                    ble_gap_event, nullptr);

  ESP_LOGI(TAG, "GATT advertising as '%s'", name);
}

esp_err_t gatt_register_services(void) {
  // Register custom GATT services BEFORE mesh init locks the GATT table
  // bluetooth_init() has already initialized NimBLE and the GAP/GATT stack

  // Set preferred ATT MTU so sensor data isn't truncated (default 23 = 20 payload)
  int rc_mtu = ble_att_set_preferred_mtu(185);
  ESP_LOGI(TAG, "Set preferred MTU=185, rc=%d", rc_mtu);

  // Set device name
  ble_svc_gap_device_name_set("DC-Monitor");

  int rc = ble_gatts_count_cfg(gatt_svr_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT count failed: %d", rc);
    return ESP_FAIL;
  }

  rc = ble_gatts_add_svcs(gatt_svr_svcs);
  if (rc != 0) {
    ESP_LOGE(TAG, "GATT add failed: %d", rc);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "GATT services registered");
  return ESP_OK;
}

void gatt_start_advertising(void) {
  // Start advertising AFTER mesh init (called from app_main)
  gatt_advertise();
  ESP_LOGI(TAG, "GATT advertising started");
}
