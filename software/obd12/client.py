"""Transport-independent asyncio client for the OBD12 protocol."""

from __future__ import annotations

import asyncio
import contextlib
import struct
from typing import Callable, Iterable, Protocol

from .framing import Frame, FrameDecoder, encode_frame
from .pids import pids_from_bitmap
from .protocol import (
    OBD_ID_BROADCAST, CanStats, CanState, Cmd, Err, Evt, FrameType, Info, ObdResponse, Power, Topic,
    parse_live_records, parse_obd_records,
)

DEFAULT_TIMEOUT = 2.0

# the firmware drops a live subscription after 30 s without requests
LIVE_KEEPALIVE_INTERVAL = 10.0


class Transport(Protocol):
    """A byte pipe to the device. Received bytes are delivered to the callback
    passed to open(), on the event loop thread."""

    async def open(self, on_data: Callable[[bytes], None]) -> None: ...
    async def write(self, data: bytes) -> None: ...
    async def close(self) -> None: ...


class DeviceError(Exception):
    def __init__(self, cmd: int, code: int):
        self.cmd = cmd
        try:
            self.code: Err | int = Err(code)
        except ValueError:
            self.code = code
        name = self.code.name if isinstance(self.code, Err) else f"0x{code:02x}"
        super().__init__(f"command 0x{cmd:02x} failed: {name}")


class Device:
    """
    async with Device(UsbTransport.discover()) as dev:
        print(await dev.get_info())
    """

    def __init__(self, transport: Transport):
        self.transport = transport
        self._decoder = FrameDecoder()
        self._seq = 0
        self._pending: dict[int, tuple[int, asyncio.Future]] = {}
        self._event_handlers: dict[int, list[Callable[[bytes], None]]] = {}
        self._keepalive: asyncio.Task | None = None

    async def __aenter__(self) -> "Device":
        await self.open()
        return self

    async def __aexit__(self, *exc) -> None:
        await self.close()

    async def open(self) -> None:
        await self.transport.open(self._on_data)

    async def close(self) -> None:
        if self._keepalive:
            self._keepalive.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._keepalive
            self._keepalive = None
        for _, future in self._pending.values():
            future.cancel()
        self._pending.clear()
        await self.transport.close()

    # --- events ---

    def on(self, evt: Evt, handler: Callable[[bytes], None]) -> None:
        """Registers handler(payload) for an event type."""
        self._event_handlers.setdefault(int(evt), []).append(handler)

    def on_log(self, handler: Callable[[str], None]) -> None:
        self.on(Evt.LOG, lambda p: handler(p.decode("utf-8", "replace")))

    def on_power(self, handler: Callable[[Power], None]) -> None:
        self.on(Evt.POWER, lambda p: handler(Power.parse(p)))

    def on_can_status(self, handler: Callable[[CanState | int], None]) -> None:
        def wrap(p: bytes) -> None:
            try:
                handler(CanState(p[0]))
            except ValueError:
                handler(p[0])
        self.on(Evt.CAN_STATUS, wrap)

    def on_live_data(self, handler: Callable[[dict[int, bytes]], None]) -> None:
        self.on(Evt.LIVE_DATA, lambda p: handler(parse_live_records(p)))

    # --- requests ---

    async def request(self, cmd: Cmd | int, payload: bytes = b"", timeout: float = DEFAULT_TIMEOUT) -> bytes:
        """Sends a request and returns the response payload; raises DeviceError or TimeoutError."""
        seq = self._next_seq()
        future = asyncio.get_running_loop().create_future()
        self._pending[seq] = (int(cmd), future)
        try:
            await self.transport.write(encode_frame(Frame(FrameType.REQ, seq, int(cmd), payload)))
            return await asyncio.wait_for(future, timeout)
        finally:
            self._pending.pop(seq, None)

    async def ping(self, data: bytes = b"") -> bytes:
        return await self.request(Cmd.PING, data)

    async def get_info(self) -> Info:
        return Info.parse(await self.request(Cmd.GET_INFO))

    async def get_power(self) -> Power:
        return Power.parse(await self.request(Cmd.GET_POWER))

    async def set_events(self, topics: Topic) -> None:
        await self.request(Cmd.SET_EVENTS, bytes([int(topics)]))

    async def get_can_stats(self) -> CanStats:
        return CanStats.parse(await self.request(Cmd.GET_CAN_STATS))

    async def reset(self) -> None:
        await self.request(Cmd.RESET)

    async def ble_clear_bonds(self) -> None:
        await self.request(Cmd.BLE_CLEAR_BONDS)

    async def obd_request(self, data: bytes, dest: int = OBD_ID_BROADCAST, timeout: float = 7.0) -> list[ObdResponse]:
        """Sends a raw OBD request (service ID + parameters, 1-7 bytes); returns every ECU's answer.

        The default timeout allows for ECUs that answer "response pending" (up to 5 s)."""
        payload = struct.pack("<H", dest) + bytes(data)
        return parse_obd_records(await self.request(Cmd.OBD_REQUEST, payload, timeout))

    async def get_vin(self) -> str:
        return (await self.request(Cmd.GET_VIN, timeout=7.0)).decode("ascii", "replace")

    async def get_supported_pids(self) -> list[int]:
        """Mode 01 PIDs the vehicle supports, sorted, including the range PIDs 0x20, 0x40, ..."""
        try:
            return pids_from_bitmap(await self.request(Cmd.GET_SUPPORTED_PIDS, timeout=7.0))
        except DeviceError as e:
            if e.code != Err.UNKNOWN_CMD:
                raise

        # firmware before 0.3.0: ask the ECUs for PIDs 0x00, 0x20, ... directly
        bitmap = bytearray(32)
        for start in range(0x00, 0xE1, 0x20):
            try:
                responses = await self.obd_request(bytes([0x01, start]))
            except DeviceError as e:
                if start == 0x00 or e.code != Err.TIMEOUT:
                    raise
                break
            for r in responses:
                if len(r.message) >= 6 and r.message[0] == 0x41 and r.message[1] == start:
                    for i in range(4):
                        bitmap[start // 8 + i] |= r.message[2 + i]
            if not bitmap[start // 8 + 3] & 0x01:
                break
        return pids_from_bitmap(bytes(bitmap))

    async def live_start(self, pids: Iterable[int], interval_ms: int = 500) -> None:
        """Starts live polling of mode 01 PIDs; results arrive via on_live_data().

        Keeps the subscription alive with periodic pings until live_stop()."""
        pids = bytes(pids)
        await self.request(Cmd.LIVE_START, struct.pack("<H", interval_ms) + pids)
        if self._keepalive is None:
            self._keepalive = asyncio.create_task(self._keepalive_loop())

    async def live_stop(self) -> None:
        if self._keepalive:
            self._keepalive.cancel()
            self._keepalive = None
        await self.request(Cmd.LIVE_STOP)

    # --- internals ---

    def _next_seq(self) -> int:
        for _ in range(256):
            self._seq = (self._seq + 1) & 0xFF
            if self._seq not in self._pending:
                return self._seq
        raise RuntimeError("too many requests in flight")

    async def _keepalive_loop(self) -> None:
        while True:
            await asyncio.sleep(LIVE_KEEPALIVE_INTERVAL)
            with contextlib.suppress(asyncio.TimeoutError, DeviceError):
                await self.ping()

    def _on_data(self, data: bytes) -> None:
        for frame in self._decoder.feed(data):
            self._on_frame(frame)

    def _on_frame(self, frame: Frame) -> None:
        if frame.type == FrameType.EVT:
            for handler in self._event_handlers.get(frame.id, []):
                handler(frame.payload)
            return

        pending = self._pending.get(frame.seq)
        if pending is None:
            return
        cmd, future = pending
        if frame.id != cmd or future.done():
            return

        if frame.type == FrameType.RSP:
            future.set_result(frame.payload)
        elif frame.type == FrameType.ERR:
            future.set_exception(DeviceError(cmd, frame.payload[0] if frame.payload else 0))
