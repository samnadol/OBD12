"""Decoding of SAE J1979 mode 01 PIDs, mirroring software/web/pids.js.

A PID carries one or more fields (e.g. an O2 sensor's voltage and fuel trim).
A PID without fields is known by name only and shown as raw bytes (bit-field
PIDs such as monitor status). A field with a `present` check is only reported
when the vehicle fills it in.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Callable


@dataclass(frozen=True)
class Field:
    name: str
    label: str
    unit: str
    decode: Callable[[bytes], float]
    present: Callable[[bytes], bool] | None = None


@dataclass(frozen=True)
class Pid:
    pid: int
    label: str
    length: int
    fields: tuple[Field, ...] = field(default=())


def _a(d: bytes) -> int:
    return d[0]


def _ab(d: bytes) -> int:
    return (d[0] << 8) | d[1]


def _be(d: bytes, i: int) -> int:
    return (d[i] << 8) | d[i + 1]


def _pct(x: int) -> float:
    return x * 100 / 255


def _trim(x: int) -> float:
    return (x - 128) * 100 / 128


def _signed16(x: int) -> int:
    return x - 0x10000 if x >= 0x8000 else x


def _one(pid: int, name: str, label: str, unit: str, length: int, decode: Callable[[bytes], float]) -> Pid:
    return Pid(pid, label, length, (Field(name, label, unit, decode),))


def _raw(pid: int, label: str) -> Pid:
    return Pid(pid, label, 0)


def _support_bit(bit: int) -> Callable[[bytes], bool]:
    # PIDs 0x66, 0x67, 0x6D: byte A says which of the following values are filled in
    return lambda d: bool(d[0] & (1 << bit))


# sensor positions as laid out by PID 0x13 (PID 0x1D numbers them differently)
_O2_SENSORS = ["B1S1", "B1S2", "B1S3", "B1S4", "B2S1", "B2S2", "B2S3", "B2S4"]


def _o2_pids() -> list[Pid]:
    pids = []
    for i, s in enumerate(_O2_SENSORS):
        n = s.lower()
        pids.append(Pid(0x14 + i, f"O2 {s}", 2, (
            Field(f"o2_{n}_voltage", f"O2 {s} voltage", "V", lambda d: d[0] / 200),
            Field(f"o2_{n}_fuel_trim", f"O2 {s} trim", "%", lambda d: _trim(d[1]), lambda d: d[1] != 0xFF),
        )))
        pids.append(Pid(0x24 + i, f"O2 {s} (wide, V)", 4, (
            Field(f"o2_{n}_lambda", f"O2 {s} lambda", "λ", lambda d: _ab(d) * 2 / 65536),
            Field(f"o2_{n}_wide_voltage", f"O2 {s} voltage", "V", lambda d: _be(d, 2) * 8 / 65536),
        )))
        pids.append(Pid(0x34 + i, f"O2 {s} (wide, mA)", 4, (
            Field(f"o2_{n}_current_lambda", f"O2 {s} lambda", "λ", lambda d: _ab(d) * 2 / 65536),
            Field(f"o2_{n}_current", f"O2 {s} current", "mA", lambda d: _be(d, 2) / 256 - 128),
        )))
    return pids


PIDS: dict[int, Pid] = {p.pid: p for p in sorted([
    Pid(0x01, "Monitor status", 4, (
        Field("mil_on", "MIL", "", lambda d: d[0] >> 7),
        Field("dtc_count", "DTC count", "", lambda d: d[0] & 0x7F),
    )),
    _raw(0x02, "Freeze frame DTC"),
    _raw(0x03, "Fuel system status"),
    _one(0x04, "engine_load", "Engine load", "%", 1, lambda d: _pct(_a(d))),
    _one(0x05, "coolant_temp", "Coolant", "°C", 1, lambda d: _a(d) - 40),
    _one(0x06, "short_term_fuel_trim_1", "Short fuel trim B1", "%", 1, lambda d: _trim(_a(d))),
    _one(0x07, "long_term_fuel_trim_1", "Long fuel trim B1", "%", 1, lambda d: _trim(_a(d))),
    _one(0x08, "short_term_fuel_trim_2", "Short fuel trim B2", "%", 1, lambda d: _trim(_a(d))),
    _one(0x09, "long_term_fuel_trim_2", "Long fuel trim B2", "%", 1, lambda d: _trim(_a(d))),
    _one(0x0A, "fuel_pressure", "Fuel pressure", "kPa", 1, lambda d: _a(d) * 3),
    _one(0x0B, "intake_map", "Intake MAP", "kPa", 1, _a),
    _one(0x0C, "engine_rpm", "RPM", "rpm", 2, lambda d: _ab(d) / 4),
    _one(0x0D, "vehicle_speed", "Speed", "km/h", 1, _a),
    _one(0x0E, "timing_advance", "Timing", "°", 1, lambda d: _a(d) / 2 - 64),
    _one(0x0F, "intake_air_temp", "Intake air", "°C", 1, lambda d: _a(d) - 40),
    _one(0x10, "maf", "MAF", "g/s", 2, lambda d: _ab(d) / 100),
    _one(0x11, "throttle", "Throttle", "%", 1, lambda d: _pct(_a(d))),
    _raw(0x12, "Secondary air status"),
    _raw(0x13, "O2 sensors present"),
    *_o2_pids(),
    _one(0x1C, "obd_standard", "OBD standard", "", 1, _a),
    _raw(0x1D, "O2 sensors present (4 banks)"),
    _raw(0x1E, "Auxiliary input status"),
    _one(0x1F, "run_time", "Run time", "s", 2, _ab),
    _one(0x21, "distance_with_mil", "Distance with MIL on", "km", 2, _ab),
    _one(0x22, "fuel_rail_pressure_relative", "Fuel rail pressure (rel. vacuum)", "kPa", 2, lambda d: _ab(d) * 0.079),
    _one(0x23, "fuel_rail_pressure", "Fuel rail pressure", "kPa", 2, lambda d: _ab(d) * 10),
    _one(0x2C, "commanded_egr", "Commanded EGR", "%", 1, lambda d: _pct(_a(d))),
    _one(0x2D, "egr_error", "EGR error", "%", 1, lambda d: _a(d) * 100 / 128 - 100),
    _one(0x2E, "commanded_evap_purge", "Commanded evap purge", "%", 1, lambda d: _pct(_a(d))),
    _one(0x2F, "fuel_level", "Fuel level", "%", 1, lambda d: _pct(_a(d))),
    _one(0x30, "warmups_since_clear", "Warm-ups since clear", "", 1, _a),
    _one(0x31, "distance_since_clear", "Distance since clear", "km", 2, _ab),
    _one(0x32, "evap_vapor_pressure", "Evap vapor pressure", "Pa", 2, lambda d: _signed16(_ab(d)) / 4),
    _one(0x33, "barometric_pressure", "Barometric pressure", "kPa", 1, _a),
    _one(0x3C, "catalyst_temp_b1s1", "Catalyst B1S1", "°C", 2, lambda d: _ab(d) / 10 - 40),
    _one(0x3D, "catalyst_temp_b2s1", "Catalyst B2S1", "°C", 2, lambda d: _ab(d) / 10 - 40),
    _one(0x3E, "catalyst_temp_b1s2", "Catalyst B1S2", "°C", 2, lambda d: _ab(d) / 10 - 40),
    _one(0x3F, "catalyst_temp_b2s2", "Catalyst B2S2", "°C", 2, lambda d: _ab(d) / 10 - 40),
    _raw(0x41, "Monitor status this cycle"),
    _one(0x42, "control_module_voltage", "Module voltage", "V", 2, lambda d: _ab(d) / 1000),
    _one(0x43, "absolute_load", "Absolute load", "%", 2, lambda d: _pct(_ab(d))),
    _one(0x44, "commanded_lambda", "Commanded lambda", "λ", 2, lambda d: _ab(d) * 2 / 65536),
    _one(0x45, "relative_throttle", "Relative throttle", "%", 1, lambda d: _pct(_a(d))),
    _one(0x46, "ambient_air_temp", "Ambient air", "°C", 1, lambda d: _a(d) - 40),
    _one(0x47, "throttle_b", "Throttle B", "%", 1, lambda d: _pct(_a(d))),
    _one(0x48, "throttle_c", "Throttle C", "%", 1, lambda d: _pct(_a(d))),
    _one(0x49, "accelerator_d", "Accelerator D", "%", 1, lambda d: _pct(_a(d))),
    _one(0x4A, "accelerator_e", "Accelerator E", "%", 1, lambda d: _pct(_a(d))),
    _one(0x4B, "accelerator_f", "Accelerator F", "%", 1, lambda d: _pct(_a(d))),
    _one(0x4C, "commanded_throttle", "Commanded throttle", "%", 1, lambda d: _pct(_a(d))),
    _one(0x4D, "time_with_mil", "Time with MIL on", "min", 2, _ab),
    _one(0x4E, "time_since_clear", "Time since clear", "min", 2, _ab),
    _raw(0x4F, "Maximum values"),
    _raw(0x50, "Maximum MAF"),
    _one(0x51, "fuel_type", "Fuel type", "", 1, _a),
    _one(0x52, "ethanol", "Ethanol", "%", 1, lambda d: _pct(_a(d))),
    _one(0x53, "evap_vapor_pressure_abs", "Evap vapor pressure (abs.)", "kPa", 2, lambda d: _ab(d) / 200),
    _one(0x54, "evap_vapor_pressure_2", "Evap vapor pressure", "Pa", 2, lambda d: _ab(d) - 32767),
    _one(0x55, "short_term_o2_trim_1", "Short O2 trim B1", "%", 2, lambda d: _trim(_a(d))),
    _one(0x56, "long_term_o2_trim_1", "Long O2 trim B1", "%", 2, lambda d: _trim(_a(d))),
    _one(0x57, "short_term_o2_trim_2", "Short O2 trim B2", "%", 2, lambda d: _trim(_a(d))),
    _one(0x58, "long_term_o2_trim_2", "Long O2 trim B2", "%", 2, lambda d: _trim(_a(d))),
    _one(0x59, "fuel_rail_pressure_abs", "Fuel rail pressure (abs.)", "kPa", 2, lambda d: _ab(d) * 10),
    _one(0x5A, "relative_accelerator", "Relative accelerator", "%", 1, lambda d: _pct(_a(d))),
    _one(0x5B, "hybrid_battery", "Hybrid battery", "%", 1, lambda d: _pct(_a(d))),
    _one(0x5C, "oil_temp", "Oil temp", "°C", 1, lambda d: _a(d) - 40),
    _one(0x5D, "injection_timing", "Injection timing", "°", 2, lambda d: _ab(d) / 128 - 210),
    _one(0x5E, "fuel_rate", "Fuel rate", "L/h", 2, lambda d: _ab(d) / 20),
    _raw(0x5F, "Emission requirements"),
    _one(0x61, "driver_demand_torque", "Demanded torque", "%", 1, lambda d: _a(d) - 125),
    _one(0x62, "actual_torque", "Actual torque", "%", 1, lambda d: _a(d) - 125),
    _one(0x63, "reference_torque", "Reference torque", "Nm", 2, _ab),
    _raw(0x64, "Engine percent torque data"),
    _raw(0x65, "Auxiliary I/O"),
    Pid(0x66, "MAF sensors", 5, (
        Field("maf_a", "MAF A", "g/s", lambda d: _be(d, 1) / 32, _support_bit(0)),
        Field("maf_b", "MAF B", "g/s", lambda d: _be(d, 3) / 32, _support_bit(1)),
    )),
    Pid(0x67, "Coolant sensors", 3, (
        Field("coolant_temp_1", "Coolant 1", "°C", lambda d: d[1] - 40, _support_bit(0)),
        Field("coolant_temp_2", "Coolant 2", "°C", lambda d: d[2] - 40, _support_bit(1)),
    )),
    _raw(0x68, "Intake air temp sensors"),
    # SAE J1979-DA: pressures 10 kPa per bit, temperatures A - 40
    Pid(0x6D, "Fuel pressure control", 11, (
        Field("fuel_rail_pressure_commanded_a", "Rail pressure A, commanded", "kPa", lambda d: _be(d, 1) * 10, _support_bit(0)),
        Field("fuel_rail_pressure_actual_a", "Rail pressure A, actual", "kPa", lambda d: _be(d, 3) * 10, _support_bit(1)),
        Field("fuel_rail_temp_a", "Rail temp A", "°C", lambda d: d[5] - 40, _support_bit(2)),
        Field("fuel_rail_pressure_commanded_b", "Rail pressure B, commanded", "kPa", lambda d: _be(d, 6) * 10, _support_bit(3)),
        Field("fuel_rail_pressure_actual_b", "Rail pressure B, actual", "kPa", lambda d: _be(d, 8) * 10, _support_bit(4)),
        Field("fuel_rail_temp_b", "Rail temp B", "°C", lambda d: d[10] - 40, _support_bit(5)),
    )),
    _one(0x8E, "friction_torque", "Friction torque", "%", 1, lambda d: _a(d) - 125),
    _one(0xA6, "odometer", "Odometer", "km", 4, lambda d: int.from_bytes(d[:4], "big") / 10),
], key=lambda p: p.pid)}


def is_range_pid(pid: int) -> bool:
    """PIDs 0x00, 0x20, ... report which PIDs are supported; they carry no data."""
    return pid % 0x20 == 0


def label(pid: int) -> str:
    info = PIDS.get(pid)
    return info.label if info else f"PID {pid:02X}"


def decode(pid: int, data: bytes) -> list[tuple[str, float, str]] | None:
    """[(name, value, unit)] for the fields the data fills in; None for a PID
    without fields or with too little data (show it raw)."""
    info = PIDS.get(pid)
    if info is None or not info.fields or len(data) < info.length:
        return None
    return [(f.name, f.decode(data), f.unit) for f in info.fields if f.present is None or f.present(data)]


def pids_from_bitmap(bitmap: bytes) -> list[int]:
    """GET_SUPPORTED_PIDS bitmap (or the concatenated A-D bytes of PIDs 0x00, 0x20, ...) -> sorted PIDs."""
    return [p for p in range(1, min(0xFF, len(bitmap) * 8) + 1) if bitmap[(p - 1) // 8] & (0x80 >> ((p - 1) % 8))]
