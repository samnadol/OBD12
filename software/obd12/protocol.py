"""Message IDs and payload layouts, mirroring firmware/OBD12/Core/Inc/Comms/protocol.h.

All multi-byte fields are little-endian.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag

PROTO_VERSION = 1


class FrameType(IntEnum):
    REQ = 0x01
    RSP = 0x02
    ERR = 0x03
    EVT = 0x04


class Cmd(IntEnum):
    PING = 0x01
    GET_INFO = 0x02
    GET_POWER = 0x03
    SET_EVENTS = 0x04
    GET_CAN_STATS = 0x05
    RESET = 0x06
    BLE_CLEAR_BONDS = 0x07
    OBD_REQUEST = 0x10
    GET_VIN = 0x11
    LIVE_START = 0x12
    LIVE_STOP = 0x13
    GET_SUPPORTED_PIDS = 0x14


class Evt(IntEnum):
    LOG = 0x80
    POWER = 0x81
    CAN_STATUS = 0x82
    LIVE_DATA = 0x83


class Topic(IntFlag):
    LOG = 0x01
    POWER = 0x02
    CAN_STATUS = 0x04


class Err(IntEnum):
    BAD_REQUEST = 0x01
    UNKNOWN_CMD = 0x02
    BUSY = 0x03
    TIMEOUT = 0x04
    NOT_AUTHORIZED = 0x05
    CAN = 0x06
    UNAVAILABLE = 0x07


class ResetCause(IntEnum):
    UNKNOWN = 0
    POWER_ON = 1
    PIN = 2
    SOFTWARE = 3
    IWDG = 4
    WWDG = 5
    LOW_POWER = 6
    FATAL = 7


class Fault(IntEnum):
    NONE = 0
    ERROR_HANDLER = 1
    HARDFAULT = 2
    MEMMANAGE = 3
    BUSFAULT = 4
    USAGEFAULT = 5
    NMI = 6


class CanState(IntEnum):
    OK = 0
    ERROR_WARNING = 1
    ERROR_PASSIVE = 2
    BUS_OFF = 3
    NOT_STARTED = 4


class InfoFlag(IntFlag):
    BLE_AVAILABLE = 0x01
    BLE_CONNECTED = 0x02
    AUTHORIZED = 0x04
    VIN_KNOWN = 0x08
    PIDS_KNOWN = 0x10


OBD_ID_BROADCAST = 0x7DF
OBD_ID_ENGINE = 0x7E0
OBD_NEGATIVE_RESPONSE = 0x7F


def _enum_or_int(enum_type, value):
    try:
        return enum_type(value)
    except ValueError:
        return value


@dataclass(frozen=True)
class Info:
    proto_version: int
    fw_version: tuple[int, int, int]
    hwconf: int
    reset_cause: ResetCause | int
    last_fault: Fault | int
    can_state: CanState | int
    last_fault_info: int
    uid: tuple[int, int, int]
    ble_mac: str
    flags: InfoFlag
    ble_passkey: int

    _FORMAT = "<8BI3I6sBBI"

    @classmethod
    def parse(cls, payload: bytes) -> "Info":
        (proto, major, minor, patch, hwconf, reset, fault, can, fault_info,
         uid0, uid1, uid2, mac, flags, _reserved, passkey) = struct.unpack(cls._FORMAT, payload)
        return cls(
            proto_version=proto,
            fw_version=(major, minor, patch),
            hwconf=hwconf,
            reset_cause=_enum_or_int(ResetCause, reset),
            last_fault=_enum_or_int(Fault, fault),
            can_state=_enum_or_int(CanState, can),
            last_fault_info=fault_info,
            uid=(uid0, uid1, uid2),
            ble_mac=":".join(f"{b:02X}" for b in mac),
            flags=InfoFlag(flags),
            ble_passkey=passkey,
        )


@dataclass(frozen=True)
class Power:
    usb_regulator_ok: bool
    can_regulator_ok: bool
    usb_regulator_mv: int
    can_regulator_mv: int

    @classmethod
    def parse(cls, payload: bytes) -> "Power":
        usb_ok, can_ok, usb_mv, can_mv = struct.unpack("<BBHH", payload)
        return cls(bool(usb_ok), bool(can_ok), usb_mv, can_mv)


@dataclass(frozen=True)
class CanStats:
    rx_frames: int
    rx_dropped: int
    tx_frames: int
    tx_failed: int
    bus_off_count: int
    transceiver_faults: int

    @classmethod
    def parse(cls, payload: bytes) -> "CanStats":
        return cls(*struct.unpack("<6I", payload))


@dataclass(frozen=True)
class ObdResponse:
    """One ECU's answer to an OBD request."""
    ecu_id: int
    message: bytes      # starts with the response SID (request SID + 0x40), or 0x7F SID NRC

    @property
    def negative(self) -> bool:
        return len(self.message) >= 3 and self.message[0] == OBD_NEGATIVE_RESPONSE

    @property
    def nrc(self) -> int | None:
        return self.message[2] if self.negative else None


def parse_obd_records(payload: bytes) -> list[ObdResponse]:
    responses = []
    pos = 0
    while pos + 3 <= len(payload):
        ecu_id = payload[pos] | (payload[pos + 1] << 8)
        length = payload[pos + 2]
        responses.append(ObdResponse(ecu_id, payload[pos + 3:pos + 3 + length]))
        pos += 3 + length
    return responses


def parse_live_records(payload: bytes) -> dict[int, bytes]:
    """EVT_LIVE_DATA payload -> {pid: data bytes (without 0x41 and the PID)}."""
    values = {}
    pos = 0
    while pos + 2 <= len(payload):
        pid, length = payload[pos], payload[pos + 1]
        values[pid] = payload[pos + 2:pos + 2 + length]
        pos += 2 + length
    return values
