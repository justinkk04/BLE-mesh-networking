/* ESP BLE Mesh Node - Direct I2C Sensor + PWM Load Control
 *
 * This node:
 * - Is provisioned by the mesh provisioner (UUID prefix 0xdd)
 * - Reads INA260 voltage/current directly via I2C (no Pico needed)
 * - Controls load PWM directly via LEDC (inverted for 2N2222->MOSFET)
 * - Acts as Generic OnOff Server + Client, relays mesh messages
 * - Responds to vendor commands synchronously (no async UART queue)
 * - Has local serial console for testing (type into idf.py monitor)
 *
 * Wiring: INA260 SDA -> ESP GPIO6
 *         INA260 SCL -> ESP GPIO7
 *         PWM out    -> ESP GPIO5 (to 2N2222 base / MOSFET gate)
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"


#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"


#include "ble_mesh_example_init.h"
#include "ble_mesh_example_nvs.h"

#define TAG "MESH_NODE"

#define CID_ESP 0x02E5

// Vendor model definitions (shared with gateway and provisioner)
#define VND_MODEL_ID_SERVER 0x0001
#define VND_OP_SEND ESP_BLE_MESH_MODEL_OP_3(0x00, CID_ESP)
#define VND_OP_STATUS ESP_BLE_MESH_MODEL_OP_3(0x01, CID_ESP)

// UUID with prefix 0xdd 0xdd for auto-provisioning
static uint8_t dev_uuid[16] = {0xdd, 0xdd};

// ============== I2C Configuration (INA260) ==============
#define I2C_PORT I2C_NUM_0
#define I2C_SDA_PIN GPIO_NUM_6 // Wire to INA260 SDA
#define I2C_SCL_PIN GPIO_NUM_7 // Wire to INA260 SCL
#define I2C_FREQ_HZ 400000
#define INA260_ADDR 0x45 // A0=GND, A1=GND (found by I2C scan) COM11 40 COM14 45

// INA260 registers
#define INA260_REG_CONFIG 0x00
#define INA260_REG_CURRENT 0x01
#define INA260_REG_VOLTAGE 0x02
#define INA260_CONFIG 0x6727 // 1024-sample averaging

// ============== PWM Configuration ==============
#define PWM_GPIO GPIO_NUM_5              // Wire to 2N2222 base / MOSFET gate
#define PWM_FREQ_HZ 1000                 // 1kHz (same as Pico code)
#define PWM_RESOLUTION LEDC_TIMER_13_BIT // 8192 steps
#define PWM_MAX_DUTY 8191                // (2^13 - 1)

static int current_duty = 0;   // 0-100%
static bool ina260_ok = false; // Set true if INA260 found on I2C

// ============== Mesh State Storage ==============
static struct mesh_node_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr; // Our unicast address
  uint8_t onoff; // Current OnOff state
  uint8_t tid;   // Transaction ID
} __attribute__((packed)) node_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .addr = 0x0000,
    .onoff = 0,
    .tid = 0,
};

static nvs_handle_t NVS_HANDLE;
static const char *NVS_KEY = "mesh_node";

// Cached indices for sending
static uint16_t cached_net_idx = 0xFFFF;
static uint16_t cached_app_idx = 0xFFFF;

// ============== Mesh Models ==============
static esp_ble_mesh_client_t onoff_client;

static esp_ble_mesh_cfg_srv_t config_server = {
    .net_transmit = ESP_BLE_MESH_TRANSMIT(3, 20),
    .relay = ESP_BLE_MESH_RELAY_ENABLED,
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(4, 20),
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 1, ROLE_NODE);
ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_srv_pub, 2 + 3, ROLE_NODE);

static esp_ble_mesh_gen_onoff_srv_t onoff_server = {
    .rsp_ctrl =
        {
            .get_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
            .set_auto_rsp = ESP_BLE_MESH_SERVER_RSP_BY_APP,
        },
};

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_SRV(&onoff_srv_pub, &onoff_server),
};

// Vendor model: receives commands from gateway, sends sensor data back
static esp_ble_mesh_model_op_t vnd_op[] = {
    ESP_BLE_MESH_MODEL_OP(VND_OP_SEND, 1),
    ESP_BLE_MESH_MODEL_OP_END,
};

static esp_ble_mesh_model_t vnd_models[] = {
    ESP_BLE_MESH_VENDOR_MODEL(CID_ESP, VND_MODEL_ID_SERVER, vnd_op, NULL, NULL),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, vnd_models),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .element_count = ARRAY_SIZE(elements),
    .elements = elements,
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

// ============== NVS Storage ==============
static void save_node_state(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_KEY, &node_state, sizeof(node_state));
}

static void restore_node_state(void) {
  esp_err_t err;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_KEY, &node_state,
                             sizeof(node_state), &exist);
  if (err == ESP_OK && exist) {
    ESP_LOGI(TAG, "Restored: net_idx=0x%04x, app_idx=0x%04x, addr=0x%04x",
             node_state.net_idx, node_state.app_idx, node_state.addr);
    cached_net_idx = node_state.net_idx;
    cached_app_idx = node_state.app_idx;
  }
}

// ============== I2C / INA260 Functions ==============
static void i2c_scan(void) {
  ESP_LOGI(TAG, "I2C bus scan:");
  int found = 0;
  for (uint8_t addr = 0x08; addr < 0x78; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "  Found device at 0x%02x", addr);
      found++;
    }
  }
  if (found == 0) {
    ESP_LOGW(TAG,
             "  No I2C devices found! Check wiring: SDA=GPIO%d, SCL=GPIO%d",
             I2C_SDA_PIN, I2C_SCL_PIN);
  }
}

static esp_err_t sensor_init(void) {
  // Initialize I2C master
  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = I2C_SDA_PIN,
      .scl_io_num = I2C_SCL_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = I2C_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_param_config(I2C_PORT, &conf));
  ESP_ERROR_CHECK(i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0));

  ESP_LOGI(TAG, "I2C initialized: SDA=GPIO%d, SCL=GPIO%d, %d Hz", I2C_SDA_PIN,
           I2C_SCL_PIN, I2C_FREQ_HZ);

  // Scan bus first
  i2c_scan();

  // Probe INA260 specifically
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret == ESP_OK) {
    ESP_LOGI(TAG, "INA260 found at 0x%02x", INA260_ADDR);
    ina260_ok = true;

    // Configure: 1024-sample averaging (same as Pico code)
    uint8_t config_data[3] = {INA260_REG_CONFIG, (INA260_CONFIG >> 8) & 0xFF,
                              INA260_CONFIG & 0xFF};
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, config_data, sizeof(config_data), true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    vTaskDelay(pdMS_TO_TICKS(200)); // Let config settle
    ESP_LOGI(TAG, "INA260 configured (1024-sample averaging)");
  } else {
    ESP_LOGE(TAG, "INA260 NOT FOUND at 0x%02x! Check wiring.", INA260_ADDR);
    ina260_ok = false;
  }

  return ESP_OK;
}

static float ina260_read_voltage(void) {
  if (!ina260_ok)
    return 0.0f;

  uint8_t reg = INA260_REG_VOLTAGE;
  uint8_t data[2] = {0};

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd); // Repeated start
  i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_READ, true);
  i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Voltage read failed: %d", ret);
    return 0.0f;
  }

  uint16_t raw = (data[0] << 8) | data[1];
  return raw * 1.25f / 1000.0f; // LSB = 1.25mV
}

static float ina260_read_current(void) {
  if (!ina260_ok)
    return 0.0f;

  uint8_t reg = INA260_REG_CURRENT;
  uint8_t data[2] = {0};

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (INA260_ADDR << 1) | I2C_MASTER_READ, true);
  i2c_master_read(cmd, data, 2, I2C_MASTER_LAST_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
  i2c_cmd_link_delete(cmd);

  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "Current read failed: %d", ret);
    return 0.0f;
  }

  int16_t raw = (int16_t)((data[0] << 8) | data[1]);
  return fabsf(raw * 1.25f); // LSB = 1.25mA, abs for polarity
}

// ============== PWM / Load Control ==============
static void pwm_init(void) {
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .duty_resolution = PWM_RESOLUTION,
      .timer_num = LEDC_TIMER_0,
      .freq_hz = PWM_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t channel = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel = LEDC_CHANNEL_0,
      .timer_sel = LEDC_TIMER_0,
      .gpio_num = PWM_GPIO,
      .duty = PWM_MAX_DUTY, // HIGH = load OFF (inverted for 2N2222)
      .hpoint = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channel));

  current_duty = 0;
  ESP_LOGI(TAG, "PWM: %d Hz on GPIO%d (inverted, load OFF)", PWM_FREQ_HZ,
           PWM_GPIO);
}

static void set_duty(int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  // Inverted: 0% load = full HIGH output, 100% load = full LOW output
  int inverted = 100 - percent;
  uint32_t duty_val = (inverted * PWM_MAX_DUTY) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_val);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  current_duty = percent;
  ESP_LOGI(TAG, "Duty set: %d%% (LEDC duty=%lu)", percent,
           (unsigned long)duty_val);
}

// ============== Sensor Response Formatting ==============
// Same format as Pico: "D:50%,V:12.003V,I:250.00mA,P:3000.8mW"
static int format_sensor_response(char *buf, size_t buf_size) {
  float voltage = ina260_read_voltage();
  float current = ina260_read_current();
  float power = fabsf(voltage * current);
  return snprintf(buf, buf_size, "D:%d%%,V:%.3fV,I:%.2fmA,P:%.1fmW",
                  current_duty, voltage, current, power);
}

// ============== Command Processing ==============
// Returns response length, writes response to buf
static int process_command(const char *cmd, char *response, size_t resp_size) {
  int len;

  if (strcmp(cmd, "s") == 0 || strcmp(cmd, "stop") == 0) {
    set_duty(0);
    len = format_sensor_response(response, resp_size);

  } else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "ramp") == 0) {
    // Ramp: step through duty levels, return final reading
    ESP_LOGI(TAG, "Ramp test starting...");
    char step_buf[128];
    for (int d = 0; d <= 100; d += 25) {
      set_duty(d);
      vTaskDelay(pdMS_TO_TICKS(500));
      format_sensor_response(step_buf, sizeof(step_buf));
      ESP_LOGI(TAG, "  Ramp %d%%: %s", d, step_buf);
    }
    set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(200));
    len = format_sensor_response(response, resp_size);
    ESP_LOGI(TAG, "Ramp complete");

  } else if (strncmp(cmd, "duty:", 5) == 0) {
    int duty_val = atoi(cmd + 5);
    set_duty(duty_val);
    len = format_sensor_response(response, resp_size);

  } else if (strcmp(cmd, "read") == 0 || strcmp(cmd, "status") == 0) {
    len = format_sensor_response(response, resp_size);

  } else {
    // Try parsing as a bare number (e.g., "50" = duty:50)
    char *endptr;
    long val = strtol(cmd, &endptr, 10);
    if (*endptr == '\0' && cmd[0] != '\0') {
      set_duty((int)val);
      len = format_sensor_response(response, resp_size);
    } else {
      len = snprintf(response, resp_size, "ERR:UNKNOWN:%s", cmd);
    }
  }

  return len;
}

// ============== Mesh Callbacks ==============
static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags,
                          uint32_t iv_index) {
  ESP_LOGI(TAG, "========== PROVISIONED ==========");
  ESP_LOGI(TAG, "NetKey index: 0x%04x", net_idx);
  ESP_LOGI(TAG, "Unicast addr: 0x%04x", addr);
  ESP_LOGI(TAG, "Flags: 0x%02x, IV: 0x%08" PRIx32, flags, iv_index);
  ESP_LOGI(TAG, "==================================");

  node_state.net_idx = net_idx;
  node_state.addr = addr;
  cached_net_idx = net_idx;
}

static void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                            esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "Mesh stack registered");
    onoff_client.model = &elements[0].sig_models[1];
    restore_node_state();
    break;

  case ESP_BLE_MESH_NODE_PROV_ENABLE_COMP_EVT:
    ESP_LOGI(TAG, "Provisioning enabled");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "Provisioning link opened (%s)",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                 ? "PB-ADV"
                 : "PB-GATT");
    break;

  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "Provisioning link closed");
    break;

  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    prov_complete(
        param->node_prov_complete.net_idx, param->node_prov_complete.addr,
        param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;

  case ESP_BLE_MESH_NODE_PROV_RESET_EVT:
    ESP_LOGW(TAG, "Node reset - reprovisioning needed");
    break;

  default:
    break;
  }
}

static void config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                             esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "AppKey added: net=0x%04x, app=0x%04x",
               param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      cached_net_idx = param->value.state_change.appkey_add.net_idx;
      cached_app_idx = param->value.state_change.appkey_add.app_idx;
      node_state.net_idx = cached_net_idx;
      node_state.app_idx = cached_app_idx;
      save_node_state();
      break;

    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG,
               "Model bound: elem=0x%04x, app=0x%04x, model=0x%04x, cid=0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.model_id,
               param->value.state_change.mod_app_bind.company_id);

      if (param->value.state_change.mod_app_bind.company_id == 0xFFFF &&
          (param->value.state_change.mod_app_bind.model_id ==
               ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI ||
           param->value.state_change.mod_app_bind.model_id ==
               ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_SRV)) {
        node_state.app_idx = param->value.state_change.mod_app_bind.app_idx;
        cached_app_idx = node_state.app_idx;
        save_node_state();
        ESP_LOGI(TAG, "Node fully configured and ready!");
      }
      // Also handle vendor model bind
      if (param->value.state_change.mod_app_bind.company_id == CID_ESP &&
          param->value.state_change.mod_app_bind.model_id ==
              VND_MODEL_ID_SERVER) {
        ESP_LOGI(TAG, "Vendor Server model bound - full command support!");
      }
      break;

    default:
      break;
    }
  }
}

// Handle incoming OnOff commands - now executes directly instead of UART
// forward
static void generic_server_cb(esp_ble_mesh_generic_server_cb_event_t event,
                              esp_ble_mesh_generic_server_cb_param_t *param) {
  ESP_LOGI(TAG, "Server event: 0x%02x, opcode: 0x%04" PRIx32 ", src: 0x%04x",
           event, param->ctx.recv_op, param->ctx.addr);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_SERVER_RECV_GET_MSG_EVT:
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      ESP_LOGI(TAG, "OnOff GET - reading sensor");
      char resp[128];
      format_sensor_response(resp, sizeof(resp));
      ESP_LOGI(TAG, "Sensor: %s", resp);
    }
    break;

  case ESP_BLE_MESH_GENERIC_SERVER_RECV_SET_MSG_EVT:
    if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET ||
        param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK) {

      uint8_t onoff = param->value.set.onoff.onoff;
      ESP_LOGI(TAG, "OnOff SET: %d from 0x%04x", onoff, param->ctx.addr);

      // Direct control: ON = set duty 100%, OFF = set duty 0%
      if (onoff) {
        set_duty(100);
      } else {
        set_duty(0);
      }

      node_state.onoff = onoff;

      if (param->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
        esp_ble_mesh_gen_onoff_srv_t *srv = param->model->user_data;
        if (srv)
          srv->state.onoff = onoff;
        esp_ble_mesh_msg_ctx_t ctx = param->ctx;
        uint8_t status_msg[1] = {onoff};
        esp_ble_mesh_server_model_send_msg(
            param->model, &ctx, ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS,
            sizeof(status_msg), status_msg);
      }

      if (cached_net_idx == 0xFFFF) {
        cached_net_idx = param->ctx.net_idx;
        cached_app_idx = param->ctx.app_idx;
      }
    }
    break;

  default:
    break;
  }
}

// Client callbacks
static void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                              esp_ble_mesh_generic_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Client event: 0x%02x, opcode: 0x%04" PRIx32, event,
           param->params->opcode);

  switch (event) {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_GET) {
      ESP_LOGI(TAG, "OnOff status: 0x%02x",
               param->status_cb.onoff_status.present_onoff);
    }
    break;

  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
    if (param->params->opcode == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET) {
      ESP_LOGI(TAG, "OnOff set confirmed: 0x%02x",
               param->status_cb.onoff_status.present_onoff);
    }
    break;

  case ESP_BLE_MESH_GENERIC_CLIENT_TIMEOUT_EVT:
    ESP_LOGW(TAG, "Client timeout");
    break;

  default:
    break;
  }
}

// ============== Vendor Model Callback ==============
// Receives commands from GATT Gateway, processes locally, responds
// synchronously
static void custom_model_cb(esp_ble_mesh_model_cb_event_t event,
                            esp_ble_mesh_model_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_MODEL_OPERATION_EVT:
    if (param->model_operation.opcode == VND_OP_SEND) {
      // Extract text command
      char cmd[64];
      uint16_t len = param->model_operation.length;
      if (len >= sizeof(cmd))
        len = sizeof(cmd) - 1;
      memcpy(cmd, param->model_operation.msg, len);
      cmd[len] = '\0';

      ESP_LOGI(TAG, "Vendor SEND from 0x%04x: %s",
               param->model_operation.ctx->addr, cmd);

      // Process command and respond synchronously (no UART, no queue!)
      char response[128];
      int resp_len = process_command(cmd, response, sizeof(response));

      // Send response back through mesh immediately
      esp_ble_mesh_msg_ctx_t ctx = *param->model_operation.ctx;
      // When message arrived via group address (0xC000), recv_dst is the
      // group addr.  The server send uses recv_dst as the reply source,
      // but we can't send FROM a group address — override with our unicast.
      if (ctx.recv_dst != node_state.addr) {
        ctx.recv_dst = node_state.addr;
      }
      esp_err_t err = esp_ble_mesh_server_model_send_msg(
          &vnd_models[0], &ctx, VND_OP_STATUS, resp_len, (uint8_t *)response);

      if (err) {
        ESP_LOGE(TAG, "Vendor STATUS send failed: %d", err);
      } else {
        ESP_LOGI(TAG, "Response -> 0x%04x: %s", ctx.addr, response);
      }
    }
    break;

  case ESP_BLE_MESH_MODEL_SEND_COMP_EVT:
    if (param->model_send_comp.err_code) {
      ESP_LOGE(TAG, "Vendor send COMP err=%d", param->model_send_comp.err_code);
    } else {
      ESP_LOGI(TAG, "Vendor send COMP OK");
    }
    break;

  default:
    break;
  }
}

// ============== Serial Console Task ==============
// Type commands into idf.py monitor for local testing without mesh
static void console_task(void *pvParameters) {
  char line[64];
  int pos = 0;

  ESP_LOGI(TAG, "");
  ESP_LOGI(TAG, "Console ready. Commands:");
  ESP_LOGI(TAG, "  read     - read INA260 voltage/current");
  ESP_LOGI(TAG, "  duty:50  - set PWM to 50%%");
  ESP_LOGI(TAG, "  50       - same as duty:50");
  ESP_LOGI(TAG, "  r        - ramp test (0->25->50->75->100%%)");
  ESP_LOGI(TAG, "  s        - stop (duty 0)");
  ESP_LOGI(TAG, "  scan     - I2C bus scan");
  ESP_LOGI(TAG, "");

  while (1) {
    int c = fgetc(stdin);
    if (c == EOF) {
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    if (c == '\n' || c == '\r') {
      if (pos > 0) {
        line[pos] = '\0';
        ESP_LOGI(TAG, "Console> %s", line);

        if (strcmp(line, "scan") == 0) {
          i2c_scan();
        } else {
          char response[128];
          process_command(line, response, sizeof(response));
          ESP_LOGI(TAG, ">> %s", response);
        }
        pos = 0;
      }
    } else if (pos < (int)sizeof(line) - 1) {
      line[pos++] = (char)c;
    }
  }
}

// ============== Mesh Initialization ==============
static esp_err_t ble_mesh_init(void) {
  esp_err_t err;

  esp_ble_mesh_register_prov_callback(provisioning_cb);
  esp_ble_mesh_register_config_server_callback(config_server_cb);
  esp_ble_mesh_register_generic_server_callback(generic_server_cb);
  esp_ble_mesh_register_generic_client_callback(generic_client_cb);
  esp_ble_mesh_register_custom_model_callback(custom_model_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Mesh init failed: %d", err);
    return err;
  }

  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Enable provisioning failed: %d", err);
    return err;
  }

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  BLE Mesh Node Ready");
  ESP_LOGI(TAG, "  UUID: %s", bt_hex(dev_uuid, 16));
  ESP_LOGI(TAG, "  Waiting for provisioner...");
  ESP_LOGI(TAG, "============================================");

  return ESP_OK;
}

// ============== Main ==============
void app_main(void) {
  esp_err_t err;

  printf("\n");
  printf("========================================\n");
  printf("  ESP32-C6 BLE Mesh Node\n");
  printf("  Direct I2C Sensor + PWM Control\n");
  printf("========================================\n\n");

  // Initialize NVS
  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Open NVS namespace
  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    ESP_LOGE(TAG, "NVS open failed");
    return;
  }

  // Initialize Bluetooth
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "Bluetooth init failed: %d", err);
    return;
  }

  // Get device UUID (keeps 0xdd prefix)
  ble_mesh_get_dev_uuid(dev_uuid);

  // Initialize I2C + INA260 sensor
  sensor_init();

  // Initialize PWM for load control
  pwm_init();

  // Initialize mesh
  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "Mesh init failed");
    return;
  }

  // Start local console for testing (type commands into idf.py monitor)
  xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

  ESP_LOGI(TAG, "============================================");
  ESP_LOGI(TAG, "  Node running - direct I2C/PWM control");
  ESP_LOGI(TAG, "  INA260: %s", ina260_ok ? "OK" : "NOT FOUND");
  ESP_LOGI(TAG, "  PWM: GPIO%d @ %d Hz", PWM_GPIO, PWM_FREQ_HZ);
  ESP_LOGI(TAG, "  Console: type 'read', 'r', 's', 'duty:50'");
  ESP_LOGI(TAG, "============================================");
}
