"""Data class tracking the state of a single mesh node."""

import time
from dataclasses import dataclass, field


@dataclass
class NodeState:
    """Tracks the last known state of a single mesh node."""
    node_id: str
    duty: int = 0              # Current duty from sensor reading
    target_duty: int = 0       # User-requested duty % (restored when threshold off)
    commanded_duty: int = 0    # Last duty % sent by PowerManager (not from sensor)
    voltage: float = 0.0       # V
    current: float = 0.0       # mA
    power: float = 0.0         # mW
    last_seen: float = field(default_factory=time.monotonic)
    responsive: bool = True
    poll_gen: int = 0          # Which poll cycle this data is from
