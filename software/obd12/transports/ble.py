"""BLE transport: the Nordic UART service, via bleak."""

from __future__ import annotations

from typing import Callable

DEVICE_NAME = "OBD12"
NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # host -> device (write)
NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"   # device -> host (notify)

# the firmware accepts writes of at most this many bytes (BLE_RX_WRITE_MAX)
MAX_WRITE = 100


class BleTransport:
    def __init__(self, address: str, pair: bool = False):
        """address: BLE address (or a bleak BLEDevice). pair: pair/bond right after connecting,
        needed for commands that change the vehicle state."""
        self.address = address
        self.pair = pair
        self._client = None

    @classmethod
    async def discover(cls, timeout: float = 10.0, pair: bool = False) -> "BleTransport":
        from bleak import BleakScanner

        device = await BleakScanner.find_device_by_filter(
            lambda d, adv: (adv.local_name or d.name) == DEVICE_NAME, timeout=timeout)
        if device is None:
            raise ConnectionError(f"no {DEVICE_NAME} found over BLE")
        return cls(device, pair=pair)

    async def open(self, on_data: Callable[[bytes], None]) -> None:
        from bleak import BleakClient

        self._client = BleakClient(self.address)
        await self._client.connect()
        if self.pair:
            await self._client.pair()
        await self._client.start_notify(NUS_TX, lambda _char, data: on_data(bytes(data)))

    async def write(self, data: bytes) -> None:
        chunk = max(20, min(self._client.mtu_size - 3, MAX_WRITE))
        for i in range(0, len(data), chunk):
            await self._client.write_gatt_char(NUS_RX, data[i:i + chunk], response=False)

    async def close(self) -> None:
        if self._client and self._client.is_connected:
            await self._client.disconnect()
