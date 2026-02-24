# How To Actually Learn This Repo

Use this repeatable loop. Do not read randomly.

## Learning Loop (45 minutes)

1. Pick one flow note (READ, scan/connect, target ALL, threshold).
2. Open matching draw.io diagram.
3. Run one real command.
4. Confirm each hop in logs/code.
5. Write 5 bullet summary in your own words.

## Week 1 plan

### Day 1

- Read:
  - `[[Concepts/Roles-Server-Client-Gateway-Explained]]`
  - `[[Concepts/Vendor-Model-Explained]]`
- Run:
  - `read` trace lab

### Day 2

- Read:
  - `[[Flows/Scan-and-Connect-By-Address-Flow]]`
  - `[[Flows/Target-Node-and-ALL-Flow]]`
- Run:
  - connect by `--address`
  - `node ALL` + `read`

### Day 3

- Read:
  - `[[ESP-IDF/ESP-GATT-Gateway]]`
- Trace:
  - command parse -> mesh send -> notify back

### Day 4

- Read:
  - `[[ESP-IDF/ESP-Sensing-Node]]`
- Trace:
  - `process_command` path

### Day 5

- Read:
  - `[[Flows/PowerManager-Flow]]`
  - `[[Labs/Hands-On-Lab-2-Trace-Threshold]]`
- Run:
  - `threshold`, `priority`, `priority off`, `threshold off`

## Rule

If confused, go back to one command and one flow only.
