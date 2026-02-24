# ESP Sensing Node Map

File: `ESP/ESP-Mesh-Node-sensor-test/main/main.c`

## Role

This is the node that actually:

- reads INA260 over I2C
- controls load duty via PWM
- answers vendor commands

## Key Functions

- Sensor init: `sensor_init()` at `:204`
- PWM init: `pwm_init()` at `:311`
- Sensor format string: `format_sensor_response()` at `:353`
- Command engine: `process_command()` at `:363`
- Vendor receive/send callback: `custom_model_cb()` at `:597`

## How It Handles Commands

`custom_model_cb()` receives text command (`read`, `duty:50`, `r`, `s`) then:

1. runs `process_command()`
2. builds response string
3. sends `VND_OP_STATUS` back

This is synchronous (no Pico UART bridge now), which is why it is more stable.

## Read This First

1. `process_command()`
2. `format_sensor_response()`
3. `custom_model_cb()`
