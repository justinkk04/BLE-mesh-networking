"""SQLite database for sensor history and dashboard settings.

Uses WAL mode for concurrent reads from the web server while the gateway
writes sensor data. All functions are synchronous (called from asyncio
via run_in_executor when needed).
"""

import sqlite3
import time
from pathlib import Path

DB_PATH = Path(__file__).parent / "mesh_data.db"


def get_connection() -> sqlite3.Connection:
    """Get a database connection with WAL mode and row factory."""
    conn = sqlite3.connect(str(DB_PATH), check_same_thread=False)
    conn.execute("PRAGMA journal_mode=WAL")
    conn.execute("PRAGMA synchronous=NORMAL")
    conn.row_factory = sqlite3.Row
    return conn


def init_db():
    """Create tables if they don't exist."""
    conn = get_connection()
    conn.executescript("""
        CREATE TABLE IF NOT EXISTS sensor_readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp REAL NOT NULL,
            node_id TEXT NOT NULL,
            duty INTEGER,
            voltage REAL,
            current_ma REAL,
            power_mw REAL,
            commanded_duty INTEGER
        );
        CREATE INDEX IF NOT EXISTS idx_readings_node_time
            ON sensor_readings(node_id, timestamp);

        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT
        );
    """)
    conn.commit()
    conn.close()


def insert_reading(node_id: str, duty: int, voltage: float,
                   current_ma: float, power_mw: float,
                   commanded_duty: int = 0):
    """Insert a sensor reading."""
    conn = get_connection()
    conn.execute(
        "INSERT INTO sensor_readings "
        "(timestamp, node_id, duty, voltage, current_ma, power_mw, commanded_duty) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        (time.time(), node_id, duty, voltage, current_ma, power_mw, commanded_duty)
    )
    conn.commit()
    conn.close()


def get_history(node_id: str = None, minutes: int = 30,
                limit: int = 500) -> list[dict]:
    """Get historical readings, optionally filtered by node and time window."""
    conn = get_connection()
    since = time.time() - (minutes * 60)
    if node_id:
        rows = conn.execute(
            "SELECT * FROM sensor_readings "
            "WHERE node_id = ? AND timestamp > ? "
            "ORDER BY timestamp DESC LIMIT ?",
            (node_id, since, limit)
        ).fetchall()
    else:
        rows = conn.execute(
            "SELECT * FROM sensor_readings "
            "WHERE timestamp > ? "
            "ORDER BY timestamp DESC LIMIT ?",
            (since, limit)
        ).fetchall()
    conn.close()
    return [dict(r) for r in rows]


def purge_old_readings(days: int = 7):
    """Delete readings older than N days."""
    conn = get_connection()
    cutoff = time.time() - (days * 86400)
    conn.execute("DELETE FROM sensor_readings WHERE timestamp < ?", (cutoff,))
    conn.commit()
    conn.close()
