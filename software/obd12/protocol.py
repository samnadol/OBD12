"""Message IDs and payload layouts, mirroring firmware/OBD12/Core/Inc/Comms/protocol.h.

All multi-byte fields are little-endian.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from enum import IntEnum, IntFlag

PROTO_VERSION = 4


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
    DIAG_REQUEST = 0x10
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
    NOT_PERMITTED = 0x08   # diagnostic service or CAN ID the device never sends


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


class ObdAddressing(IntEnum):
    UNKNOWN = 0     # no vehicle has answered yet
    CAN_11BIT = 1
    CAN_29BIT = 2


def format_can_id(can_id: int) -> str:
    """0x7E8 -> '7E8', 0x98DAF110 -> '18DAF110'."""
    if can_id & CAN_ID_EXT:
        return f"{can_id & CAN_ID_EXT_MASK:08X}"
    return f"{can_id:03X}"


def parse_can_id(text: str) -> int:
    """'7DF' -> 0x7DF, '18DA10F1' -> 0x98DA10F1: IDs above 0x7FF are 29-bit."""
    value = int(text, 16)
    return value | CAN_ID_EXT if value > 0x7FF else value


class InfoFlag(IntFlag):
    BLE_AVAILABLE = 0x01
    BLE_CONNECTED = 0x02
    AUTHORIZED = 0x04
    VIN_KNOWN = 0x08
    PIDS_KNOWN = 0x10


# CAN IDs are u32; bit 31 marks a 29-bit ID (as in Linux SocketCAN)
CAN_ID_EXT = 0x80000000
CAN_ID_EXT_MASK = 0x1FFFFFFF

OBD_NEGATIVE_RESPONSE = 0x7F


class DiagFlag(IntFlag):
    AUTO = 0x01         # the vehicle's OBD broadcast (0x7DF or 0x18DB33F1); IDs must be 0
    FUNCTIONAL = 0x02   # several ECUs may answer: collect them until the timeout
    EXT_ADDR = 0x04     # extended addressing: tx_ext / rx_ext before the PCI in every frame


def id_bits(can_id: int) -> int:
    return CAN_ID_EXT_MASK if can_id & CAN_ID_EXT else 0x7FF


def response_id_for(request_id: int) -> int:
    """ISO 15765-4: 0x7E0 -> 0x7E8, 0x98DA10F1 -> 0x98DAF110."""
    if request_id & CAN_ID_EXT:
        return (request_id & 0xFFFF0000) | ((request_id & 0xFF) << 8) | ((request_id >> 8) & 0xFF)
    return request_id + 8


@dataclass(frozen=True)
class DiagTarget:
    """Where a DIAG_REQUEST goes and which answers belong to it."""
    tx_id: int = 0
    rx_id: int = 0
    rx_mask: int = 0            # bits of rx_id that must match; 0 = all (one ECU)
    flags: DiagFlag = DiagFlag(0)
    tx_ext: int = 0             # with DiagFlag.EXT_ADDR: address byte sent
    rx_ext: int = 0             # with DiagFlag.EXT_ADDR: address byte of the answers
    pad: int = 0x00
    timeout_ms: int = 0         # 0 = 50 ms, at most 5000

    _FORMAT = "<BIIIBBBH"

    def pack(self) -> bytes:
        return struct.pack(self._FORMAT, int(self.flags), self.tx_id, self.rx_id, self.rx_mask,
                           self.tx_ext, self.rx_ext, self.pad, self.timeout_ms)

    @classmethod
    def iso15765(cls, tx_id: int, **kwargs) -> "DiagTarget":
        """ISO 15765-4 addressing: 0x7DF / 0x98DB33F1 reach every OBD ECU; 0x7E0-0x7E7 and
        0x98DAxxF1 reach one, which answers on the matching response ID."""
        if tx_id == 0x7DF:
            return cls(tx_id, 0x7E8, 0x7F8, DiagFlag.FUNCTIONAL, **kwargs)
        if tx_id == CAN_ID_EXT | 0x18DB33F1:
            return cls(tx_id, CAN_ID_EXT | 0x18DAF100, 0x1FFFFF00, DiagFlag.FUNCTIONAL, **kwargs)
        return cls(tx_id, response_id_for(tx_id), **kwargs)


# the vehicle's OBD broadcast: the device sends it on 0x7DF or 0x18DB33F1,
# whichever addressing the vehicle uses (Info.obd_addressing)
OBD_BROADCAST = DiagTarget(flags=DiagFlag.AUTO)


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
    obd_addressing: ObdAddressing | int
    ble_passkey: int

    _FORMAT = "<8BI3I6sBBI"

    @classmethod
    def parse(cls, payload: bytes) -> "Info":
        (proto, major, minor, patch, hwconf, reset, fault, can, fault_info,
         uid0, uid1, uid2, mac, flags, addressing, passkey) = struct.unpack(cls._FORMAT, payload)
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
            obd_addressing=_enum_or_int(ObdAddressing, addressing),
            ble_passkey=passkey,
        )


@dataclass(frozen=True)
class Power:
    usb_reg_st: bool    # LM66100 status pin of the USB path
    can_reg_st: bool    # LM66100 status pin of the vehicle path
    usb_reg_out: bool   # the USB regulator output is up
    can_reg_out: bool   # the vehicle regulator output is up

    @classmethod
    def parse(cls, payload: bytes) -> "Power":
        return cls(*(bool(b) for b in struct.unpack("<BBBB", payload)))


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
class DiagResponse:
    """One ECU's answer to a diagnostic request."""
    ecu_id: int         # the CAN ID it answered on (CAN_ID_EXT set for 29-bit)
    message: bytes      # starts with the response SID (request SID + 0x40), or 0x7F SID NRC

    @property
    def negative(self) -> bool:
        return len(self.message) >= 3 and self.message[0] == OBD_NEGATIVE_RESPONSE

    @property
    def nrc(self) -> int | None:
        return self.message[2] if self.negative else None


def parse_diag_records(payload: bytes) -> list[DiagResponse]:
    """DIAG_REQUEST response: [ecu_id u32][len u16][message] per answering ECU."""
    responses = []
    pos = 0
    while pos + 6 <= len(payload):
        ecu_id, length = struct.unpack_from("<IH", payload, pos)
        responses.append(DiagResponse(ecu_id, payload[pos + 6:pos + 6 + length]))
        pos += 6 + length
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
