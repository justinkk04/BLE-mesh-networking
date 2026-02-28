"""Shared constants for the DC Monitor Gateway."""

import re

# Custom UUIDs matching ESP32-C6 ble_service.h
DC_MONITOR_SERVICE_UUID = "0000dc01-0000-1000-8000-00805f9b34fb"
SENSOR_DATA_CHAR_UUID = "0000dc02-0000-1000-8000-00805f9b34fb"
COMMAND_CHAR_UUID = "0000dc03-0000-1000-8000-00805f9b34fb"

# Device name prefixes to look for
# Before provisioning: "Mesh-Gateway" (custom GATT advert)
# After provisioning: "ESP-BLE-MESH" (mesh GATT proxy advert)
# Universal node (v0.7.0): "DC-Monitor" (sensor + gateway node)
DEVICE_NAME_PREFIXES = ["Mesh-Gateway", "DC-Monitor", "ESP-BLE-MESH"]

# Sensor data parsing regex (case-insensitive for mA/mW/MA/MW)
SENSOR_RE = re.compile(r'D:(\d+)%,V:([\d.]+)V,I:([\d.]+)mA,P:([\d.]+)mW', re.IGNORECASE)
NODE_ID_RE = re.compile(r'NODE(\d+)', re.IGNORECASE)
