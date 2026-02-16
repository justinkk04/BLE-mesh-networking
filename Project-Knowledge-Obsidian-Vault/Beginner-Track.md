# Beginner Track (Basic Python + C)

This track is designed for your level right now.

## Rule

Do not try to read full files top-to-bottom.

Read only the functions listed in each step.

## Step 1: Learn the message path (no deep C yet)

1. [[Flows/End-to-End-READ-Flow]]
2. [[Labs/Hands-On-Lab-1-Trace-READ]]
3. [[Flows/Scan-and-Connect-By-Address-Flow]]
4. [[Flows/Target-Node-and-ALL-Flow]]

## Step 2: Learn protocol words

1. [[Protocol/Protocol-Index]]
2. [[Concepts/Why-BLE-Mesh-Hybrid-Star-Mesh]]
3. [[Concepts/Vendor-Model-Explained]]
4. [[Concepts/Addressing-Unicast-vs-Group]]
5. [[Concepts/Relay-Behavior-and-When-It-Activates]]
6. [[Concepts/Self-Healing-and-Auto-Rejoin]]
7. [[Concepts/WireGuard-Concepts]]
8. [[Cheat-Sheets/C-Basics-For-This-Repo]]
9. [[Cheat-Sheets/Python-Basics-For-This-Repo]]

## Step 3: Learn one Python class

Read only these in order:

1. `gateway-pi5/gateway.py:683` (`DCMonitorGateway`)
2. `gateway-pi5/gateway.py:760` (`notification_handler`)
3. `gateway-pi5/gateway.py:929` (`send_to_node`)

Then read [[Python-Gateway/Python-Gateway-Index]].

## Step 4: Learn one ESP file at a time

1. GATT bridge: [[ESP-IDF/ESP-GATT-Gateway]]
2. Sensing node: [[ESP-IDF/ESP-Sensing-Node]]
3. Provisioner: [[ESP-IDF/ESP-Provisioner]]
4. Relay node: [[ESP-IDF/ESP-Relay-Node]]

## Step 5: Learn auto-balancing logic

1. [[Flows/PowerManager-Flow]]
2. [[Labs/Hands-On-Lab-2-Trace-Threshold]]
3. [[Flows/RAMP-One-vs-ALL-Flow]]
4. [[Python-Gateway/Command-Feature-Reference]]

## Step 6: Visual learning with draw.io

1. [[Drawio/Drawio-Index]]
2. Open `Drawio/02-READ-Sequence.drawio`
3. Trace one live `read` command and color each step when confirmed

If anything gets confusing, go back to Step 1 and trace one packet again.
