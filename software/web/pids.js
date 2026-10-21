/*
 * Decoding of common SAE J1979 mode 01 PIDs, mirroring software/obd12/pids.py.
 * digits: decimals shown in the UI.
 */

const a = d => d[0];
const ab = d => (d[0] << 8) | d[1];

export const PIDS = new Map([
	{ pid: 0x04, name: "engine_load", label: "Engine load", unit: "%", length: 1, digits: 1, decode: d => a(d) * 100 / 255 },
	{ pid: 0x05, name: "coolant_temp", label: "Coolant", unit: "°C", length: 1, digits: 0, decode: d => a(d) - 40 },
	{ pid: 0x06, name: "short_term_fuel_trim_1", label: "Short fuel trim", unit: "%", length: 1, digits: 1, decode: d => (a(d) - 128) * 100 / 128 },
	{ pid: 0x07, name: "long_term_fuel_trim_1", label: "Long fuel trim", unit: "%", length: 1, digits: 1, decode: d => (a(d) - 128) * 100 / 128 },
	{ pid: 0x0B, name: "intake_map", label: "Intake MAP", unit: "kPa", length: 1, digits: 0, decode: a },
	{ pid: 0x0C, name: "engine_rpm", label: "RPM", unit: "rpm", length: 2, digits: 0, decode: d => ab(d) / 4 },
	{ pid: 0x0D, name: "vehicle_speed", label: "Speed", unit: "km/h", length: 1, digits: 0, decode: a },
	{ pid: 0x0E, name: "timing_advance", label: "Timing", unit: "°", length: 1, digits: 1, decode: d => a(d) / 2 - 64 },
	{ pid: 0x0F, name: "intake_air_temp", label: "Intake air", unit: "°C", length: 1, digits: 0, decode: d => a(d) - 40 },
	{ pid: 0x10, name: "maf", label: "MAF", unit: "g/s", length: 2, digits: 2, decode: d => ab(d) / 100 },
	{ pid: 0x11, name: "throttle", label: "Throttle", unit: "%", length: 1, digits: 1, decode: d => a(d) * 100 / 255 },
	{ pid: 0x1F, name: "run_time", label: "Run time", unit: "s", length: 2, digits: 0, decode: ab },
	{ pid: 0x2F, name: "fuel_level", label: "Fuel level", unit: "%", length: 1, digits: 1, decode: d => a(d) * 100 / 255 },
	{ pid: 0x42, name: "control_module_voltage", label: "Module voltage", unit: "V", length: 2, digits: 2, decode: d => ab(d) / 1000 },
	{ pid: 0x46, name: "ambient_air_temp", label: "Ambient air", unit: "°C", length: 1, digits: 0, decode: d => a(d) - 40 },
	{ pid: 0x5C, name: "oil_temp", label: "Oil temp", unit: "°C", length: 1, digits: 0, decode: d => a(d) - 40 },
].map(p => [p.pid, p]));

// { ...pid info, value } for a known PID with enough data, else null
export function decode(pid, data) {
	const info = PIDS.get(pid);
	if (!info || data.length < info.length)
		return null;
	return { ...info, value: info.decode(data) };
}

const IMPERIAL = {
	"km/h": { unit: "mph", convert: v => v * 0.621371 },
	"°C": { unit: "°F", convert: v => v * 9 / 5 + 32 },
};

// { value, unit } in the chosen unit system
export function display(decoded, imperial) {
	const conv = imperial && IMPERIAL[decoded.unit];
	return conv ? { value: conv.convert(decoded.value), unit: conv.unit } : { value: decoded.value, unit: decoded.unit };
}

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
