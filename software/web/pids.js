/*
 * Decoding of SAE J1979 mode 01 PIDs, mirroring software/obd12/pids.py.
 *
 * A PID carries one or more fields (e.g. an O2 sensor's voltage and fuel
 * trim). A PID without fields is known by name only and shown as raw bytes
 * (bit-field PIDs such as monitor status). digits: decimals shown in the UI.
 * A field with present(d) is only shown when the vehicle fills it in.
 */

const a = d => d[0];
const ab = d => (d[0] << 8) | d[1];
const cd = d => (d[2] << 8) | d[3];
const be = (d, i) => (d[i] << 8) | d[i + 1];
const pct = x => x * 100 / 255;
const trim = x => (x - 128) * 100 / 128;
const signed16 = x => x >= 0x8000 ? x - 0x10000 : x;

// single-field PID
const one = (pid, name, label, unit, length, digits, decode) =>
	({ pid, label, length, fields: [{ name, label, unit, digits, decode }] });

// PID known by name only, shown raw
const raw = (pid, label) => ({ pid, label, length: 0, fields: [] });

// sensor positions as laid out by PID 0x13 (PID 0x1D numbers them differently)
const O2_SENSORS = ["B1S1", "B1S2", "B1S3", "B1S4", "B2S1", "B2S2", "B2S3", "B2S4"];

const o2Voltage = O2_SENSORS.map((s, i) => ({
	pid: 0x14 + i, label: `O2 ${s}`, length: 2, fields: [
		{ name: `o2_${s.toLowerCase()}_voltage`, label: `O2 ${s} voltage`, unit: "V", digits: 3, decode: d => d[0] / 200 },
		{ name: `o2_${s.toLowerCase()}_fuel_trim`, label: `O2 ${s} trim`, unit: "%", digits: 1, decode: d => trim(d[1]), present: d => d[1] !== 0xFF },
	],
}));

const o2WideVoltage = O2_SENSORS.map((s, i) => ({
	pid: 0x24 + i, label: `O2 ${s} (wide, V)`, length: 4, fields: [
		{ name: `o2_${s.toLowerCase()}_lambda`, label: `O2 ${s} lambda`, unit: "λ", digits: 3, decode: d => ab(d) * 2 / 65536 },
		{ name: `o2_${s.toLowerCase()}_wide_voltage`, label: `O2 ${s} voltage`, unit: "V", digits: 3, decode: d => cd(d) * 8 / 65536 },
	],
}));

const o2WideCurrent = O2_SENSORS.map((s, i) => ({
	pid: 0x34 + i, label: `O2 ${s} (wide, mA)`, length: 4, fields: [
		{ name: `o2_${s.toLowerCase()}_current_lambda`, label: `O2 ${s} lambda`, unit: "λ", digits: 3, decode: d => ab(d) * 2 / 65536 },
		{ name: `o2_${s.toLowerCase()}_current`, label: `O2 ${s} current`, unit: "mA", digits: 2, decode: d => cd(d) / 256 - 128 },
	],
}));

// PIDs 0x66, 0x67, 0x6D: byte A says which of the following values are filled in
const supportBit = bit => d => (d[0] & (1 << bit)) !== 0;

export const PIDS = new Map([
	{
		pid: 0x01, label: "Monitor status", length: 4, fields: [
			{ name: "mil_on", label: "MIL", unit: "", digits: 0, decode: d => d[0] >> 7 },
			{ name: "dtc_count", label: "DTC count", unit: "", digits: 0, decode: d => d[0] & 0x7F },
		],
	},
	raw(0x02, "Freeze frame DTC"),
	raw(0x03, "Fuel system status"),
	one(0x04, "engine_load", "Engine load", "%", 1, 1, d => pct(a(d))),
	one(0x05, "coolant_temp", "Coolant", "°C", 1, 0, d => a(d) - 40),
	one(0x06, "short_term_fuel_trim_1", "Short fuel trim B1", "%", 1, 1, d => trim(a(d))),
	one(0x07, "long_term_fuel_trim_1", "Long fuel trim B1", "%", 1, 1, d => trim(a(d))),
	one(0x08, "short_term_fuel_trim_2", "Short fuel trim B2", "%", 1, 1, d => trim(a(d))),
	one(0x09, "long_term_fuel_trim_2", "Long fuel trim B2", "%", 1, 1, d => trim(a(d))),
	one(0x0A, "fuel_pressure", "Fuel pressure", "kPa", 1, 0, d => a(d) * 3),
	one(0x0B, "intake_map", "Intake MAP", "kPa", 1, 0, a),
	one(0x0C, "engine_rpm", "RPM", "rpm", 2, 0, d => ab(d) / 4),
	one(0x0D, "vehicle_speed", "Speed", "km/h", 1, 0, a),
	one(0x0E, "timing_advance", "Timing", "°", 1, 1, d => a(d) / 2 - 64),
	one(0x0F, "intake_air_temp", "Intake air", "°C", 1, 0, d => a(d) - 40),
	one(0x10, "maf", "MAF", "g/s", 2, 2, d => ab(d) / 100),
	one(0x11, "throttle", "Throttle", "%", 1, 1, d => pct(a(d))),
	raw(0x12, "Secondary air status"),
	raw(0x13, "O2 sensors present"),
	...o2Voltage,
	one(0x1C, "obd_standard", "OBD standard", "", 1, 0, a),
	raw(0x1D, "O2 sensors present (4 banks)"),
	raw(0x1E, "Auxiliary input status"),
	one(0x1F, "run_time", "Run time", "s", 2, 0, ab),
	one(0x21, "distance_with_mil", "Distance with MIL on", "km", 2, 0, ab),
	one(0x22, "fuel_rail_pressure_relative", "Fuel rail pressure (rel. vacuum)", "kPa", 2, 1, d => ab(d) * 0.079),
	one(0x23, "fuel_rail_pressure", "Fuel rail pressure", "kPa", 2, 0, d => ab(d) * 10),
	...o2WideVoltage,
	one(0x2C, "commanded_egr", "Commanded EGR", "%", 1, 1, d => pct(a(d))),
	one(0x2D, "egr_error", "EGR error", "%", 1, 1, d => a(d) * 100 / 128 - 100),
	one(0x2E, "commanded_evap_purge", "Commanded evap purge", "%", 1, 1, d => pct(a(d))),
	one(0x2F, "fuel_level", "Fuel level", "%", 1, 1, d => pct(a(d))),
	one(0x30, "warmups_since_clear", "Warm-ups since clear", "", 1, 0, a),
	one(0x31, "distance_since_clear", "Distance since clear", "km", 2, 0, ab),
	one(0x32, "evap_vapor_pressure", "Evap vapor pressure", "Pa", 2, 1, d => signed16(ab(d)) / 4),
	one(0x33, "barometric_pressure", "Barometric pressure", "kPa", 1, 0, a),
	...o2WideCurrent,
	one(0x3C, "catalyst_temp_b1s1", "Catalyst B1S1", "°C", 2, 0, d => ab(d) / 10 - 40),
	one(0x3D, "catalyst_temp_b2s1", "Catalyst B2S1", "°C", 2, 0, d => ab(d) / 10 - 40),
	one(0x3E, "catalyst_temp_b1s2", "Catalyst B1S2", "°C", 2, 0, d => ab(d) / 10 - 40),
	one(0x3F, "catalyst_temp_b2s2", "Catalyst B2S2", "°C", 2, 0, d => ab(d) / 10 - 40),
	raw(0x41, "Monitor status this cycle"),
	one(0x42, "control_module_voltage", "Module voltage", "V", 2, 2, d => ab(d) / 1000),
	one(0x43, "absolute_load", "Absolute load", "%", 2, 1, d => pct(ab(d))),
	one(0x44, "commanded_lambda", "Commanded lambda", "λ", 2, 3, d => ab(d) * 2 / 65536),
	one(0x45, "relative_throttle", "Relative throttle", "%", 1, 1, d => pct(a(d))),
	one(0x46, "ambient_air_temp", "Ambient air", "°C", 1, 0, d => a(d) - 40),
	one(0x47, "throttle_b", "Throttle B", "%", 1, 1, d => pct(a(d))),
	one(0x48, "throttle_c", "Throttle C", "%", 1, 1, d => pct(a(d))),
	one(0x49, "accelerator_d", "Accelerator D", "%", 1, 1, d => pct(a(d))),
	one(0x4A, "accelerator_e", "Accelerator E", "%", 1, 1, d => pct(a(d))),
	one(0x4B, "accelerator_f", "Accelerator F", "%", 1, 1, d => pct(a(d))),
	one(0x4C, "commanded_throttle", "Commanded throttle", "%", 1, 1, d => pct(a(d))),
	one(0x4D, "time_with_mil", "Time with MIL on", "min", 2, 0, ab),
	one(0x4E, "time_since_clear", "Time since clear", "min", 2, 0, ab),
	raw(0x4F, "Maximum values"),
	raw(0x50, "Maximum MAF"),
	one(0x51, "fuel_type", "Fuel type", "", 1, 0, a),
	one(0x52, "ethanol", "Ethanol", "%", 1, 1, d => pct(a(d))),
	one(0x53, "evap_vapor_pressure_abs", "Evap vapor pressure (abs.)", "kPa", 2, 3, d => ab(d) / 200),
	one(0x54, "evap_vapor_pressure_2", "Evap vapor pressure", "Pa", 2, 0, d => ab(d) - 32767),
	one(0x55, "short_term_o2_trim_1", "Short O2 trim B1", "%", 2, 1, d => trim(a(d))),
	one(0x56, "long_term_o2_trim_1", "Long O2 trim B1", "%", 2, 1, d => trim(a(d))),
	one(0x57, "short_term_o2_trim_2", "Short O2 trim B2", "%", 2, 1, d => trim(a(d))),
	one(0x58, "long_term_o2_trim_2", "Long O2 trim B2", "%", 2, 1, d => trim(a(d))),
	one(0x59, "fuel_rail_pressure_abs", "Fuel rail pressure (abs.)", "kPa", 2, 0, d => ab(d) * 10),
	one(0x5A, "relative_accelerator", "Relative accelerator", "%", 1, 1, d => pct(a(d))),
	one(0x5B, "hybrid_battery", "Hybrid battery", "%", 1, 1, d => pct(a(d))),
	one(0x5C, "oil_temp", "Oil temp", "°C", 1, 0, d => a(d) - 40),
	one(0x5D, "injection_timing", "Injection timing", "°", 2, 2, d => ab(d) / 128 - 210),
	one(0x5E, "fuel_rate", "Fuel rate", "L/h", 2, 2, d => ab(d) / 20),
	raw(0x5F, "Emission requirements"),
	one(0x61, "driver_demand_torque", "Demanded torque", "%", 1, 0, d => a(d) - 125),
	one(0x62, "actual_torque", "Actual torque", "%", 1, 0, d => a(d) - 125),
	one(0x63, "reference_torque", "Reference torque", "Nm", 2, 0, ab),
	raw(0x64, "Engine percent torque data"),
	raw(0x65, "Auxiliary I/O"),
	{
		pid: 0x66, label: "MAF sensors", length: 5, fields: [
			{ name: "maf_a", label: "MAF A", unit: "g/s", digits: 2, decode: d => be(d, 1) / 32, present: supportBit(0) },
			{ name: "maf_b", label: "MAF B", unit: "g/s", digits: 2, decode: d => be(d, 3) / 32, present: supportBit(1) },
		],
	},
	{
		pid: 0x67, label: "Coolant sensors", length: 3, fields: [
			{ name: "coolant_temp_1", label: "Coolant 1", unit: "°C", digits: 0, decode: d => d[1] - 40, present: supportBit(0) },
			{ name: "coolant_temp_2", label: "Coolant 2", unit: "°C", digits: 0, decode: d => d[2] - 40, present: supportBit(1) },
		],
	},
	raw(0x68, "Intake air temp sensors"),
	{
		// SAE J1979-DA: pressures 10 kPa per bit, temperatures A - 40
		pid: 0x6D, label: "Fuel pressure control", length: 11, fields: [
			{ name: "fuel_rail_pressure_commanded_a", label: "Rail pressure A, commanded", unit: "kPa", digits: 0, decode: d => be(d, 1) * 10, present: supportBit(0) },
			{ name: "fuel_rail_pressure_actual_a", label: "Rail pressure A, actual", unit: "kPa", digits: 0, decode: d => be(d, 3) * 10, present: supportBit(1) },
			{ name: "fuel_rail_temp_a", label: "Rail temp A", unit: "°C", digits: 0, decode: d => d[5] - 40, present: supportBit(2) },
			{ name: "fuel_rail_pressure_commanded_b", label: "Rail pressure B, commanded", unit: "kPa", digits: 0, decode: d => be(d, 6) * 10, present: supportBit(3) },
			{ name: "fuel_rail_pressure_actual_b", label: "Rail pressure B, actual", unit: "kPa", digits: 0, decode: d => be(d, 8) * 10, present: supportBit(4) },
			{ name: "fuel_rail_temp_b", label: "Rail temp B", unit: "°C", digits: 0, decode: d => d[10] - 40, present: supportBit(5) },
		],
	},
	one(0x8E, "friction_torque", "Friction torque", "%", 1, 0, d => a(d) - 125),
	one(0xA6, "odometer", "Odometer", "km", 4, 1, d => ((d[0] * 2 ** 24) + (d[1] << 16) + (d[2] << 8) + d[3]) / 10),
].map(p => [p.pid, p]));

// PID 0x00, 0x20, ... report which PIDs are supported; they carry no data
export const isRangePid = pid => pid % 0x20 === 0;

export const pidLabel = pid => PIDS.get(pid)?.label ?? `PID ${pid.toString(16).toUpperCase().padStart(2, "0")}`;

// fields of a known PID that the data fills in: [{ ...field, value }]; null
// for a PID without fields or with too little data (shown raw)
export function decode(pid, data) {
	const info = PIDS.get(pid);
	if (!info || info.fields.length === 0 || data.length < info.length)
		return null;
	return info.fields.filter(f => !f.present || f.present(data)).map(f => ({ ...f, value: f.decode(data) }));
}

const IMPERIAL = {
	"km/h": { unit: "mph", convert: v => v * 0.621371 },
	"km": { unit: "mi", convert: v => v * 0.621371 },
	"°C": { unit: "°F", convert: v => v * 9 / 5 + 32 },
	"kPa": { unit: "psi", convert: v => v * 0.145038, extraDigits: 1 },
	"L/h": { unit: "gal/h", convert: v => v * 0.264172, extraDigits: 1 },
	"Nm": { unit: "lb·ft", convert: v => v * 0.737562 },
};

// { value, unit, digits } of a decoded field in the chosen unit system
export function display(field, imperial) {
	const conv = imperial && IMPERIAL[field.unit];
	return conv
		? { value: conv.convert(field.value), unit: conv.unit, digits: field.digits + (conv.extraDigits ?? 0) }
		: { value: field.value, unit: field.unit, digits: field.digits };
}

// unit a field is shown in, for column headers
export const displayUnit = (unit, imperial) => (imperial && IMPERIAL[unit]?.unit) || unit;

// mode 03 / 07 / 0A responses (43 / 47 / 4A): [count][A B]... -> ["P0301", ...]
export function decodeDtcs(message) {
	const codes = [];
	for (let i = 2; i + 1 < message.length; i += 2) {
		const a = message[i], b = message[i + 1];
		if (a === 0 && b === 0)
			continue;
		const hex = n => n.toString(16).toUpperCase();
		codes.push("PCBU"[a >> 6] + ((a >> 4) & 3) + hex(a & 0xF) + hex(b >> 4) + hex(b & 0xF));
	}
	return codes;
}

// GET_SUPPORTED_PIDS bitmap (or the concatenated A-D bytes of PIDs 0x00, 0x20, ...) -> sorted PIDs
export function pidsFromBitmap(bitmap) {
	const pids = [];
	for (let p = 1; p <= Math.min(0xFF, bitmap.length * 8); p++)
		if (bitmap[(p - 1) >> 3] & (0x80 >> ((p - 1) & 7)))
			pids.push(p);
	return pids;
}
