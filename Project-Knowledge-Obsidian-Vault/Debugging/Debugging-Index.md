# Debugging Index

Use this when commands fail or behavior is confusing.

## Fast Triage Order

1. Pi side command send:
   - `gateway-pi5/gateway.py:929`
2. GATT gateway parse/send:
   - `ESP/ESP_GATT_BLE_Gateway/main/main.c:563`
   - `ESP/ESP_GATT_BLE_Gateway/main/main.c:353`
3. Sensing node receive/execute:
   - `ESP/ESP-Mesh-Node-sensor-test/main/main.c:597`
   - `ESP/ESP-Mesh-Node-sensor-test/main/main.c:363`
4. Response forwarding:
   - `ESP/ESP_GATT_BLE_Gateway/main/main.c:403`
   - `gateway-pi5/gateway.py:760`

## Common Failure Buckets

- Provisioning/key/bind issue (check provisioner logs)
- Node did not receive vendor command
- Node responded but gateway did not forward notify
- Pi received notify but parse/update failed
- PM stale node or timeout behavior (expected in some cases)

## Companion Notes

- [[Concepts/Provisioning-Explained]]
- [[Flows/End-to-End-READ-Flow]]
- [[Labs/Hands-On-Lab-1-Trace-READ]]
