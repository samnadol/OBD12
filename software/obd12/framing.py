"""COBS + CRC-16 framing, mirroring firmware/OBD12/Core/Src/Comms/{cobs,crc16,comms}.c.

On the wire every frame is COBS-encoded and terminated by a 0x00 byte.
Decoded, a frame is ``[type u8][seq u8][id u8][payload][crc16 u16 LE]``.
"""

from __future__ import annotations

from dataclasses import dataclass

HEADER_LEN = 3
CRC_LEN = 2
MAX_PAYLOAD = 240
MAX_FRAME = HEADER_LEN + MAX_PAYLOAD + CRC_LEN
MAX_ENCODED = MAX_FRAME + MAX_FRAME // 254 + 1
DELIMITER = b"\x00"


def crc16(data: bytes) -> int:
    """CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if crc & 0x8000 else (crc << 1)
            crc &= 0xFFFF
    return crc


def cobs_encode(data: bytes) -> bytes:
    out = bytearray([0])
    code_idx = 0
    code = 1

    for i, byte in enumerate(data):
        if byte == 0:
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)
            continue

        out.append(byte)
        code += 1

        # a full block (254 data bytes) has no implicit zero after it
        if code == 0xFF and i + 1 < len(data):
            out[code_idx] = code
            code = 1
            code_idx = len(out)
            out.append(0)

    out[code_idx] = code
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        if code == 0 or i + code > len(data):
            raise ValueError("malformed COBS data")
        block = data[i + 1:i + code]
        if 0 in block:
            raise ValueError("malformed COBS data")
        out += block
        i += code
        if code != 0xFF and i != len(data):
            out.append(0)
    return bytes(out)


@dataclass(frozen=True)
class Frame:
    type: int
    seq: int
    id: int
    payload: bytes = b""


def encode_frame(frame: Frame) -> bytes:
    if len(frame.payload) > MAX_PAYLOAD:
        raise ValueError(f"payload too long ({len(frame.payload)} > {MAX_PAYLOAD})")
    body = bytes([frame.type, frame.seq & 0xFF, frame.id]) + frame.payload
    body += crc16(body).to_bytes(2, "little")
    return cobs_encode(body) + DELIMITER


def decode_frame(encoded: bytes) -> Frame | None:
    """Decodes one frame (without the delimiter); None if it is malformed or fails the CRC."""
    try:
        body = cobs_decode(encoded)
    except ValueError:
        return None
    if len(body) < HEADER_LEN + CRC_LEN:
        return None
    if crc16(body[:-CRC_LEN]) != int.from_bytes(body[-CRC_LEN:], "little"):
        return None
    return Frame(body[0], body[1], body[2], body[HEADER_LEN:-CRC_LEN])


class FrameDecoder:
    """Splits a byte stream into frames; resynchronises on the next delimiter after garbage."""

    def __init__(self) -> None:
        self._buf = bytearray()
        self._overflow = False

    def feed(self, data: bytes) -> list[Frame]:
        frames = []
        for byte in data:
            if byte == 0:
                if self._buf and not self._overflow:
                    frame = decode_frame(bytes(self._buf))
                    if frame is not None:
                        frames.append(frame)
                self._buf.clear()
                self._overflow = False
            elif len(self._buf) < MAX_ENCODED:
                self._buf.append(byte)
            else:
                self._overflow = True
        return frames
