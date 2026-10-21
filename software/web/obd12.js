/*
 * OBD12 host protocol for the browser, mirroring software/obd12 (framing.py,
 * protocol.py, client.py) and docs/protocol.md.
 *
 * A Device wraps one transport (BleTransport: Web Bluetooth, SerialTransport:
 * Web Serial) and keeps it open. After the link drops, Device.open() can be
 * called again on the same Device; it restores the event subscriptions and
 * live polling, which the firmware forgets with the connection.
 */

// --- protocol constants (protocol.h) ---

export const DEVICE_NAME = "OBD12";
export const NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
export const NUS_RX = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";   // host -> device (write)
export const NUS_TX = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";   // device -> host (notify)

export const FrameType = Object.freeze({ REQ: 0x01, RSP: 0x02, ERR: 0x03, EVT: 0x04 });

export const Cmd = Object.freeze({
	PING: 0x01,
	GET_INFO: 0x02,
	GET_POWER: 0x03,
	SET_EVENTS: 0x04,
	GET_CAN_STATS: 0x05,
	RESET: 0x06,
	BLE_CLEAR_BONDS: 0x07,
	OBD_REQUEST: 0x10,
	GET_VIN: 0x11,
	LIVE_START: 0x12,
	LIVE_STOP: 0x13,
});

export const Evt = Object.freeze({ LOG: 0x80, POWER: 0x81, CAN_STATUS: 0x82, LIVE_DATA: 0x83 });

export const Topic = Object.freeze({ LOG: 0x01, POWER: 0x02, CAN_STATUS: 0x04 });

export const Err = Object.freeze({
	BAD_REQUEST: 0x01,
	UNKNOWN_CMD: 0x02,
	BUSY: 0x03,
	TIMEOUT: 0x04,
	NOT_AUTHORIZED: 0x05,
	CAN: 0x06,
	UNAVAILABLE: 0x07,
});

export const InfoFlag = Object.freeze({ BLE_AVAILABLE: 0x01, BLE_CONNECTED: 0x02, AUTHORIZED: 0x04, VIN_KNOWN: 0x08 });

export const RESET_CAUSES = ["unknown", "power-on", "reset pin", "software", "watchdog", "window watchdog", "low-power", "fatal error"];
export const FAULTS = ["none", "Error_Handler", "HardFault", "MemManage", "BusFault", "UsageFault", "NMI"];
export const CAN_STATES = ["OK", "error warning", "error passive", "bus-off", "not started"];

export const OBD_ID_BROADCAST = 0x7DF;
export const OBD_NEGATIVE_RESPONSE = 0x7F;

const DEFAULT_TIMEOUT_MS = 2000;
const OBD_TIMEOUT_MS = 7000;            // allows for "response pending" (up to 5 s)

// the firmware drops a live subscription after 30 s without requests
const LIVE_KEEPALIVE_MS = 10000;

const nameOf = (table, value) => Object.keys(table).find(k => table[k] === value) ?? `0x${hex8(value)}`;
const hex8 = n => n.toString(16).toUpperCase().padStart(2, "0");

export function toHex(bytes, sep = " ") {
	return Array.from(bytes, hex8).join(sep);
}

// --- framing (framing.py) ---

export const HEADER_LEN = 3;
export const CRC_LEN = 2;
export const MAX_PAYLOAD = 240;
export const MAX_FRAME = HEADER_LEN + MAX_PAYLOAD + CRC_LEN;
export const MAX_ENCODED = MAX_FRAME + Math.floor(MAX_FRAME / 254) + 1;

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no final XOR
export function crc16(data) {
	let crc = 0xFFFF;
	for (const byte of data) {
		crc ^= byte << 8;
		for (let i = 0; i < 8; i++)
			crc = ((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1) & 0xFFFF;
	}
	return crc;
}

export function cobsEncode(data) {
	const out = [0];
	let codeIdx = 0;
	let code = 1;

	for (let i = 0; i < data.length; i++) {
		const byte = data[i];
		if (byte === 0) {
			out[codeIdx] = code;
			code = 1;
			codeIdx = out.length;
			out.push(0);
			continue;
		}

		out.push(byte);
		code++;

		// a full block (254 data bytes) has no implicit zero after it
		if (code === 0xFF && i + 1 < data.length) {
			out[codeIdx] = code;
			code = 1;
			codeIdx = out.length;
			out.push(0);
		}
	}

	out[codeIdx] = code;
	return Uint8Array.from(out);
}

// null if malformed
export function cobsDecode(data) {
	const out = [];
	let i = 0;
	while (i < data.length) {
		const code = data[i];
		if (code === 0 || i + code > data.length)
			return null;
		for (let j = i + 1; j < i + code; j++) {
			if (data[j] === 0)
				return null;
			out.push(data[j]);
		}
		i += code;
		if (code !== 0xFF && i !== data.length)
			out.push(0);
	}
	return Uint8Array.from(out);
}

export function encodeFrame(type, seq, id, payload = new Uint8Array(0)) {
	if (payload.length > MAX_PAYLOAD)
		throw new RangeError(`payload too long (${payload.length} > ${MAX_PAYLOAD})`);

	const body = new Uint8Array(HEADER_LEN + payload.length + CRC_LEN);
	body[0] = type;
	body[1] = seq & 0xFF;
	body[2] = id;
	body.set(payload, HEADER_LEN);
	const crc = crc16(body.subarray(0, HEADER_LEN + payload.length));
	body[body.length - 2] = crc & 0xFF;
	body[body.length - 1] = crc >> 8;

	const encoded = cobsEncode(body);
	const out = new Uint8Array(encoded.length + 1);   // + 0x00 delimiter
	out.set(encoded);
	return out;
}

// one frame without the delimiter; null if it is malformed or fails the CRC
export function decodeFrame(encoded) {
	const body = cobsDecode(encoded);
	if (!body || body.length < HEADER_LEN + CRC_LEN)
		return null;
	const crc = body[body.length - 2] | (body[body.length - 1] << 8);
	if (crc16(body.subarray(0, body.length - CRC_LEN)) !== crc)
		return null;
	return { type: body[0], seq: body[1], id: body[2], payload: body.slice(HEADER_LEN, body.length - CRC_LEN) };
}

// splits a byte stream into frames; resynchronises on the next delimiter after garbage
export class FrameDecoder {
	constructor() {
		this._buf = new Uint8Array(MAX_ENCODED);
		this._len = 0;
		this._overflow = false;
	}

	feed(data) {
		const frames = [];
		for (const byte of data) {
			if (byte === 0) {
				if (this._len > 0 && !this._overflow) {
					const frame = decodeFrame(this._buf.subarray(0, this._len));
					if (frame)
						frames.push(frame);
				}
				this._len = 0;
				this._overflow = false;
			} else if (this._len < this._buf.length) {
				this._buf[this._len++] = byte;
			} else {
				this._overflow = true;
			}
		}
		return frames;
	}
}

// --- payloads (protocol.py) ---

const view = p => new DataView(p.buffer, p.byteOffset, p.byteLength);

export function parseInfo(p) {
	const v = view(p);
	return {
		protoVersion: p[0],
		fwVersion: [p[1], p[2], p[3]],
		hwconf: p[4],
		resetCause: p[5],
		lastFault: p[6],
		canState: p[7],
		lastFaultInfo: v.getUint32(8, true),
		uid: [v.getUint32(12, true), v.getUint32(16, true), v.getUint32(20, true)],
		bleMac: toHex(p.subarray(24, 30), ":"),
		flags: p[30],
		blePasskey: v.getUint32(32, true),
	};
}

export function parsePower(p) {
	const v = view(p);
	return {
		usbRegulatorOk: !!p[0],
		canRegulatorOk: !!p[1],
		usbRegulatorMv: v.getUint16(2, true),
		canRegulatorMv: v.getUint16(4, true),
	};
}

const CAN_STATS_FIELDS = ["rx_frames", "rx_dropped", "tx_frames", "tx_failed", "bus_off_count", "transceiver_faults"];

export function parseCanStats(p) {
	const v = view(p);
	return Object.fromEntries(CAN_STATS_FIELDS.map((name, i) => [name, v.getUint32(i * 4, true)]));
}

// one record per answering ECU: { ecuId, message, negative, nrc }
export function parseObdRecords(p) {
	const responses = [];
	let pos = 0;
	while (pos + 3 <= p.length) {
		const len = p[pos + 2];
		const message = p.slice(pos + 3, pos + 3 + len);
		const negative = message.length >= 3 && message[0] === OBD_NEGATIVE_RESPONSE;
		responses.push({ ecuId: p[pos] | (p[pos + 1] << 8), message, negative, nrc: negative ? message[2] : null });
		pos += 3 + len;
	}
	return responses;
}

// EVT_LIVE_DATA payload -> Map pid -> data bytes (without 0x41 and the PID)
export function parseLiveRecords(p) {
	const values = new Map();
	let pos = 0;
	while (pos + 2 <= p.length) {
		const len = p[pos + 1];
		values.set(p[pos], p.slice(pos + 2, pos + 2 + len));
		pos += 2 + len;
	}
	return values;
}

// --- client (client.py) ---

export class DeviceError extends Error {
	constructor(cmd, code) {
		super(`${nameOf(Cmd, cmd)} failed: ${nameOf(Err, code)}`);
		this.name = "DeviceError";
		this.cmd = cmd;
		this.code = code;
	}
}

export class RequestTimeout extends Error {
	constructor(cmd) {
		super(`${nameOf(Cmd, cmd)}: no answer from the device`);
		this.name = "RequestTimeout";
		this.cmd = cmd;
	}
}

export class Device {
	constructor(transport) {
		this.transport = transport;
		this._decoder = new FrameDecoder();
		this._seq = 0;
		this._pending = new Map();      // seq -> { cmd, resolve, reject, timer }
		this._handlers = new Map();     // event id -> [handler(payload)]
		this._lostHandlers = [];
		this._topics = null;            // last SET_EVENTS mask
		this._live = null;              // { pids, intervalMs } while live polling
		this._keepalive = null;

		transport.onData = data => this._onData(data);
		transport.onLost = () => this._onLost();
	}

	get connected() {
		return this.transport.connected;
	}

	get live() {
		return this._live !== null;
	}

	// connects, or reconnects after the link was lost
	async open() {
		this._decoder = new FrameDecoder();
		await this.transport.open();
		await this.restore();
	}

	// re-sends the event subscriptions and live polling, which the firmware
	// forgets with a BLE connection and on a reboot
	async restore() {
		if (this._topics !== null)
			await this.setEvents(this._topics);
		if (this._live)
			await this._sendLiveStart(this._live);
	}

	async close() {
		this._stopKeepalive();
		this._failPending(new Error("disconnected"));
		await this.transport.close();
	}

	// --- events ---

	// registers handler(payload) for an event id; returns a function that removes it
	on(evt, handler) {
		const list = this._handlers.get(evt) ?? [];
		list.push(handler);
		this._handlers.set(evt, list);
		return () => list.splice(list.indexOf(handler), 1);
	}

	onLog(handler) {
		const decoder = new TextDecoder("utf-8");
		return this.on(Evt.LOG, p => handler(decoder.decode(p)));
	}

	onPower(handler) {
		return this.on(Evt.POWER, p => handler(parsePower(p)));
	}

	onCanStatus(handler) {
		return this.on(Evt.CAN_STATUS, p => handler(p[0]));
	}

	onLiveData(handler) {
		return this.on(Evt.LIVE_DATA, p => handler(parseLiveRecords(p)));
	}

	// called when the link drops without close()
	onLost(handler) {
		this._lostHandlers.push(handler);
	}

	// --- requests ---

	// resolves with the response payload; rejects with DeviceError or RequestTimeout
	request(cmd, payload = new Uint8Array(0), timeoutMs = DEFAULT_TIMEOUT_MS) {
		if (!this.connected)
			return Promise.reject(new Error("not connected"));

		const seq = this._nextSeq();
		const frame = encodeFrame(FrameType.REQ, seq, cmd, payload);

		return new Promise((resolve, reject) => {
			const timer = setTimeout(() => {
				this._pending.delete(seq);
				reject(new RequestTimeout(cmd));
			}, timeoutMs);
			this._pending.set(seq, { cmd, resolve, reject, timer });

			this.transport.write(frame).catch(e => {
				clearTimeout(timer);
				this._pending.delete(seq);
				reject(e);
			});
		});
	}

	ping(data = new Uint8Array(0)) {
		return this.request(Cmd.PING, data);
	}

	async getInfo() {
		return parseInfo(await this.request(Cmd.GET_INFO));
	}

	async getPower() {
		return parsePower(await this.request(Cmd.GET_POWER));
	}

	// topics: Topic bit mask; kept and re-sent after a reconnect
	async setEvents(topics) {
		await this.request(Cmd.SET_EVENTS, Uint8Array.of(topics));
		this._topics = topics;
	}

	get topics() {
		return this._topics;
	}

	async getCanStats() {
		return parseCanStats(await this.request(Cmd.GET_CAN_STATS));
	}

	async reset() {
		await this.request(Cmd.RESET);
	}

	async bleClearBonds() {
		await this.request(Cmd.BLE_CLEAR_BONDS);
	}

	// raw OBD request (service ID + parameters, 1-7 bytes); resolves with every ECU's answer
	async obdRequest(data, dest = OBD_ID_BROADCAST, timeoutMs = OBD_TIMEOUT_MS) {
		const payload = new Uint8Array(2 + data.length);
		view(payload).setUint16(0, dest, true);
		payload.set(data, 2);
		return parseObdRecords(await this.request(Cmd.OBD_REQUEST, payload, timeoutMs));
	}

	async getVin() {
		return new TextDecoder("ascii").decode(await this.request(Cmd.GET_VIN, undefined, OBD_TIMEOUT_MS));
	}

	// starts (or reconfigures) live polling of mode 01 PIDs; results arrive via onLiveData()
	async liveStart(pids, intervalMs = 500) {
		const live = { pids: Uint8Array.from(pids), intervalMs };
		await this._sendLiveStart(live);
		this._live = live;
	}

	async liveStop() {
		this._live = null;
		this._stopKeepalive();
		await this.request(Cmd.LIVE_STOP);
	}

	// --- internals ---

	async _sendLiveStart(live) {
		const payload = new Uint8Array(2 + live.pids.length);
		view(payload).setUint16(0, live.intervalMs, true);
		payload.set(live.pids, 2);
		await this.request(Cmd.LIVE_START, payload);
		this._startKeepalive();
	}

	_startKeepalive() {
		if (this._keepalive === null)
			this._keepalive = setInterval(() => this.ping().catch(() => {}), LIVE_KEEPALIVE_MS);
	}

	_stopKeepalive() {
		clearInterval(this._keepalive);
		this._keepalive = null;
	}

	_nextSeq() {
		for (let i = 0; i < 256; i++) {
			this._seq = (this._seq + 1) & 0xFF;
			if (!this._pending.has(this._seq))
				return this._seq;
		}
		throw new Error("too many requests in flight");
	}

	_failPending(error) {
		for (const { reject, timer } of this._pending.values()) {
			clearTimeout(timer);
			reject(error);
		}
		this._pending.clear();
	}

	_onLost() {
		this._stopKeepalive();
		this._failPending(new Error("connection lost"));
		for (const handler of this._lostHandlers)
			handler();
	}

	_onData(data) {
		for (const frame of this._decoder.feed(data))
			this._onFrame(frame);
	}

	_onFrame(frame) {
		if (frame.type === FrameType.EVT) {
			for (const handler of this._handlers.get(frame.id) ?? [])
				handler(frame.payload);
			return;
		}

		const pending = this._pending.get(frame.seq);
		if (!pending || frame.id !== pending.cmd)
			return;
		this._pending.delete(frame.seq);
		clearTimeout(pending.timer);

		if (frame.type === FrameType.RSP)
			pending.resolve(frame.payload);
		else if (frame.type === FrameType.ERR)
			pending.reject(new DeviceError(pending.cmd, frame.payload.length ? frame.payload[0] : 0));
	}
}

// --- transports ---
//
// open() connects (again), write(bytes) sends, close() disconnects for good.
// The Device sets onData(bytes) and onLost() (link dropped without close()).

const BLE_CONNECT_TIMEOUT_MS = 10000;

// fits the default ATT MTU (23); the firmware accepts writes of up to 100 bytes
const BLE_WRITE_CHUNK = 20;

function withTimeout(promise, ms, onTimeout) {
	let timer;
	const timeout = new Promise((_, reject) => {
		timer = setTimeout(() => {
			onTimeout?.();
			reject(new Error("timed out"));
		}, ms);
	});
	return Promise.race([promise, timeout]).finally(() => clearTimeout(timer));
}

export class BleTransport {
	static get supported() {
		return typeof navigator !== "undefined" && !!navigator.bluetooth;
	}

	// shows the browser's device picker
	static async request() {
		const device = await navigator.bluetooth.requestDevice({
			filters: [{ name: DEVICE_NAME }],
			optionalServices: [NUS_SERVICE],
		});
		return new BleTransport(device);
	}

	constructor(device) {
		this.device = device;
		this.kind = "BLE";
		this.onData = null;
		this.onLost = null;
		this._rx = null;
		this._open = false;
		this._writes = Promise.resolve();

		this._onNotify = e => {
			const v = e.target.value;
			this.onData?.(new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength)));
		};
		device.addEventListener("gattserverdisconnected", () => {
			const lost = this._open;
			this._open = false;
			if (lost)
				this.onLost?.();
		});
	}

	get connected() {
		return this._open && this.device.gatt.connected;
	}

	async open() {
		const gatt = this.device.gatt;
		const server = await withTimeout(gatt.connect(), BLE_CONNECT_TIMEOUT_MS, () => gatt.disconnect());
		try {
			const service = await server.getPrimaryService(NUS_SERVICE);
			this._rx = await service.getCharacteristic(NUS_RX);
			const tx = await service.getCharacteristic(NUS_TX);
			tx.addEventListener("characteristicvaluechanged", this._onNotify);
			// the device sends nothing until notifications are enabled
			await tx.startNotifications();
		} catch (e) {
			gatt.disconnect();
			throw e;
		}
		this._writes = Promise.resolve();
		this._open = true;
	}

	// GATT operations must not overlap, so writes are queued
	write(data) {
		const rx = this._rx;
		const op = this._writes.then(async () => {
			for (let i = 0; i < data.length; i += BLE_WRITE_CHUNK) {
				const chunk = data.slice(i, i + BLE_WRITE_CHUNK);
				if (rx.writeValueWithResponse)
					await rx.writeValueWithResponse(chunk);
				else
					await rx.writeValue(chunk);
			}
		});
		this._writes = op.catch(() => {});
		return op;
	}

	async close() {
		this._open = false;
		if (this.device.gatt.connected)
			this.device.gatt.disconnect();
	}
}

const FTDI_VID = 0x0403;

export class SerialTransport {
	static get supported() {
		return typeof navigator !== "undefined" && !!navigator.serial;
	}

	// shows the browser's port picker
	static async request() {
		return new SerialTransport(await navigator.serial.requestPort({ filters: [{ usbVendorId: FTDI_VID }] }));
	}

	constructor(port) {
		this.port = port;
		this.kind = "USB";
		this.onData = null;
		this.onLost = null;
		this._open = false;
		this._reader = null;
		this._writer = null;
		this._readLoopDone = null;
	}

	get connected() {
		return this._open;
	}

	async open() {
		try {
			await this.port.open({ baudRate: 115200 });
		} catch (e) {
			// after unplugging, the browser may hand out a new port object for the same adapter
			const { usbVendorId, usbProductId } = this.port.getInfo();
			const ports = await navigator.serial.getPorts();
			const other = ports.find(p => p !== this.port && p.getInfo().usbVendorId === usbVendorId && p.getInfo().usbProductId === usbProductId);
			if (!other)
				throw e;
			this.port = other;
			await this.port.open({ baudRate: 115200 });
		}

		this._reader = this.port.readable.getReader();
		this._writer = this.port.writable.getWriter();
		this._open = true;
		this._readLoopDone = this._readLoop();
	}

	async _readLoop() {
		try {
			for (;;) {
				const { value, done } = await this._reader.read();
				if (done)
					break;
				if (value)
					this.onData?.(value);
			}
		} catch {
			// unplugged or a read error; handled below
		}

		const lost = this._open;
		this._open = false;
		try { this._reader.releaseLock(); } catch { }
		try { this._writer.releaseLock(); } catch { }
		try { await this.port.close(); } catch { }
		if (lost)
			this.onLost?.();
	}

	async write(data) {
		await this._writer.write(data);
	}

	async close() {
		if (!this._open)
			return;
		this._open = false;
		await this._reader.cancel().catch(() => {});
		await this._readLoopDone;
	}
}
