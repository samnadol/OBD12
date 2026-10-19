"""Decoding of common SAE J1979 mode 01 PIDs."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable


@dataclass(frozen=True)
class Pid:
    pid: int
    name: str
    unit: str
    length: int
    decode: Callable[[bytes], float]


def _a(d: bytes) -> int:
    return d[0]


def _ab(d: bytes) -> int:
    return (d[0] << 8) | d[1]


PIDS: dict[int, Pid] = {p.pid: p for p in [
    Pid(0x04, "engine_load", "%", 1, lambda d: _a(d) * 100 / 255),
    Pid(0x05, "coolant_temp", "°C", 1, lambda d: _a(d) - 40),
    Pid(0x06, "short_term_fuel_trim_1", "%", 1, lambda d: (_a(d) - 128) * 100 / 128),
    Pid(0x07, "long_term_fuel_trim_1", "%", 1, lambda d: (_a(d) - 128) * 100 / 128),
    Pid(0x0B, "intake_map", "kPa", 1, _a),
    Pid(0x0C, "engine_rpm", "rpm", 2, lambda d: _ab(d) / 4),
    Pid(0x0D, "vehicle_speed", "km/h", 1, _a),
    Pid(0x0E, "timing_advance", "°", 1, lambda d: _a(d) / 2 - 64),
    Pid(0x0F, "intake_air_temp", "°C", 1, lambda d: _a(d) - 40),
    Pid(0x10, "maf", "g/s", 2, lambda d: _ab(d) / 100),
    Pid(0x11, "throttle", "%", 1, lambda d: _a(d) * 100 / 255),
    Pid(0x1F, "run_time", "s", 2, _ab),
    Pid(0x2F, "fuel_level", "%", 1, lambda d: _a(d) * 100 / 255),
    Pid(0x42, "control_module_voltage", "V", 2, lambda d: _ab(d) / 1000),
    Pid(0x46, "ambient_air_temp", "°C", 1, lambda d: _a(d) - 40),
    Pid(0x5C, "oil_temp", "°C", 1, lambda d: _a(d) - 40),
]}

BY_NAME: dict[str, Pid] = {p.name: p for p in PIDS.values()}


def decode(pid: int, data: bytes) -> tuple[str, float, str] | None:
    """(name, value, unit) for a known PID with enough data, else None."""
    info = PIDS.get(pid)
    if info is None or len(data) < info.length:
        return None
    return info.name, info.decode(data), info.unit
