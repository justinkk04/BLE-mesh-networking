#include "load_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "LOAD_CTRL";

// ============== PWM Configuration ==============
#define PWM_GPIO GPIO_NUM_5              // Wire to 4.7k then 2N2222 base / MOSFET gate
#define PWM_FREQ_HZ 1000                 // 1kHz (same as Pico code)
#define PWM_RESOLUTION LEDC_TIMER_13_BIT // 8192 steps
#define PWM_MAX_DUTY 8191                // (2^13 - 1)

static int current_duty = 0;   // 0-100%

int get_current_duty(void) {
    return current_duty;
}

void pwm_init(void) {
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

void set_duty(int percent) {
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
