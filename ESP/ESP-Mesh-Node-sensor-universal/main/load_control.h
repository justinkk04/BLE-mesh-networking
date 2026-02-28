#ifndef LOAD_CONTROL_H
#define LOAD_CONTROL_H

// Initialize LEDC PWM for load control. Starts with load OFF (0%).
void pwm_init(void);

// Set load duty cycle (0-100%). Clamped to valid range.
void set_duty(int percent);

// Get current duty cycle percentage (0-100%).
int get_current_duty(void);

#endif // LOAD_CONTROL_H
