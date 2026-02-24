# Command and Feature Reference (`gateway.py`)

## Node Control

- `node <id|ALL>`: switch target node
- `read`: one sensor read
- `status`: node status read
- `duty <0-100>`: set duty
- `ramp` / `r`: ramp test
- `stop` / `s`: stop load
- `monitor` / `m`: continuous reads
- `raw <cmd>`: raw command passthrough

Help source:

- `gateway-pi5/gateway.py:1548`

## Power Manager

- `threshold <mW>`: enable PM budget control
- `threshold off`: disable PM and restore targets
- `priority <id>`: weighted share to one node
- `priority off`: return to equal-share mode
- `power`: print PM status summary

Refs:

- set threshold: `gateway-pi5/gateway.py:173`
- set priority: `gateway-pi5/gateway.py:218`
- clear priority: `gateway-pi5/gateway.py:235`
- evaluate loop: `gateway-pi5/gateway.py:452`

## CLI startup modes

- `--scan`
- `--address <MAC>`
- `--node <id|ALL>`
- `--duty`, `--ramp`, `--stop`, `--status`, `--read`, `--monitor`
- `--no-tui`

Parser refs:

- `gateway-pi5/gateway.py:1613`

## Typical remote run command

`python gateway.py --address 98:A3:16:B1:C9:8A`

Then in interactive/TUI:

1. `node ALL`
2. `read`
3. `threshold 5000`
4. `priority 1`
