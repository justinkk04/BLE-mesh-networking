#include "command.h"
#include "sensor.h"
#include "load_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static const char *TAG = "CMD";

// Same format as Pico: "D:50%,V:12.003V,I:250.00mA,P:3000.8mW"
int format_sensor_response(char *buf, size_t buf_size) {
  float voltage = ina260_read_voltage();
  float current = ina260_read_current();
  float power = fabsf(voltage * current);
  return snprintf(buf, buf_size, "D:%d%%,V:%.3fV,I:%.2fmA,P:%.1fmW",
                  get_current_duty(), voltage, current, power);
}

// Returns response length, writes response to buf
int process_command(const char *cmd, char *response, size_t resp_size) {
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

// Type commands into idf.py monitor for local testing without mesh
void console_task(void *pvParameters) {
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
