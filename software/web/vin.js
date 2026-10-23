/*
 * Vehicle lookup by VIN with corgi (github.com/cardog-ai/corgi), which decodes
 * against a SQLite copy of the NHTSA vPIC database in the browser.
 *
 * The database is 25 MB gzipped and 80 MB open, so it is only fetched for a
 * VIN that has not been looked up before, and closed again afterwards. The
 * caller keeps the results.
 */

const CORGI = "https://cdn.jsdelivr.net/npm/@cardog/corgi@2.0.4/dist/";
const SQLJS = "https://cdn.jsdelivr.net/npm/sql.js@1.10.3/dist/";

const VIN_PATTERN = /^[A-HJ-NPR-Z0-9]{17}$/;

export const isVin = vin => VIN_PATTERN.test(vin);

let sqlJs = null;

// sql.js is a classic script that defines initSqlJs()
function loadSqlJs() {
	sqlJs ??= new Promise((resolve, reject) => {
		const script = document.createElement("script");
		script.src = SQLJS + "sql-wasm.js";
		script.onload = resolve;
		script.onerror = () => reject(new Error("Couldn't load sql.js"));
		document.head.append(script);
	}).then(() => window.initSqlJs({ locateFile: file => SQLJS + file }))
		.catch(e => {
			sqlJs = null;
			throw e;
		});
	return sqlJs;
}

// the database, decompressed; onProgress(fraction) while downloading
async function fetchDatabase(onProgress) {
	const response = await fetch(CORGI + "db/vpic.lite.db.gz");
	if (!response.ok)
		throw new Error(`Vehicle database: HTTP ${response.status}`);

	const total = Number(response.headers.get("content-length")) || 0;
	let loaded = 0;
	const counted = response.body.pipeThrough(new TransformStream({
		transform(chunk, controller) {
			loaded += chunk.length;
			if (total)
				onProgress?.(Math.min(1, loaded / total));
			controller.enqueue(chunk);
		},
	}));
	return new Uint8Array(await new Response(counted.pipeThrough(new DecompressionStream("gzip"))).arrayBuffer());
}

function engineText(engine, fuel) {
	if (/electric/i.test(fuel) && !engine.displacement)
		return "Electric";
	const parts = [];
	if (engine.displacement)
		parts.push(`${Number(engine.displacement).toFixed(1)} L`);
	if (engine.cylinders)
		parts.push(`${engine.cylinders}-cylinder`);
	return parts.join(" ");
}

// { year, make, model, trim, body, engine, drive }, or null if the database doesn't know the VIN
function summarize(result) {
	const v = result.components?.vehicle;
	if (!v?.make)
		return null;
	const fuel = v.fuelType || result.components.engine?.fuel || "";
	return {
		year: v.year || null,
		make: v.make,
		model: v.model || "",
		trim: v.trim || v.series || "",
		body: v.bodyStyle || "",
		engine: engineText(result.components.engine ?? {}, fuel),
		drive: (v.driveType || "").split("/")[0],     // "4WD/4-Wheel Drive/4x4" -> "4WD"
	};
}

const pending = new Map();      // vin -> Promise, so a reconnect doesn't fetch twice

export function lookupVehicle(vin, onProgress) {
	if (!pending.has(vin)) {
		const lookup = (async () => {
			const [corgi, SQL, bytes] = await Promise.all([
				import(CORGI + "browser.mjs"),
				loadSqlJs(),
				fetchDatabase(onProgress),
			]);
			const db = new corgi.BrowserDatabaseAdapter(new SQL.Database(bytes));
			try {
				return summarize(await corgi.decodeVIN(vin, db, {}));
			} finally {
				await db.close();
			}
		})().finally(() => pending.delete(vin));
		pending.set(vin, lookup);
	}
	return pending.get(vin);
}
