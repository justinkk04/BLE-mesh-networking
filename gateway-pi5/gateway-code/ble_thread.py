"""Dedicated BLE I/O thread with persistent asyncio event loop.

Bleak on Linux/BlueZ uses D-Bus signals for GATT notifications. These
signals require a running event loop to be pumped. Textual's @work workers
create short-lived event loops that die when the worker returns, orphaning
the D-Bus signal handlers. BleThread provides a single persistent loop.
"""

import asyncio
import threading
import traceback
from typing import Optional


class BleThread:
    """Dedicated thread with a persistent asyncio event loop for bleak BLE operations."""

    def __init__(self):
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._thread: Optional[threading.Thread] = None

    def start(self):
        """Spawn the daemon thread and block until its loop is running."""
        ready = threading.Event()

        def _run():
            self._loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self._loop)
            self._loop.set_exception_handler(self._exception_handler)
            ready.set()
            self._loop.run_forever()
            self._loop.run_until_complete(self._loop.shutdown_asyncgens())
            self._loop.close()

        self._thread = threading.Thread(target=_run, daemon=True, name="ble-io")
        self._thread.start()
        ready.wait()

    def submit(self, coro) -> 'asyncio.Future':
        """Submit a coroutine to the BLE loop. Returns concurrent.futures.Future."""
        if self._loop is None:
            raise RuntimeError("BleThread not started")
        return asyncio.run_coroutine_threadsafe(coro, self._loop)

    async def submit_async(self, coro):
        """Submit a coroutine and await its result from another async context."""
        future = self.submit(coro)
        return await asyncio.wrap_future(future)

    def stop(self):
        """Stop the event loop and join the thread."""
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=5.0)
        self._loop = None
        self._thread = None

    def _exception_handler(self, loop, context):
        msg = context.get("message", "Unhandled exception in BLE thread")
        exc = context.get("exception")
        if exc:
            tb = ''.join(traceback.format_exception(type(exc), exc, exc.__traceback__))
            print(f"[BLE THREAD ERROR] {msg}\n{tb}")
        else:
            print(f"[BLE THREAD ERROR] {msg}")
