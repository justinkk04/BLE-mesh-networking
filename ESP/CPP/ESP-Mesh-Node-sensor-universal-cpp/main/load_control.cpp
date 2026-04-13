#include "load_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

// ============== PWM Configuration ==============
namespace {
    constexpr char TAG[]          = "LOAD_CTRL";
    constexpr gpio_num_t PWM_GPIO = GPIO_NUM_5; // Wire to 4.7k then 2N2222 base / MOSFET gate
    constexpr int  PWM_FREQ_HZ    = 1000;        // 1kHz (same as Pico code)
    constexpr ledc_timer_bit_t PWM_RESOLUTION = LEDC_TIMER_13_BIT; // 8192 steps
    constexpr uint32_t PWM_MAX_DUTY = 8191;      // (2^13 - 1)

    int current_duty = 0; // 0-100%
}

int get_current_duty(void) {
    return current_duty;
}

void pwm_init(void) {
  ledc_timer_config_t timer = {
      .speed_mode      = LEDC_LOW_SPEED_MODE,
      .duty_resolution = PWM_RESOLUTION,
      .timer_num       = LEDC_TIMER_0,
      .freq_hz         = static_cast<uint32_t>(PWM_FREQ_HZ),
      .clk_cfg         = LEDC_AUTO_CLK,
  };
  ESP_ERROR_CHECK(ledc_timer_config(&timer));

  ledc_channel_config_t channel = {
      .gpio_num   = static_cast<int>(PWM_GPIO),
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .channel    = LEDC_CHANNEL_0,
      .intr_type  = LEDC_INTR_DISABLE,
      .timer_sel  = LEDC_TIMER_0,
      .duty       = 0, // LOW = load OFF at boot
      .hpoint     = 0,
  };
  ESP_ERROR_CHECK(ledc_channel_config(&channel));

  current_duty = 0;
  ESP_LOGI(TAG, "PWM: %d Hz on GPIO%d (load OFF at boot)", PWM_FREQ_HZ,
           static_cast<int>(PWM_GPIO));
}

void set_duty(int percent) {
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;
  // Direct: 0% = fully OFF, 100% = fully ON
  uint32_t duty_val = (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty_val);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
  current_duty = percent;
  ESP_LOGI(TAG, "Duty set: %d%% (LEDC duty=%lu)", percent,
           static_cast<unsigned long>(duty_val));
}
