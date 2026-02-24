# Python Basics For This Repo

This is only the Python syntax you need for `gateway.py`.

## 1) Classes and methods

Main classes:

- `PowerManager` (`gateway-pi5/gateway.py:141`)
- `DCMonitorGateway` (`gateway-pi5/gateway.py:683`)

Method syntax:

```python
class DCMonitorGateway:
    async def connect_to_node(self, device):
        ...
```

## 2) `async` / `await` (most important)

BLE operations are async:

```python
await self.client.connect()
await self.client.start_notify(...)
await self.client.write_gatt_char(...)
```

Rule:

- if function is `async def`, calls to async functions need `await`.

## 3) Dataclass for node state

`NodeState` stores structured runtime values:

```python
@dataclass
class NodeState:
    node_id: str
    duty: int = 0
    voltage: float = 0.0
```

This replaces manual constructor boilerplate.

## 4) Dict and set usage

Common patterns:

```python
self.nodes: dict[str, NodeState] = {}
self.known_nodes: set[str] = set()
```

Used for:

- quickly finding node state by id
- tracking which node ids have ever responded

## 5) Parsing text with regex

Notification payload parse:

```python
SENSOR_RE = re.compile(r'D:(\d+)%,V:([\d.]+)V,I:([\d.]+)mA,P:([\d.]+)mW', re.IGNORECASE)
```

Then:

```python
sensor_match = SENSOR_RE.match(payload)
```

Where:

- `gateway-pi5/gateway.py:60`
- `gateway-pi5/gateway.py:792`

## 6) Thread sync with `threading.Event`

Used to wait for node response without blocking forever:

```python
evt = threading.Event()
self._node_events[node_id] = evt
...
evt.set()  # in notification handler
```

Where:

- wait side: `gateway-pi5/gateway.py:913`
- signal side: `gateway-pi5/gateway.py:811`

## 7) Practical read order for `gateway.py`

1. `DCMonitorGateway.__init__` (`:684`)
2. `notification_handler` (`:760`)
3. `send_to_node` (`:929`)
4. `PowerManager.set_threshold` (`:173`)
5. `PowerManager._poll_all_nodes` (`:412`)
6. `PowerManager._evaluate_and_adjust` (`:452`)

## 8) What to ignore for now

Skip these on first pass:

- Textual UI internals
- BLE thread exception handler details
- advanced PM optimization comments

## 9) Use this with labs

- `[[Labs/Hands-On-Lab-1-Trace-READ]]`
- `[[Labs/Hands-On-Lab-2-Trace-Threshold]]`
