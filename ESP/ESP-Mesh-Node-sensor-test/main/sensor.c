#include "sensor.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "SENSOR";

// ============== I2C Configuration (INA260) ==============
#define I2C_PORT I2C_NUM_0
#define I2C_SDA_PIN GPIO_NUM_6 // Wire to INA260 SDA
#define I2C_SCL_PIN GPIO_NUM_7 // Wire to INA260 SCL
#define I2C_FREQ_HZ 400000

// INA260 valid address range: 0x40-0x4F (depends on A0/A1 wiring)
#define INA260_ADDR_MIN 0x40
#define INA260_ADDR_MAX 0x4F

static uint8_t ina260_addr = 0; // Auto-detected at runtime

// INA260 registers
#define INA260_REG_CONFIG 0x00
#define INA260_REG_CURRENT 0x01
#define INA260_REG_VOLTAGE 0x02
#define INA260_CONFIG 0x6727 // 1024-sample averaging

static bool ina260_ok = false; // Set true if INA260 found on I2C

bool sensor_is_ready(void) {
    return ina260_ok;
}

void i2c_scan(void) {
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

esp_err_t sensor_init(void) {
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

  // Scan bus (diagnostic log)
  i2c_scan();

  // Auto-detect INA260 in the valid address range (0x40-0x4F)
  ina260_addr = 0;
  for (uint8_t addr = INA260_ADDR_MIN; addr <= INA260_ADDR_MAX; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret == ESP_OK) {
      ina260_addr = addr;
      ESP_LOGI(TAG, "INA260 auto-detected at 0x%02x", ina260_addr);
      break;
    }
  }

  if (ina260_addr != 0) {
    ina260_ok = true;

    // Configure: 1024-sample averaging (same as Pico code)
    uint8_t config_data[3] = {INA260_REG_CONFIG, (INA260_CONFIG >> 8) & 0xFF,
                              INA260_CONFIG & 0xFF};
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ina260_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, config_data, sizeof(config_data), true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    vTaskDelay(pdMS_TO_TICKS(200)); // Let config settle
    ESP_LOGI(TAG, "INA260 configured (1024-sample averaging)");
  } else {
    ESP_LOGE(TAG, "INA260 NOT FOUND in range 0x%02x-0x%02x! Check wiring.",
             INA260_ADDR_MIN, INA260_ADDR_MAX);
    ina260_ok = false;
  }

  return ESP_OK;
}

float ina260_read_voltage(void) {
  if (!ina260_ok)
    return 0.0f;

  uint8_t reg = INA260_REG_VOLTAGE;
  uint8_t data[2] = {0};

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (ina260_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd); // Repeated start
  i2c_master_write_byte(cmd, (ina260_addr << 1) | I2C_MASTER_READ, true);
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

float ina260_read_current(void) {
  if (!ina260_ok)
    return 0.0f;

  uint8_t reg = INA260_REG_CURRENT;
  uint8_t data[2] = {0};

  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (ina260_addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write_byte(cmd, reg, true);
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (ina260_addr << 1) | I2C_MASTER_READ, true);
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
