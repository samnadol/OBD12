"""USB transport: the FT230X serial port, read by a background thread."""

from __future__ import annotations

import asyncio
import threading
from typing import Callable

FTDI_VID = 0x0403
FT230X_PID = 0x6015
SERIAL_PREFIX = "OBD12_"
BAUDRATE = 115200


def find_ports() -> list[str]:
    """Serial ports of connected OBD12 boards (FT230X with an OBD12_ serial number)."""
    import serial.tools.list_ports

    return [
        p.device for p in serial.tools.list_ports.comports()
        if p.vid == FTDI_VID and p.pid == FT230X_PID and (p.serial_number or "").startswith(SERIAL_PREFIX)
    ]


class UsbTransport:
    def __init__(self, port: str):
        self.port = port
        self._serial = None
        self._thread: threading.Thread | None = None
        self._running = False

    @classmethod
    def discover(cls) -> "UsbTransport":
        ports = find_ports()
        if not ports:
            raise ConnectionError("no OBD12 found on USB (FT230X serial number must start with OBD12_)")
        return cls(ports[0])

    async def open(self, on_data: Callable[[bytes], None]) -> None:
        import serial

        loop = asyncio.get_running_loop()
        self._serial = serial.Serial(self.port, BAUDRATE, timeout=0.1)
        self._running = True

        def reader() -> None:
            while self._running:
                try:
                    data = self._serial.read(self._serial.in_waiting or 1)
                except serial.SerialException:
                    break
                if data:
                    loop.call_soon_threadsafe(on_data, data)

        self._thread = threading.Thread(target=reader, name="obd12-usb-reader", daemon=True)
        self._thread.start()

    async def write(self, data: bytes) -> None:
        await asyncio.to_thread(self._serial.write, data)

    async def close(self) -> None:
        self._running = False
        if self._thread:
            await asyncio.to_thread(self._thread.join, 1.0)
        if self._serial:
            self._serial.close()
