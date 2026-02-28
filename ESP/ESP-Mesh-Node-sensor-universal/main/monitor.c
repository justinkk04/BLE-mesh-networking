#include "monitor.h"
#include "mesh_node.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define TAG "MONITOR"

static TimerHandle_t monitor_timer = NULL;
#define MONITOR_INTERVAL_MS 1000

// ============== Monitor Mode (gateway-side polling) ==============
static void monitor_timer_cb(TimerHandle_t xTimer) {
  // Guard: skip this tick if a command send is already in-flight.
  // The timer runs on the FreeRTOS timer daemon task, while GATT commands
  // run on the NimBLE host task - both can call send_vendor_command().
  if (monitor_target_addr != 0 && vnd_bound && !monitor_waiting_response && !vnd_send_busy) {
    monitor_waiting_response = true;
    send_vendor_command(monitor_target_addr, "read", 4);
  }
}

void monitor_start(uint16_t target_addr) {
  monitor_target_addr = target_addr;
  if (monitor_timer == NULL) {
    monitor_timer = xTimerCreate("monitor", pdMS_TO_TICKS(MONITOR_INTERVAL_MS),
                                  pdTRUE, NULL, monitor_timer_cb);
  }
  xTimerStart(monitor_timer, 0);
  ESP_LOGI(TAG, "Monitor started: polling 0x%04x every %d ms",
           target_addr, MONITOR_INTERVAL_MS);
}

void monitor_stop(void) {
  if (monitor_timer != NULL) {
    xTimerStop(monitor_timer, 0);
  }
  monitor_target_addr = 0;
  monitor_waiting_response = false;
  ESP_LOGI(TAG, "Monitor stopped");
}
