# Draw.io Blueprints

Use this note to build diagrams quickly in draw.io.

Ready-made editable files:

1. `Drawio/01-System-Architecture.drawio`
2. `Drawio/02-READ-Sequence.drawio`
3. `Drawio/03-Threshold-PowerManager.drawio`
4. `Drawio/04-Scan-Connect-By-Address.drawio`
5. `Drawio/05-Target-Node-and-ALL-Dispatch.drawio`
6. `Drawio/06-WireGuard-SSH-Remote-Control.drawio`
7. `Drawio/07-Provisioning-Lifecycle.drawio`
8. `Drawio/08-Relay-Out-of-Range.drawio`
9. `Drawio/09-RAMP-One-vs-ALL.drawio`
10. `Drawio/10-Power-Loss-Auto-Rejoin.drawio`

## Blueprint 1: System Architecture

Nodes:

1. Pi 5 gateway.py
2. ESP GATT Gateway
3. ESP Mesh Sensing Node 1
4. ESP Mesh Sensing Node 2
5. ESP Relay Node
6. Provisioner

Edges:

1. Pi 5 -> ESP GATT Gateway (GATT write/read notify)
2. ESP GATT Gateway -> Sensing Node 1 (vendor mesh send)
3. ESP GATT Gateway -> Sensing Node 2 (vendor mesh send)
4. Relay Node <-> Sensing Node 2 (mesh forwarding path)
5. Provisioner -> all ESP nodes (provision/config bind/subscription)

## Blueprint 2: One READ Flow

Swimlanes:

1. Pi gateway.py
2. ESP GATT Gateway
3. ESP Sensing Node

Sequence blocks:

1. `1:READ` command write
2. parse + map to `read`
3. vendor send (`VND_OP_SEND`)
4. process command + sensor read
5. vendor response (`VND_OP_STATUS`)
6. `NODE1:DATA:...` GATT notify
7. notification_handler parse/update

## Blueprint 3: threshold Flow

Blocks:

1. threshold set
2. poll all (`ALL:READ`)
3. wait responses
4. compute budget diff
5. equal or priority balance
6. send node duty changes
7. repeat loop
