/*
 * OBD12 web app: the CLI's commands (info, power, can-stats, vin, obd, live,
 * log, ping, reset, clear-bonds) over one connection that stays open. If the
 * link drops, it reconnects to the same device without the picker, and the
 * Device restores event subscriptions and live polling.
 */

import {
	BleTransport, CAN_STATES, Device, DeviceError, Err, FAULTS, InfoFlag, OBD_ID_BROADCAST, RESET_CAUSES,
	SerialTransport, Topic, toHex,
} from "./obd12.js";
import { PIDS, decode, decodeDtcs, display } from "./pids.js";

const DEFAULT_LIVE_PIDS = [0x0C, 0x0D, 0x05, 0x0F, 0x11];
const MAX_LIVE_PIDS = 16;
const MIN_INTERVAL_MS = 50;
const RECONNECT_MAX_DELAY_MS = 10000;
const LOG_MAX_LINES = 2000;
const MAX_RESULTS = 20;

// time for the firmware to boot after CMD_RESET, before it answers again
const USB_REBOOT_MS = 3000;

const OBD_PRESETS = [
	["Supported PIDs", "01 00"],
	["RPM", "01 0C"],
	["Stored DTCs", "03"],
	["Pending DTCs", "07"],
	["VIN", "09 02"],
];

const NRC_NAMES = {
	0x10: "general reject",
	0x11: "service not supported",
	0x12: "sub-function not supported",
	0x13: "incorrect message length",
	0x21: "busy, repeat request",
	0x22: "conditions not correct",
	0x31: "request out of range",
	0x33: "security access denied",
	0x78: "response pending",
};

const $ = id => document.getElementById(id);

function el(tag, className, text) {
	const e = document.createElement(tag);
	if (className)
		e.className = className;
	if (text !== undefined)
		e.textContent = text;
	return e;
}

const hex2 = n => n.toString(16).toUpperCase().padStart(2, "0");
const hex3 = n => "0x" + n.toString(16).toUpperCase().padStart(3, "0");

// "01 0C", "01,0c" or "010C" -> [0x01, 0x0C]; null if invalid
function parseHex(text) {
	const bytes = [];
	for (const token of text.trim().split(/[\s,]+/).filter(Boolean)) {
		if (!/^[0-9a-f]+$/i.test(token))
			return null;
		const padded = token.length % 2 ? "0" + token : token;
		for (let i = 0; i < padded.length; i += 2)
			bytes.push(parseInt(padded.slice(i, i + 2), 16));
	}
	return bytes;
}

// --- settings, per browser ---

function load(key, fallback) {
	try {
		const v = localStorage.getItem("obd12." + key);
		return v === null ? fallback : JSON.parse(v);
	} catch {
		return fallback;
	}
}

function save(key, value) {
	try {
		localStorage.setItem("obd12." + key, JSON.stringify(value));
	} catch { }
}

const settings = {
	pids: load("pids", DEFAULT_LIVE_PIDS),
	extra: load("extra", ""),
	interval: load("interval", 500),
	imperial: load("imperial", false),
	log: load("log", false),
	tab: load("tab", "live"),
};

// --- toast and log ---

let toastTimer = null;

function toast(text, ms = 4000) {
	const t = $("toast");
	t.textContent = text;
	t.hidden = false;
	clearTimeout(toastTimer);
	toastTimer = setTimeout(() => { t.hidden = true; }, ms);
}

function errorText(e) {
	if (e instanceof DeviceError) {
		switch (e.code) {
			case Err.NOT_AUTHORIZED:
				return "This needs a paired link. Accept the pairing prompt and enter the passkey (shown by 'obd12 info' over USB), then try again.";
			case Err.TIMEOUT: return "No ECU answered.";
			case Err.CAN: return "CAN bus not available. Is the ignition on?";
			case Err.BUSY: return "The device is busy. Try again.";
			case Err.UNAVAILABLE: return "Not available on this device.";
		}
	}
	return e?.message || String(e);
}

function toastError(e) {
	const pairing = e instanceof DeviceError && e.code === Err.NOT_AUTHORIZED;
	toast(errorText(e), pairing ? 9000 : 4000);
	logLine(errorText(e), "error");
}

function logLine(text, kind = "") {
	const log = $("log");
	const atBottom = log.scrollHeight - log.scrollTop - log.clientHeight < 24;

	const line = el("div", kind);
	const now = new Date();
	line.append(el("time", "", now.toTimeString().slice(0, 8) + "." + String(now.getMilliseconds()).padStart(3, "0")), text);
	log.append(line);
	while (log.childElementCount > LOG_MAX_LINES)
		log.firstElementChild.remove();

	if (atBottom)
		log.scrollTop = log.scrollHeight;
}

// runs an async UI action, reporting errors; the button is disabled meanwhile
async function action(button, fn) {
	if (button)
		button.disabled = true;
	try {
		return await fn();
	} catch (e) {
		toastError(e);
	} finally {
		if (button)
			button.disabled = !connected();
	}
}

// --- connection ---

let dev = null;             // this session's Device, kept across reconnects
let state = "disconnected";
let wakeReconnect = null;

const connected = () => state === "connected";

function setState(s) {
	state = s;
	const status = $("status");
	status.dataset.state = s;
	$("status-text").textContent = {
		disconnected: "Disconnected",
		connecting: "Connecting…",
		connected: `Connected · ${dev?.transport.kind}`,
		reconnecting: "Reconnecting…",
	}[s];

	$("connect-ble").hidden = !!dev;
	$("connect-usb").hidden = !!dev || !SerialTransport.supported;
	$("disconnect").hidden = !dev;
	$("connect-ble").disabled = !BleTransport.supported;
	if (!dev)
		$("can-chip").hidden = true;

	for (const b of document.querySelectorAll(".needs-link"))
		b.disabled = !connected();
	updateLiveButton();
}

function desiredTopics() {
	return Topic.CAN_STATUS | ($("log-enable").checked ? Topic.LOG : 0) | ($("power-stream").checked ? Topic.POWER : 0);
}

async function connect(Transport) {
	let transport;
	try {
		transport = await Transport.request();
	} catch (e) {
		if (e.name !== "NotFoundError")   // picker cancelled
			toastError(e);
		return;
	}

	const session = new Device(transport);
	dev = session;
	bindEvents(session);
	setState("connecting");

	try {
		await session.open();
		await session.setEvents(desiredTopics());
	} catch (e) {
		if (dev === session) {
			dev = null;
			setState("disconnected");
		}
		await session.close().catch(() => {});
		toastError(e);
		return;
	}

	if (dev !== session)
		return;
	setState("connected");
	logLine(`Connected over ${transport.kind}`, "app");
	refreshInfo();
}

function bindEvents(session) {
	session.onLog(text => logLine(text));
	session.onCanStatus(s => {
		showCanState(s);
		logLine(`CAN state: ${CAN_STATES[s] ?? s}`, "can");
	});
	session.onPower(renderPower);
	session.onLiveData(onLiveData);
	session.onLost(() => {
		if (dev !== session)
			return;
		setState("reconnecting");
		logLine("Connection lost, reconnecting", "app");
		reconnect(session);
	});
}

async function reconnect(session) {
	let delay = 500;
	while (dev === session) {
		try {
			await session.open();
			if (dev !== session)
				return;
			setState("connected");
			logLine("Reconnected", "app");
			refreshInfo();
			return;
		} catch {
			// opened but restoring failed: start over cleanly
			await session.transport.close().catch(() => {});
		}

		await new Promise(resolve => {
			wakeReconnect = resolve;
			setTimeout(resolve, delay);
		});
		wakeReconnect = null;
		delay = Math.min(delay * 2, RECONNECT_MAX_DELAY_MS);
	}
}

async function disconnect() {
	const session = dev;
	dev = null;
	wakeReconnect?.();
	setState("disconnected");
	clearInterval(staleTimer);
	staleTimer = null;
	releaseWakeLock();
	if (session) {
		await session.close().catch(() => {});
		logLine("Disconnected", "app");
	}
}

function showCanState(s) {
	const chip = $("can-chip");
	chip.hidden = false;
	chip.textContent = "CAN " + (CAN_STATES[s] ?? s);
	chip.className = "chip " + (s === 0 ? "ok" : s === 1 || s === 4 ? "warn" : "bad");
}

async function updateTopics() {
	save("log", $("log-enable").checked);
	if (connected())
		await action(null, () => dev.setEvents(desiredTopics()));
}

// --- live data ---

const liveData = new Map();     // pid -> { data, seen }
let liveRate = [];              // arrival times of the last events
let staleTimer = null;
let restartTimer = null;
let wakeLock = null;

function livePids() {
	const extra = parseHex(settings.extra);
	if (extra === null)
		throw new Error("Other PIDs: enter hex bytes, e.g. 1C 33");
	return [...new Set([...settings.pids, ...extra])].slice(0, MAX_LIVE_PIDS);
}

function liveInterval() {
	const v = parseInt($("live-interval").value, 10);
	return Number.isFinite(v) ? Math.max(MIN_INTERVAL_MS, v) : 500;
}

function updateLiveButton() {
	const b = $("live-toggle");
	b.textContent = dev?.live ? "Stop" : "Start";
	b.classList.toggle("danger", !!dev?.live);
	b.classList.toggle("primary", !dev?.live);
}

function renderPidChips() {
	const chips = $("pid-chips");
	chips.replaceChildren();
	for (const info of PIDS.values()) {
		const b = el("button", "", info.label);
		b.setAttribute("aria-pressed", settings.pids.includes(info.pid));
		b.addEventListener("click", () => {
			settings.pids = settings.pids.includes(info.pid)
				? settings.pids.filter(p => p !== info.pid)
				: [...settings.pids, info.pid];
			save("pids", settings.pids);
			b.setAttribute("aria-pressed", settings.pids.includes(info.pid));
			liveConfigChanged();
		});
		chips.append(b);
	}
}

function renderTiles(pids) {
	const tiles = $("live-tiles");
	tiles.replaceChildren();
	for (const pid of pids) {
		const tile = el("div", "tile stale");
		tile.dataset.pid = pid;
		tile.append(el("div", "label", PIDS.get(pid)?.label ?? `PID ${hex2(pid)}`), el("div", "value", "—"));
		tiles.append(tile);
		if (liveData.has(pid))
			renderTile(tile, liveData.get(pid).data);
	}
}

function renderTile(tile, data) {
	const pid = Number(tile.dataset.pid);
	const value = tile.querySelector(".value");
	const decoded = decode(pid, data);
	if (!decoded) {
		value.className = "value raw";
		value.textContent = toHex(data) || "—";
		return;
	}
	const shown = display(decoded, settings.imperial);
	value.className = "value";
	value.textContent = shown.value.toFixed(decoded.digits);
	value.append(el("span", "unit", shown.unit));
}

function onLiveData(values) {
	const now = performance.now();
	liveRate = [...liveRate.filter(t => now - t < 5000), now];

	for (const [pid, data] of values) {
		liveData.set(pid, { data, seen: now });
		const tile = $("live-tiles").querySelector(`[data-pid="${pid}"]`);
		if (tile) {
			renderTile(tile, data);
			tile.classList.remove("stale");
		}
	}

	const rate = liveRate.length > 1 ? (liveRate.length - 1) / ((now - liveRate[0]) / 1000) : 0;
	$("live-meta").textContent = `Updated ${new Date().toLocaleTimeString()}` + (rate ? ` · ${rate.toFixed(1)}/s` : "");
}

// dims tiles whose PID stopped answering
function markStale() {
	const limit = Math.max(2000, liveInterval() * 3);
	const now = performance.now();
	for (const tile of $("live-tiles").children) {
		const entry = liveData.get(Number(tile.dataset.pid));
		tile.classList.toggle("stale", !entry || now - entry.seen > limit);
	}
}

async function startLive() {
	const pids = livePids();
	if (pids.length === 0)
		throw new Error("Choose at least one PID.");
	await dev.liveStart(pids, liveInterval());
	renderTiles(pids);
	staleTimer ??= setInterval(markStale, 1000);
	acquireWakeLock();
}

async function toggleLive() {
	await action($("live-toggle"), async () => {
		if (dev.live) {
			clearInterval(staleTimer);
			staleTimer = null;
			releaseWakeLock();
			await dev.liveStop();
		} else {
			await startLive();
		}
	});
	updateLiveButton();
}

// while streaming, the latest LIVE_START replaces the polling configuration
function liveConfigChanged() {
	if (!dev?.live)
		return;
	clearTimeout(restartTimer);
	restartTimer = setTimeout(() => {
		if (connected() && dev.live)
			startLive().catch(toastError);
	}, 500);
}

// keeps the screen on while streaming, where supported
async function acquireWakeLock() {
	try {
		wakeLock = await navigator.wakeLock?.request("screen");
	} catch { }
}

function releaseWakeLock() {
	wakeLock?.release().catch(() => {});
	wakeLock = null;
}

function setUnits(imperial) {
	settings.imperial = imperial;
	save("imperial", imperial);
	for (const b of $("units").children)
		b.setAttribute("aria-pressed", (b.dataset.units === "imperial") === imperial);
	for (const tile of $("live-tiles").children) {
		const entry = liveData.get(Number(tile.dataset.pid));
		if (entry)
			renderTile(tile, entry.data);
	}
}

// --- vehicle ---

// a short human reading of a positive response, if there is one
function describeResponse(msg) {
	if (msg[0] === 0x41 && msg.length >= 2) {
		const pid = msg[1];
		if (pid % 0x20 === 0 && msg.length >= 6) {
			const supported = [];
			for (let i = 0; i < 32; i++)
				if (msg[2 + (i >> 3)] & (0x80 >> (i & 7)))
					supported.push(hex2(pid + i + 1));
			return supported.length ? `Supported PIDs: ${supported.join(" ")}` : "No PIDs supported in this range";
		}
		const decoded = decode(pid, msg.slice(2));
		if (decoded) {
			const shown = display(decoded, settings.imperial);
			return `${decoded.label}: ${shown.value.toFixed(decoded.digits)} ${shown.unit}`;
		}
	}
	if (msg[0] === 0x43 || msg[0] === 0x47 || msg[0] === 0x4A) {
		const codes = decodeDtcs(msg);
		return codes.length ? codes.join("  ") : "No trouble codes";
	}
	if (msg[0] === 0x49 && msg.length > 3) {
		const text = String.fromCharCode(...msg.slice(3));
		if (/^[\x20-\x7E]+$/.test(text))
			return text.trim();
	}
	return null;
}

async function sendObd() {
	const bytes = parseHex($("obd-request").value);
	if (!bytes || bytes.length < 1 || bytes.length > 7) {
		toast("Enter 1 to 7 hex bytes, e.g. 01 0C");
		return;
	}
	const dest = parseInt($("obd-dest").value, 16);

	const card = el("div", "result");
	const head = el("div", "result-head");
	head.append(el("span", "req", toHex(bytes)), el("span", "", `${dest === OBD_ID_BROADCAST ? "broadcast" : hex3(dest)} · ${new Date().toLocaleTimeString()}`));
	const body = el("div", "meta", "Waiting…");
	card.append(head, body);
	const results = $("obd-results");
	results.prepend(card);
	while (results.childElementCount > MAX_RESULTS)
		results.lastElementChild.remove();

	await action($("obd-send"), async () => {
		try {
			const responses = await dev.obdRequest(bytes, dest);
			body.remove();
			for (const r of responses) {
				const ecu = el("div", r.negative ? "ecu negative" : "ecu");
				ecu.append(el("div", "id", `ECU ${hex3(r.ecuId)}`), el("div", "bytes", toHex(r.message)));
				const text = r.negative
					? `Negative response, NRC 0x${hex2(r.nrc)}` + (NRC_NAMES[r.nrc] ? ` (${NRC_NAMES[r.nrc]})` : "")
					: describeResponse(r.message);
				if (text)
					ecu.append(el("div", "decoded", text));
				card.append(ecu);
			}
		} catch (e) {
			body.className = "error";
			body.textContent = errorText(e);
			throw e;
		}
	});
}

// --- device ---

function renderKv(dl, rows) {
	dl.replaceChildren();
	for (const [k, v] of rows)
		dl.append(el("dt", "", k), el("dd", "", v));
}

async function refreshInfo() {
	await action($("info-refresh"), async () => {
		const info = await dev.getInfo();
		const flags = Object.keys(InfoFlag).filter(k => info.flags & InfoFlag[k]).map(k => k.toLowerCase().replace("_", " "));
		const rows = [
			["Protocol", String(info.protoVersion)],
			["Firmware", info.fwVersion.join(".")],
			["HW config", "0b" + info.hwconf.toString(2).padStart(2, "0")],
			["Reset cause", RESET_CAUSES[info.resetCause] ?? String(info.resetCause)],
			["Last fault", (FAULTS[info.lastFault] ?? String(info.lastFault)) + (info.lastFaultInfo ? ` (0x${info.lastFaultInfo.toString(16).toUpperCase().padStart(8, "0")})` : "")],
			["CAN state", CAN_STATES[info.canState] ?? String(info.canState)],
		];
		if (info.uid.some(Boolean))
			rows.push(["UID", info.uid.map(w => w.toString(16).toUpperCase().padStart(8, "0")).join("")]);
		rows.push(["BLE address", info.bleMac]);
		if (info.blePasskey)
			rows.push(["BLE passkey", String(info.blePasskey).padStart(6, "0")]);
		rows.push(["Flags", flags.join(", ") || "–"]);
		renderKv($("info"), rows);
		showCanState(info.canState);
	});
}

function renderPower(p) {
	renderKv($("power"), [
		["USB regulator", `${p.usbRegulatorOk ? "ok" : "off"} · ${p.usbRegulatorMv} mV`],
		["CAN regulator", `${p.canRegulatorOk ? "ok" : "off"} · ${p.canRegulatorMv} mV`],
	]);
}

// first tap arms the button, a second tap within 3 s runs it
function confirmTap(button, fn) {
	const label = button.textContent;
	let timer = null;
	button.addEventListener("click", () => {
		if (!button.classList.contains("armed")) {
			button.classList.add("armed");
			button.textContent = button.dataset.confirm;
			timer = setTimeout(() => {
				button.classList.remove("armed");
				button.textContent = label;
			}, 3000);
			return;
		}
		clearTimeout(timer);
		button.classList.remove("armed");
		button.textContent = label;
		action(button, fn);
	});
}

// --- setup ---

function selectTab(name) {
	if (!$("tab-" + name))
		name = "live";
	for (const b of document.querySelectorAll(".tabs button"))
		b.setAttribute("aria-selected", b.dataset.tab === name);
	for (const p of document.querySelectorAll(".panel"))
		p.hidden = p.id !== "tab-" + name;
	save("tab", name);
}

function init() {
	$("unsupported").hidden = BleTransport.supported;
	$("connect-ble").addEventListener("click", () => connect(BleTransport));
	$("connect-usb").addEventListener("click", () => connect(SerialTransport));
	$("disconnect").addEventListener("click", disconnect);

	for (const b of document.querySelectorAll(".tabs button"))
		b.addEventListener("click", () => selectTab(b.dataset.tab));
	selectTab(settings.tab);

	// live
	renderPidChips();
	$("live-extra").value = settings.extra;
	$("live-interval").value = settings.interval;
	$("live-extra").addEventListener("change", e => {
		settings.extra = e.target.value;
		save("extra", settings.extra);
		liveConfigChanged();
	});
	$("live-interval").addEventListener("change", () => {
		settings.interval = liveInterval();
		$("live-interval").value = settings.interval;
		save("interval", settings.interval);
		liveConfigChanged();
	});
	$("live-toggle").addEventListener("click", toggleLive);
	for (const b of $("units").children)
		b.addEventListener("click", () => setUnits(b.dataset.units === "imperial"));
	setUnits(settings.imperial);
	try {
		renderTiles(livePids());
	} catch {
		renderTiles(settings.pids);
	}

	// vehicle
	const destSelect = $("obd-dest");
	destSelect.append(new Option("7DF (all)", "7DF"));
	for (let id = 0x7E0; id <= 0x7E7; id++)
		destSelect.append(new Option(id.toString(16).toUpperCase(), id.toString(16)));
	for (const [label, request] of OBD_PRESETS) {
		const b = el("button", "needs-link", label);
		b.disabled = true;
		b.addEventListener("click", () => {
			$("obd-request").value = request;
			sendObd();
		});
		$("obd-presets").append(b);
	}
	$("obd-send").addEventListener("click", sendObd);
	$("obd-request").addEventListener("keydown", e => {
		if (e.key === "Enter" && connected())
			sendObd();
	});
	$("vin-read").addEventListener("click", () => action($("vin-read"), async () => {
		$("vin").textContent = await dev.getVin();
	}));

	// device
	$("info-refresh").addEventListener("click", refreshInfo);
	$("power-refresh").addEventListener("click", () => action($("power-refresh"), async () => renderPower(await dev.getPower())));
	$("power-stream").addEventListener("change", updateTopics);
	$("can-refresh").addEventListener("click", () => action($("can-refresh"), async () => {
		const stats = await dev.getCanStats();
		renderKv($("can-stats"), Object.entries(stats).map(([k, v]) => [k.replaceAll("_", " "), v.toLocaleString()]));
	}));
	$("ping").addEventListener("click", () => action($("ping"), async () => {
		const start = performance.now();
		await dev.ping(new TextEncoder().encode("obd12"));
		$("ping-result").textContent = `Pong in ${(performance.now() - start).toFixed(1)} ms`;
	}));
	confirmTap($("reset"), async () => {
		const session = dev;
		await session.reset();
		toast("The device is rebooting.");

		// a BLE link drops and reconnects by itself; the USB serial port stays open
		if (session.transport.kind === "USB") {
			setTimeout(() => {
				if (dev === session && connected())
					session.restore().then(refreshInfo).catch(toastError);
			}, USB_REBOOT_MS);
		}
	});
	confirmTap($("clear-bonds"), async () => {
		await dev.bleClearBonds();
		toast("BLE bonds cleared.");
	});

	// log
	$("log-enable").checked = settings.log;
	$("log-enable").addEventListener("change", updateTopics);
	$("log-clear").addEventListener("click", () => $("log").replaceChildren());

	document.addEventListener("visibilitychange", () => {
		if (document.hidden)
			return;
		wakeReconnect?.();   // retry right away when the page comes back
		// the browser releases the wake lock while the page is hidden
		if (dev?.live && (!wakeLock || wakeLock.released))
			acquireWakeLock();
	});

	setState("disconnected");
}

init();
