# TODO

Work planned from the "OEM diagnostics and OTA firmware updates" report (Nov 2026; kept locally in `research/`, which is not in git), plus the BLE service cleanup. Efforts are rough focused-engineer estimates. Not planned: an ELM327 compatibility mode.

## 0. Validate the size-optimized build

- [ ] Flash the newlib-nano + LTO build (34.5 KB) and check BLE pairing/connect, mode 01 live data, USB link and EVT_LOG output (0.5–1 d)
- [ ] Build once in STM32CubeIDE to confirm the `.cproject` LTO / no-float-printf settings link

## 1. Vehicle coverage: CAN addressing (firmware)

- [ ] Enable extended (29-bit) receive filters; the FDCAN config has `ExtFiltersNbr=0`
- [ ] 29-bit ISO-TP: normal-fixed addressing (`0x18DAxxF1` / `0x18DAF1xx`), functional `0x18DB33F1`, flow control to `0x18DAxxF1`
- [ ] Accept GM Global B responses on `0x142AF1xx` (requests `0x14DAxxF1`, priority `0x14`)
- [ ] ISO 15765-4 detection at connect: 500k 11-bit (`0x7DF`), then 500k 29-bit (`0x18DB33F1`), then 250k; record every responder; cache the result per VIN
- [ ] Listen-only bit-rate probe before transmitting on an unknown bus
- [ ] Drain FDCAN FIFOs in the ISR so multi-responder replies (e.g. Honda's ~15 modules on `18DBEFF1`) aren't dropped
- [ ] Generic request descriptor: `{29-bit flag, tx_id, rx_id or mask, ext_addr_tx, ext_addr_rx, fc_mode, timeout_ms, pad_byte}`, matching OBDb's `hdr/rax/eax/tst/pri/tmo/fcm1`; never assume rx = tx + 8
- [ ] Extended addressing (Toyota `0x750/0x758`): target byte on every frame including our flow control, filter on ID + target byte, 6-byte single frames, one target at a time
- [ ] NRC 0x78 (response pending): extend the wait to ~5 s in firmware
- [ ] Reassembly buffers ≥ 256 B (Honda block DIDs exceed 160 B)
- [ ] Per-request timeouts (≥ 600 ms for Toyota `0x750` targets) and padding (0x00 default; 0x55/0xAA seen)
- [ ] Read-only service allow-list: allow J1979 0x01–0x0A, `0x10` (01/03 only), `0x19`, `0x1A`, `0x21`, `0x22`, `0x3E`, GMLAN `$A9`/`$AA`; DTC clear (`0x14`, `0x04`) only behind explicit user confirmation; block `0x27`, `0x29`, `0x2E`, `0x2F`, `0x31`, `0x34–0x37`, `0x3D`, `0x11`, `0x28`
- [ ] GM UUDT raw-frame sink for `$A9` DTC reports and `$AA` periodic data (single frames without ISO-TP PCI on `0x541–0x55F` / `0x5E8–0x5EF`); background `$3E` while streaming (2–3 d)
- [ ] Optional: OVMS-style firmware poll scheduler (app pushes `{tx, rx, eax, service, id, interval}`; ~2.5 KB RAM for 210 commands)

## 2. Vehicle coverage: web app

- [ ] UDS / KWP2000 / GMLAN response parsers
- [ ] DTC display as J2012 codes with the UDS failure-type byte (e.g. `P0301-1F`); descriptions later (no open database exists)
- [ ] Treat NRC 0x33, 0x34, 0x22 and 0x7F as "restricted by the manufacturer" and never retry
- [ ] J1979-2 (OBDonUDS) fallback: `22 F400` when `01 00` gets no answer; `22 F4xx` / `22 F8xx` mapping; 3-byte DTCs via `0x19`
- [ ] OBDb ingestion:
  - [ ] GitHub Action that builds a make/model/year index (avoids browser API rate limits)
  - [ ] VIN to make/year, user confirms the model
  - [ ] Fetch `SAEJ1979`, then the make file, then the model file from jsDelivr pinned to a commit SHA
  - [ ] Cache in IndexedDB
  - [ ] Show OBDb's CC BY-SA 4.0 attribution string (About screen)
  - [ ] Replace the hand-written mode-01 table in `pids.js` with OBDb's `SAEJ1979` set
- [ ] Per-make module discovery and DTC read/clear:
  - [ ] Toyota: `0x750` extended-address sweep; KWP `0x13` read / `0x04` clear on older ECUs; UDS DTC requests to be probed and verified on real cars
  - [ ] Honda: functional `02 3E 00` on `18DBEFF1`, collect `7E 00` replies; `19 02 08` read, `14 FF FF FF` clear; probe PGM-FI at `0x10`, `0x11` and `0x0E`
  - [ ] GM: probe `7E0` and `DA11`; Global A `$A9`/`$04`; Global B DTC service still unconfirmed

## 3. BLE GATT cleanup

- [ ] Move the data pipe from the Nordic UART UUIDs to an OBD12 128-bit UUID (keep write/write-without-response in, notify out); update `obd12.js`, `software/obd12/transports/ble.py`, `docs/protocol.md` and the firmware together
- [ ] Advertise the service UUID (in the advertisement or scan response) and have the web app filter `requestDevice` by service instead of name
- [ ] Remove the Status/Power service (duplicates `GET_POWER` / `EVT_POWER`)
- [ ] Add Firmware Revision (0x2A26) and Hardware Revision (0x2A27) to Device Information
- [ ] Connection tuning from the peripheral side: connection-parameter update request, data length extension; record the negotiated ATT MTU (check `att_mtu` in `transport_ble.c` updates on the MTU event); write-without-response with an ack window for bulk transfers (2–4 d)
- [ ] Check that the BlueNRG stack sends Service Changed after the table changes; otherwise note that bonded phones must re-pair once
- [ ] Freeze the GATT table after this change (the bootloader must expose the same one)

## 4. Firmware updates

Decide once sections 1 and 3 are done and the firmware is a stable generic passthrough. Build units only for yourself: keep using SWD/USB and skip this section, except the hardware BOOT0 pad. Units go to other people: build at least the bootloader with USB recovery; BLE updates can come later.

- [ ] Decide: no updates / USB-only bootloader / full BLE + USB updates
- [ ] Signed image format: 512 B header (version, minimum protocol/bootloader version, hardware-revision mask, SHA-256, Ed25519 or MCUboot TLVs) (2–3 d)
- [ ] CI: tag triggers `pio run`, signs in a protected environment (required reviewers) or offline, publishes the image and `manifest.json` on the Pages origin with versioned filenames
- [ ] Custom bootloader (16 KB at `0x0800_0000`, WRP-protected) (8–15 d):
  - [ ] USART DFU over the existing COBS framing (recovery path)
  - [ ] SHA-256 + signature verify; measure crypto code size against the 16 KB budget
  - [ ] Staging-to-primary copy with a per-page progress record
  - [ ] IWDG + boot counter; stay in USB update mode after K unconfirmed boots
  - [ ] Pulse `BLE_NRST` and wait for the radio's init event
  - [ ] Anti-rollback security counter in the metadata page
- [ ] Flash layout: bootloader 16 KB, metadata 2 KB (`0x0800_4000`), settings/spare 2 KB, primary slot 54 KB (`0x0800_5000`, vectors at `0x0800_5200`), staging slot 54 KB (`0x0801_2800`)
- [ ] App changes (5–8 d):
  - [ ] Relink at `0x0800_5200`, set VTOR; PlatformIO linker script and upload offset
  - [ ] DFU command group over BLE and USB: `DFU_BEGIN` (returns resume offset), `DFU_DATA` (offset-addressed, ack + CRC every 2 KB), `DFU_STATUS`, `DFU_FINISH`, `DFU_ACTIVATE`, `DFU_CONFIRM`
  - [ ] Require a bonded link for DFU over BLE
  - [ ] Verify the signature in the app as well as the bootloader
  - [ ] Suspend CAN/OBD and FDCAN interrupts before erasing; nothing on the board measures the 12 V supply, so guard the update with J1979 PID 0x42 (control module voltage), or the 12 V divider from section 6
  - [ ] Self-test, then confirm (BlueNRG, CAN controller and a host link up)
- [ ] Web app update UI for both transports: windowed acks, resume, wake lock, reconnect after reset, compatibility check against `GET_INFO` (4–6 d)
- [ ] Web app AN3155 rescue/migration flasher: reopen at 8E1, Extended Erase, 256 B writes, read-back verify; installs bootloader + app on existing boards (3–5 d)
- [ ] Report the BlueNRG DTM firmware version in `GET_INFO`
- [ ] Later: swap-using-move rollback (primary 28 pages, staging 27); BLE recovery mode in the bootloader; BlueNRG DTM update through a USB-to-SPI pass-through

## 5. App rewrite (web + iOS)

TypeScript + Svelte 5 + Vite, packaged for iOS with Capacitor, so web (GitHub Pages), iOS and later Android share one codebase.

- [ ] Framework-free TypeScript core package: framing (COBS, CRC16), protocol, Device, PID/UDS/KWP/GMLAN decoding, OBDb loading; unit tests (Vitest) against `docs/protocol.md` vectors and OBDb captured responses
- [ ] Transport interface (`open`, `write`, `onData`, `onLost`) with two implementations: BLE on `@capacitor-community/bluetooth-le` (native on iOS/Android, Web Bluetooth in browsers) and Web Serial for USB (browsers only)
- [ ] Svelte UI on top of the core: vehicle, live gauges, recording, trouble codes, adapter, firmware update (if section 4 goes ahead)
- [ ] Keep the GitHub Pages deploy (`.github/workflows/pages.yml`) building the web target
- [ ] Retire `software/web` once the rewrite reaches feature parity
- [ ] Filter the device picker by the OBD12 service UUID (section 3); iOS only returns devices matching a service filter
- [ ] Reconnect to the last adapter without the picker (`getDevice` / `retrievePeripherals`)
- [ ] `NSBluetoothAlwaysUsageDescription`, and `bluetooth-central` in `UIBackgroundModes` so recording keeps running with the screen off
- [ ] Save CSV recordings through the share sheet / Files
- [ ] Check corgi's sql.js + 25 MB vehicle database in WKWebView (memory, download caching); consider bundling the database in the app
- [ ] Hide the USB option on iOS (no Web Serial)
- [ ] App icon, launch screen, privacy manifest; TestFlight; App Store review notes describing the hardware
- [ ] Apple Developer Program membership (needed for TestFlight and the App Store)

## 6. Hardware (next board revision)

- [ ] Bring PB8/BOOT0 to a button or pad (keep the pull-down)
- [ ] Optionally wire FT230X RTS/CBUS to BOOT0 and DTR to NRST
- [ ] Divider from the protected +12 V to a spare ADC pin: battery voltage, cranking dips, and a supply check before firmware updates
- [ ] SWD and BlueNRG DIO7/DIO8/DIO11 to test pads
- [ ] Check the CAN transceiver idles safely while PA11/PA12 float (ROM bootloader)
- [ ] Analog switch routing FDCAN to pins 6/14, 3/11 or 12/13 (GM chassis/object buses, Ford MS-CAN) (1–2 wk incl. bring-up)
- [ ] Optional single-wire CAN transceiver on pin 1 (33.3k; NCV7356 / MC33897) for GM low-speed body modules (1–2 wk)
- [ ] Later: consider an MCU with several FDCANs if simultaneous multi-bus sniffing becomes a goal

## Open questions to verify

- [ ] STM32G431 flash timings (page erase, 64-bit program) against DS12589 / RM0440 before setting DFU timeouts
- [ ] RM0440's 1 KB OTP area for a key hash or write-once counter
- [ ] Ed25519 vs P-256 verifier code size and verify time on the M4
- [ ] BlueNRG-M2 DTM maximum ATT MTU (158 vs 247); read it at runtime
- [ ] Measured BLE update throughput on Chrome desktop, Chrome Android and Bluefy
- [ ] Whether DTC clear is allowed on 2022+ gateway cars (Honda, GM, Toyota ABS/SRS/body)
- [ ] Toyota DTC request bytes for ABS, SRS, body and UDS-era ECUs
- [ ] GM Global B DTC service (`$19`/`$14` vs `$A9`/`$04`)
- [ ] Massachusetts data-access appeal (First Circuit No. 25-1262) outcome
