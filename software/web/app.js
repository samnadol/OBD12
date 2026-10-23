/*
 * OBD12 web app: the connected vehicle, its live data (with CSV recording)
 * and trouble codes, over one connection that stays open. If the link drops,
 * it reconnects to the same device without the picker, and the Device
 * restores event subscriptions and live polling.
 *
 * On every (re)connection it reads the VIN, looks up the vehicle (vin.js),
 * reads the mode 01 PIDs the vehicle supports (those become the gauges to
 * choose from) and checks for trouble codes.
 */

import { BleTransport, CAN_STATES, Device, DeviceError, Err, SerialTransport, Topic, toHex } from "./obd12.js";
import { PIDS, decode, decodeDtcs, display, displayUnit, isRangePid, pidLabel, pidsFromBitmap } from "./pids.js";
import { isVin, lookupVehicle } from "./vin.js";

const DEFAULT_LIVE_PIDS = [0x0C, 0x0D, 0x05, 0x0F, 0x11];
const MAX_LIVE_PIDS = 32;

// gauge choices until a vehicle's supported PIDs have been read
const FALLBACK_PIDS = [0x04, 0x05, 0x06, 0x07, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x1F, 0x2F, 0x42, 0x46, 0x5C];
const RATES_MS = [200, 500, 1000];
const RECONNECT_MAX_DELAY_MS = 10000;
const MAX_CACHED_VEHICLES = 20;

// time for the firmware to boot after CMD_RESET, before it answers again
const USB_REBOOT_MS = 3000;

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

const nearestRate = ms => RATES_MS.reduce((best, r) => Math.abs(r - ms) < Math.abs(best - ms) ? r : best);

const settings = {
	pids: load("pids", DEFAULT_LIVE_PIDS),
	interval: nearestRate(load("interval", 500)),
	imperial: load("imperial", false),
	supported: load("supported", null),    // the last vehicle's supported PIDs, without 0x00, 0x20, ...
	vehicles: load("vehicles", {}),        // VIN -> lookupVehicle() result, most recent last
};

// --- toast ---

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
				return "This needs a paired link. Accept the pairing prompt and enter the passkey (shown when connected over USB), then try again.";
			case Err.TIMEOUT: return "The vehicle didn't answer. Is the ignition on?";
			case Err.CAN: return "Can't reach the vehicle. Is the ignition on?";
			case Err.BUSY: return "The adapter is busy. Try again.";
			case Err.UNAVAILABLE: return "This adapter doesn't support that.";
		}
	}
	return e?.message || String(e);
}

function toastError(e) {
	const pairing = e instanceof DeviceError && e.code === Err.NOT_AUTHORIZED;
	toast(errorText(e), pairing ? 9000 : 4000);
	console.warn(e);
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
			button.disabled = false;
		refreshControls();
	}
}

// enables the buttons that make sense in the current state
function refreshControls() {
	for (const b of document.querySelectorAll(".needs-link"))
		b.disabled = !connected();
	updateLiveButton();
	updateRecordingUi();
}

// --- connection ---

let dev = null;             // this session's Device, kept across reconnects
let state = "disconnected";
let wakeReconnect = null;

const connected = () => state === "connected";

function setState(s) {
	state = s;
	$("status").dataset.state = s;
	$("status-text").textContent = {
		disconnected: "Not connected",
		connecting: "Connecting…",
		connected: "Connected",
		reconnecting: "Reconnecting…",
	}[s];

	$("welcome").hidden = !!dev;
	$("disconnect").hidden = !dev;
	$("connect-usb").hidden = !SerialTransport.supported;
	$("connect-ble").disabled = !BleTransport.supported || s === "connecting";
	updateDashboard();
	refreshControls();
}

// the dashboard stays up after disconnecting while a recording is unsaved
function updateDashboard() {
	$("dashboard").hidden = !dev && !hasUnsaved();
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
	resetVehicle();
	setState("connecting");

	try {
		await session.open();
		await session.setEvents(Topic.CAN_STATUS);
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
	onConnected(session);
}

function onConnected(session) {
	refreshInfo();
	loadVehicle(session);
}

function bindEvents(session) {
	session.onCanStatus(showBusState);
	session.onLiveData(onLiveData);
	session.onLost(() => {
		if (dev !== session)
			return;
		setState("reconnecting");
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
			onConnected(session);
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
	clearInterval(staleTimer);
	staleTimer = null;
	releaseWakeLock();
	stopRecording();
	supportedFresh = false;
	scanFailed = false;
	setState("disconnected");
	updatePidStatus();
	updateLiveHint();
	if (session)
		await session.close().catch(() => {});
}

// --- vehicle ---

const vehicle = {
	vin: null,
	reading: false,         // the VIN is being read
	readFailed: false,
	looking: false,         // the vehicle is being looked up
	lookupFailed: false,
	progress: null,         // database download, 0..1
};

function resetVehicle() {
	Object.assign(vehicle, { vin: null, reading: false, readFailed: false, looking: false, lookupFailed: false, progress: null });
	renderVehicle();
	$("bus").hidden = true;
}

function rememberVehicle(vin, info) {
	const vehicles = { ...settings.vehicles };
	delete vehicles[vin];
	vehicles[vin] = info;
	const vins = Object.keys(vehicles);
	for (const old of vins.slice(0, Math.max(0, vins.length - MAX_CACHED_VEHICLES)))
		delete vehicles[old];
	settings.vehicles = vehicles;
	save("vehicles", vehicles);
}

function renderVehicle() {
	const { vin } = vehicle;
	const info = vin ? settings.vehicles[vin] : undefined;

	$("veh-year").hidden = !info?.year;
	$("veh-year").textContent = info?.year ?? "";
	$("veh-name").textContent = info ? `${info.make} ${info.model}`.trim()
		: info === null ? "Unknown vehicle"
		: "Your vehicle";

	const specs = info ? [info.trim, info.body, info.engine, info.drive].filter(Boolean) : [];
	$("veh-specs").replaceChildren(...specs.map(s => el("li", "", s)));
	$("veh-specs").hidden = specs.length === 0;

	$("vin").hidden = !vin;
	$("vin").textContent = vin ?? "";
	$("vin").title = vin ? "Vehicle identification number" : "";

	let note = "", retry = false;
	if (vehicle.reading)
		note = "Reading the VIN…";
	else if (vehicle.readFailed)
		[note, retry] = ["Couldn't read the VIN. Turn the ignition on, then try again.", true];
	else if (vehicle.looking)
		note = vehicle.progress === null ? "Looking up the vehicle…" : `Downloading vehicle database, ${Math.round(vehicle.progress * 100)}%`;
	else if (vehicle.lookupFailed)
		[note, retry] = ["Couldn't look up this VIN. Check the internet connection, then try again.", true];
	else if (info === null)
		note = "This VIN isn't in the NHTSA database.";
	$("veh-note-text").textContent = note;
	$("veh-retry").hidden = !retry;
	$("veh-note").hidden = !note;
}

async function readVin(session) {
	Object.assign(vehicle, { reading: true, readFailed: false });
	renderVehicle();
	try {
		const vin = (await session.getVin()).replace(/[^\x21-\x7E]/g, "").toUpperCase();
		if (dev === session)
			vehicle.vin = vin;
	} catch (e) {
		if (dev === session)
			vehicle.readFailed = true;
		console.warn("VIN:", errorText(e));
	} finally {
		vehicle.reading = false;
		renderVehicle();
	}
}

async function identifyVehicle() {
	const vin = vehicle.vin;
	if (!vin || vin in settings.vehicles) {
		renderVehicle();
		return;
	}
	if (!isVin(vin)) {
		rememberVehicle(vin, null);
		renderVehicle();
		return;
	}

	Object.assign(vehicle, { looking: true, lookupFailed: false, progress: null });
	renderVehicle();
	try {
		const info = await lookupVehicle(vin, progress => {
			if (vehicle.vin !== vin)
				return;
			vehicle.progress = progress;
			renderVehicle();
		});
		rememberVehicle(vin, info);
	} catch (e) {
		if (vehicle.vin === vin)
			vehicle.lookupFailed = true;
		console.warn("Vehicle lookup:", e);
	} finally {
		if (vehicle.vin === vin) {
			vehicle.looking = false;
			vehicle.progress = null;
		}
		renderVehicle();
	}
}

async function retryVehicle() {
	if (vehicle.readFailed) {
		await readVin(dev);
		if (vehicle.vin)
			identifyVehicle();
	} else {
		identifyVehicle();
	}
}

async function loadVehicle(session) {
	await readVin(session);
	identifyVehicle();      // runs over the internet, alongside the requests below
	if (dev === session)
		await scanPids(session);
	if (dev === session)
		await checkCodes(session);
}

const BUS_STATES = [
	["ok", "Vehicle connected"],
	["warn", "Vehicle bus errors"],
	["bad", "Vehicle bus errors"],
	["bad", "Vehicle bus off"],
	["warn", "Waiting for the vehicle. Turn the ignition on."],
];

function showBusState(s) {
	const [kind, text] = BUS_STATES[s] ?? ["warn", `Vehicle bus state ${s}`];
	const bus = $("bus");
	bus.hidden = false;
	bus.className = "bus " + kind;
	bus.title = "CAN " + (CAN_STATES[s] ?? s);
	$("bus-text").textContent = text;
}

// --- live data ---

const liveData = new Map();     // pid -> { data, seen }
let staleTimer = null;
let restartTimer = null;
let wakeLock = null;

// the chips: the vehicle's supported PIDs, once known
const chipPids = () => settings.supported ?? FALLBACK_PIDS;

const selectedPids = () => settings.pids.filter(p => chipPids().includes(p));

function updateLiveButton() {
	const b = $("live-toggle");
	b.textContent = dev?.live ? "Stop" : "Start";
	b.classList.toggle("primary", !dev?.live);
	b.classList.toggle("ghost", !!dev?.live);
}

function updateLiveHint() {
	const hint = $("live-hint");
	if (dev?.live)
		hint.textContent = "";
	else if (selectedPids().length === 0)
		hint.textContent = "No gauges chosen. Open Choose gauges below to add some.";
	else
		hint.textContent = connected() ? "Press Start to read the gauges." : "";
	hint.hidden = !hint.textContent;
}

function renderPidChips() {
	const chips = $("pid-chips");
	chips.replaceChildren();
	for (const pid of chipPids()) {
		const b = el("button", "", pidLabel(pid));
		b.title = `PID ${hex2(pid)}`;
		b.setAttribute("aria-pressed", settings.pids.includes(pid));
		b.addEventListener("click", () => {
			const selected = settings.pids.includes(pid);
			if (!selected && selectedPids().length >= MAX_LIVE_PIDS) {
				toast(`Choose up to ${MAX_LIVE_PIDS} gauges.`);
				return;
			}
			settings.pids = selected ? settings.pids.filter(p => p !== pid) : [...settings.pids, pid];
			save("pids", settings.pids);
			b.setAttribute("aria-pressed", !selected);
			gaugesChanged();
		});
		chips.append(b);
	}
	gaugesChanged();
}

function gaugesChanged() {
	updatePidStatus();
	updateLiveHint();
	if (!dev?.live)
		renderTiles(selectedPids());
	liveConfigChanged();
}

// one tile per field of a known PID, one tile of raw bytes for any other PID
function renderTiles(pids) {
	const tiles = $("live-tiles");
	tiles.replaceChildren();
	for (const pid of pids) {
		const fields = PIDS.get(pid)?.fields ?? [];
		const labels = fields.length ? fields.map(f => f.label) : [pidLabel(pid)];
		for (const label of labels) {
			const tile = el("div", "tile stale");
			tile.dataset.pid = pid;
			const value = el("div", "value", "–");
			tile.append(value, el("div", "label", label));
			tiles.append(tile);
		}
		if (liveData.has(pid))
			renderPid(pid, liveData.get(pid).data);
	}
}

function renderPid(pid, data) {
	const tiles = $("live-tiles").querySelectorAll(`[data-pid="${pid}"]`);
	const fields = PIDS.get(pid)?.fields ?? [];
	const decoded = decode(pid, data);

	tiles.forEach((tile, i) => {
		const value = tile.querySelector(".value");
		if (!decoded) {
			tile.hidden = i > 0;
			value.className = "value raw";
			value.textContent = toHex(data) || "–";
			return;
		}

		// fields the vehicle does not fill in (e.g. a second fuel rail) stay hidden
		const field = decoded.find(f => f.name === fields[i].name);
		tile.hidden = !field;
		if (!field)
			return;
		const shown = display(field, settings.imperial);
		value.className = "value";
		value.textContent = shown.value.toFixed(shown.digits);
		if (shown.unit)
			value.append(el("span", "unit", shown.unit));
	});
}

function onLiveData(values) {
	const now = performance.now();
	record(values);

	for (const [pid, data] of values) {
		liveData.set(pid, { data, seen: now });
		renderPid(pid, data);
		for (const tile of $("live-tiles").querySelectorAll(`[data-pid="${pid}"]`))
			tile.classList.remove("stale");
	}
}

// dims tiles whose PID stopped answering
function markStale() {
	const limit = Math.max(2000, settings.interval * 3);
	const now = performance.now();
	for (const tile of $("live-tiles").children) {
		const entry = liveData.get(Number(tile.dataset.pid));
		tile.classList.toggle("stale", !entry || now - entry.seen > limit);
	}
}

async function startLive() {
	const pids = selectedPids();
	if (pids.length === 0)
		throw new Error("Choose at least one gauge.");
	await dev.liveStart(pids, settings.interval);
	renderTiles(pids);
	staleTimer ??= setInterval(markStale, 1000);
	acquireWakeLock();
	updateLiveHint();
}

async function stopLive() {
	clearInterval(staleTimer);
	staleTimer = null;
	releaseWakeLock();
	stopRecording();
	await dev.liveStop();
	updateLiveHint();
}

async function toggleLive() {
	await action($("live-toggle"), () => dev.live ? stopLive() : startLive());
	updateLiveButton();
}

// while streaming, the latest LIVE_START replaces the polling configuration
function liveConfigChanged() {
	if (!dev?.live)
		return;
	clearTimeout(restartTimer);
	restartTimer = setTimeout(() => {
		if (connected() && dev.live && selectedPids().length)
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
	for (const [pid, entry] of liveData)
		renderPid(pid, entry.data);
}

function setRate(ms) {
	settings.interval = ms;
	save("interval", ms);
	for (const b of $("rates").children)
		b.setAttribute("aria-pressed", Number(b.dataset.ms) === ms);
	liveConfigChanged();
}

// --- recording ---
//
// Every EVT_LIVE_DATA becomes one row, so rows come at the live interval (or
// slower, if a polling pass takes longer). Raw bytes are kept and decoded on
// save, in the units selected then.

const recording = { active: false, start: 0, pids: [], rows: [], saved: true };
let recTimer = null;
let recArmTimer = null;

const hasUnsaved = () => !recording.saved && recording.rows.length > 0;

function record(values) {
	if (!recording.active)
		return;
	recording.rows.push({ t: Date.now(), values });
	for (const pid of values.keys())
		if (!recording.pids.includes(pid))
			recording.pids.push(pid);
}

async function startRecording() {
	await action($("rec-toggle"), async () => {
		if (!dev.live)
			await startLive();
	});
	if (!dev?.live)
		return;

	Object.assign(recording, { active: true, start: Date.now(), pids: selectedPids(), rows: [], saved: false });
	recTimer = setInterval(updateRecordingUi, 1000);
	updateRecordingUi();
}

function stopRecording() {
	if (!recording.active)
		return;
	recording.active = false;
	clearInterval(recTimer);
	recTimer = null;
	updateRecordingUi();
}

// starting over discards an unsaved recording, so that takes a second tap
function onRecordClick() {
	const b = $("rec-toggle");
	if (recording.active) {
		stopRecording();
		return;
	}
	if (hasUnsaved() && !b.classList.contains("armed")) {
		b.classList.add("armed");
		b.querySelector(".rec-label").textContent = b.dataset.confirm;
		recArmTimer = setTimeout(() => {
			b.classList.remove("armed");
			updateRecordingUi();
		}, 3000);
		return;
	}
	clearTimeout(recArmTimer);
	b.classList.remove("armed");
	startRecording();
}

function formatDuration(seconds) {
	const s = Math.max(0, Math.floor(seconds));
	const mmss = `${Math.floor(s / 60) % 60}`.padStart(s >= 3600 ? 2 : 1, "0") + ":" + `${s % 60}`.padStart(2, "0");
	return s >= 3600 ? `${Math.floor(s / 3600)}:${mmss}` : mmss;
}

function updateRecordingUi() {
	const b = $("rec-toggle");
	const n = recording.rows.length;
	if (!b.classList.contains("armed"))
		b.querySelector(".rec-label").textContent = recording.active ? "Stop recording" : "Record";
	b.classList.toggle("active", recording.active);
	b.disabled = !recording.active && !connected();

	const end = recording.active ? Date.now() : recording.rows.at(-1)?.t ?? recording.start;
	const duration = formatDuration((end - recording.start) / 1000);
	const samples = `${n.toLocaleString()} ${n === 1 ? "sample" : "samples"}`;
	$("recording").hidden = !recording.active && n === 0;
	$("recording").classList.toggle("active", recording.active);
	$("rec-status").textContent = recording.active
		? `Recording ${duration}, ${samples}`
		: `${duration} recorded, ${samples}${recording.saved ? ", saved" : ""}`;
	$("rec-save").hidden = recording.active;
	$("rec-discard").hidden = recording.active;
	updateDashboard();
}

// a column per field that occurs in the recording; PIDs without fields (or
// with too little data) get a column of raw hex
function recordingColumns() {
	const columns = [];
	for (const pid of recording.pids) {
		const fields = PIDS.get(pid)?.fields ?? [];
		const seen = new Set();
		let raw = false, any = false;
		for (const row of recording.rows) {
			const data = row.values.get(pid);
			if (!data)
				continue;
			any = true;
			const decoded = decode(pid, data);
			if (decoded)
				decoded.forEach(f => seen.add(f.name));
			else
				raw = true;
		}

		const shown = fields.filter(f => seen.has(f.name) || (!any && !f.present));
		for (const field of shown) {
			const unit = displayUnit(field.unit, settings.imperial);
			columns.push({ pid, field, header: unit ? `${field.name} (${unit})` : field.name });
		}
		if (raw || shown.length === 0)
			columns.push({ pid, header: `pid_${hex2(pid)} (hex)` });
	}
	return columns;
}

function recordingCsv() {
	const columns = recordingColumns();
	const lines = [["time", "elapsed_s", ...columns.map(c => c.header)].join(",")];

	for (const row of recording.rows) {
		const cells = [new Date(row.t).toISOString(), ((row.t - recording.start) / 1000).toFixed(3)];
		for (const { pid, field } of columns) {
			const data = row.values.get(pid);
			const decoded = data && decode(pid, data);
			if (!data) {
				cells.push("");
			} else if (field) {
				const f = decoded?.find(f => f.name === field.name);
				cells.push(f ? String(Number(display(f, settings.imperial).value.toFixed(3))) : "");
			} else {
				cells.push(decoded ? "" : toHex(data, ""));
			}
		}
		lines.push(cells.join(","));
	}

	// the BOM makes Excel read the file (and the ° in units) as UTF-8
	return "﻿" + lines.join("\r\n") + "\r\n";
}

function fileStamp(date) {
	const p = n => String(n).padStart(2, "0");
	return `${date.getFullYear()}${p(date.getMonth() + 1)}${p(date.getDate())}-${p(date.getHours())}${p(date.getMinutes())}${p(date.getSeconds())}`;
}

async function saveRecording() {
	const name = `obd12-${fileStamp(new Date(recording.start))}.csv`;
	const file = new File([recordingCsv()], name, { type: "text/csv" });

	// on phones the share sheet is the way to a file ("Save to Files" on iOS)
	if (matchMedia("(pointer: coarse)").matches && navigator.canShare?.({ files: [file] })) {
		try {
			await navigator.share({ files: [file] });
			recording.saved = true;
			updateRecordingUi();
			return;
		} catch (e) {
			if (e.name === "AbortError")
				return;
			// otherwise fall back to a download
		}
	}

	const url = URL.createObjectURL(file);
	const a = el("a");
	a.href = url;
	a.download = name;
	document.body.append(a);
	a.click();
	a.remove();
	setTimeout(() => URL.revokeObjectURL(url), 10000);
	recording.saved = true;
	updateRecordingUi();
}

// --- supported PIDs ---

let supportedFresh = false;     // settings.supported was read from the connected vehicle
let scanning = false;
let scanFailed = false;

async function scanPids(session) {
	scanning = true;
	updatePidStatus();
	try {
		const supported = pidsFromBitmap(await session.getSupportedPidsBitmap()).filter(p => !isRangePid(p));
		if (dev !== session)
			return;
		settings.supported = supported;
		save("supported", supported);
		supportedFresh = true;
		scanFailed = false;
		renderPidChips();       // also stops polling PIDs this vehicle does not have
	} catch (e) {
		scanFailed = true;
		console.warn("Supported PIDs:", errorText(e));
	} finally {
		scanning = false;
		updatePidStatus();
	}
}

function updatePidStatus() {
	const count = selectedPids().length;
	$("gauge-count").textContent = `${count} of ${chipPids().length}`;

	let text;
	if (scanning)
		text = "Checking which gauges this vehicle supports…";
	else if (scanFailed)
		text = "The vehicle didn't say which gauges it supports. Turn the ignition on and scan again.";
	else if (settings.supported)
		text = supportedFresh ? "Gauges this vehicle supports." : "Gauges supported by the last vehicle.";
	else
		text = "Common gauges. Connect to see the ones this vehicle supports.";
	$("pid-status").textContent = text;
	$("pid-scan").hidden = !scanFailed || scanning;
}

// --- trouble codes ---

async function readCodes(session) {
	const codes = [];
	for (const [service, kind] of [[0x03, "stored"], [0x07, "pending"]]) {
		let responses;
		try {
			responses = await session.obdRequest(Uint8Array.of(service));
		} catch (e) {
			// some vehicles don't answer for pending codes
			if (service !== 0x03 && e instanceof DeviceError && e.code === Err.TIMEOUT)
				continue;
			throw e;
		}
		for (const r of responses)
			if (!r.negative && r.message[0] === service + 0x40)
				for (const code of decodeDtcs(r.message))
					if (!codes.some(c => c.code === code))
						codes.push({ code, kind });
	}
	return codes;
}

function renderCodes(codes) {
	const body = $("codes-body");
	if (codes.length === 0) {
		body.replaceChildren(el("p", "all-clear", "No stored or pending trouble codes."));
		return;
	}
	const list = el("ul", "dtcs");
	for (const { code, kind } of codes) {
		const item = el("li");
		item.append(el("span", "dtc", code), el("span", "dtc-kind", kind === "stored" ? "Stored" : "Pending"));
		list.append(item);
	}
	body.replaceChildren(list);
}

async function checkCodes(session) {
	$("codes-body").replaceChildren(el("p", "meta", "Checking…"));
	try {
		const codes = await readCodes(session);
		if (dev === session)
			renderCodes(codes);
	} catch (e) {
		$("codes-body").replaceChildren(el("p", "meta", `Couldn't check for codes. ${errorText(e)}`));
	}
}

// --- adapter ---

function renderKv(dl, rows) {
	dl.replaceChildren();
	for (const [k, v] of rows)
		dl.append(el("dt", "", k), el("dd", "", v));
}

async function refreshInfo() {
	const session = dev;
	try {
		const info = await session.getInfo();
		if (dev !== session)
			return;
		const firmware = info.fwVersion.join(".");
		const rows = [
			["Connection", session.transport.kind === "USB" ? "USB" : "Bluetooth"],
			["Firmware", firmware],
			["Bluetooth address", info.bleMac],
		];
		if (info.blePasskey)
			rows.push(["Pairing passkey", String(info.blePasskey).padStart(6, "0")]);
		renderKv($("info"), rows);
		$("adapter-summary").textContent = `Firmware ${firmware}`;
		showBusState(info.canState);
	} catch (e) {
		console.warn("Device info:", errorText(e));
	}
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

function init() {
	$("unsupported").hidden = BleTransport.supported;
	$("connect-ble").addEventListener("click", () => connect(BleTransport));
	$("connect-usb").addEventListener("click", () => connect(SerialTransport));
	$("disconnect").addEventListener("click", disconnect);

	// vehicle
	$("veh-retry").addEventListener("click", () => action($("veh-retry"), retryVehicle));
	$("codes-check").addEventListener("click", () => action($("codes-check"), () => checkCodes(dev)));

	// live
	for (const ms of RATES_MS) {
		const b = el("button", "", `${ms / 1000} s`);
		b.dataset.ms = ms;
		b.addEventListener("click", () => setRate(ms));
		$("rates").append(b);
	}
	setRate(settings.interval);
	for (const b of $("units").children)
		b.addEventListener("click", () => setUnits(b.dataset.units === "imperial"));
	setUnits(settings.imperial);
	renderPidChips();
	$("live-toggle").addEventListener("click", toggleLive);
	$("pid-scan").addEventListener("click", () => action($("pid-scan"), () => scanPids(dev)));

	// recording
	$("rec-toggle").addEventListener("click", onRecordClick);
	$("rec-save").addEventListener("click", () => action($("rec-save"), saveRecording));
	confirmTap($("rec-discard"), async () => {
		Object.assign(recording, { rows: [], pids: [], saved: true });
	});
	window.addEventListener("beforeunload", e => {
		if (recording.active || hasUnsaved())
			e.preventDefault();
	});

	// adapter
	confirmTap($("reset"), async () => {
		const session = dev;
		await session.reset();
		toast("The adapter is restarting.");

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
		toast("Forgot all paired phones.");
	});

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
