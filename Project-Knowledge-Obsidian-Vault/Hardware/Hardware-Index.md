# Hardware Index

Use this section to map firmware behavior to real wires.

## Key Hardware References

- Sensing node wiring (from code/docs):
  - I2C SDA: GPIO6
  - I2C SCL: GPIO7
  - PWM output: GPIO5
- Relay LED:
  - LED GPIO8

## Useful Repo Files

- Pinout images:
  - `Pinouts/esp32-c6-devkitm-1-pin-layout.png`
  - `Pinouts/esp32-c6-devkitc-1-pin-layout.png`
- Sensing node code:
  - `ESP/ESP-Mesh-Node-sensor-test/main/main.c`
- Relay node code:
  - `ESP/ESP-Mesh-Relay-Node/main/main.c`

## Next Step

Create your own hardware diagram in draw.io using [[Flows/Drawio-Blueprints]].
