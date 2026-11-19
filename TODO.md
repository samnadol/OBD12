# TODO

Work planned from the "OEM diagnostics and OTA firmware updates" report (Nov 2026; kept locally in `research/`, which is not in git), plus the BLE service cleanup. Efforts are rough focused-engineer estimates. Not planned: an ELM327 compatibility mode.

## 0. Validate the size-optimized build

- [ ] Flash the newlib-nano + LTO build (34.5 KB) and check BLE pairing/connect, mode 01 live data, USB link and EVT_LOG output (0.5–1 d)
- [ ] Build once in STM32CubeIDE to confirm the `.cproject` LTO / no-float-printf settings link

## 1. Vehicle coverage: CAN addressing (firmware)

- [x] Enable extended (29-bit) receive filters (`0x18DAF1xx`, `ExtFiltersNbr=1`)
- [x] 29-bit ISO-TP: normal-fixed addressing (`0x18DAxxF1` / `0x18DAF1xx`), functional `0x18DB33F1`, flow control to `0x18DAxxF1`; protocol v3 carries u32 CAN IDs
- [x] Accept GM Global B responses on `0x142AF1xx` (requests `0x14DAxxF1`, priority `0x14`): a `DIAG_REQUEST` with `rx_id` `0x142AF1xx`
- [x] Addressing detection at 500k: an unanswered `0x7DF` broadcast is resent on `0x18DB33F1` until a vehicle answers; kept while powered, reported in `GET_INFO`
- [ ] 250 kbit/s detection: when neither `0x7DF` nor `0x18DB33F1` gets an answer at 500k, retry both at 250k (ISO 15765-4 order). Detection costs ~100–200 ms per power-up, so no per-VIN cache (reading the VIN needs the bit rate and addressing first anyway)
- [ ] Listen-only bit-rate probe before transmitting on an unknown bus
- [ ] Check multi-responder bursts (e.g. Honda's ~15 modules on `18DBEFF1`) against the 3-element RX FIFO on a real car; the RX interrupt already drains the FIFO
- [x] Generic request descriptor: `DIAG_REQUEST` (protocol v4) replaces `OBD_REQUEST`: `{flags, tx_id, rx_id, rx_mask, tx_ext, rx_ext, pad, timeout_ms}`, matching OBDb's `hdr/rax/eax/tmo`; a spare FDCAN filter per ID type follows `rx_id`/`rx_mask`
- [x] Extended addressing (Toyota `0x750/0x758`): target byte on every frame including our flow control, filter on ID + target byte, 6-byte single frames, one target at a time
- [x] NRC 0x78 (response pending): extend the wait to ~5 s in firmware
- [x] Reassembly buffers ≥ 256 B (Honda block DIDs exceed 160 B); records carry a u16 length, protocol payload up to 512 B
- [x] Per-request timeouts (≥ 600 ms for Toyota `0x750` targets) and padding (0x00 default; 0x55/0xAA seen)
- [x] Read-only service allow-list: J1979 reads, `0x10` (01/03 only), `0x19`, `0x1A`, `0x21`, `0x22`, `0x3E`, GMLAN `$A9`/`$AA`; DTC clear (`0x04`, `0x14`) and J1979 `0x08` on authorized links only; everything else `ERR_NOT_PERMITTED`. `tx_id` limited to diagnostic IDs (11-bit `0x600–0x7FF`, GMLAN `0x101`/`0x240–0x25F`, 29-bit PF `0xDA`/`0xDB`, VW group `0x17FC00xx`, Volvo/Polestar `0x1DD0xxxx`; checked against the request IDs in OBDb)
- [ ] Apps: ask for explicit confirmation before sending a DTC clear
- [ ] Multi-frame requests (UDS `0x22` with several DIDs, GMLAN `$AA` with many DPIDs exceed 7 bytes)
- [ ] Functional requests to non-ISO 15765-4 IDs: flow control currently goes to the ISO 15765-4 request ID of each responder, which is wrong for e.g. GM `0x14DB…` broadcasts with multi-frame answers
- [ ] Optional: VW TP 2.0 transport (channel setup on `0x200`) for older VW group modules (roughly pre-2012); until then those cars get generic OBD only
- [ ] GM UUDT raw-frame sink for `$A9` DTC reports and `$AA` periodic data (single frames without ISO-TP PCI on `0x541–0x55F` / `0x5E8–0x5EF`); background `$3E` while streaming (2–3 d)
- [ ] Optional: OVMS-style firmware poll scheduler (app pushes `{tx, rx, eax, service, id, interval}`; ~2.5 KB RAM for 210 commands)

## 2. Vehicle coverage: web app

- [ ] UDS / KWP2000 / GMLAN response parsers
- [ ] DTC display as J2012 codes with the UDS failure-type byte (e.g. `P0301-1F`); descriptions later (no open database exists)
- [ ] Treat NRC 0x33, 0x34, 0x22 and 0x7F as "restricted by the manufacturer" and never retry
- [ ] J1979-2 (OBDonUDS) fallback: `22 F400` when `01 00` gets no answer; `22 F4xx` / `22 F8xx` mapping; 3-byte DTCs via `0x19`
- [ ] OBDb ingestion:
  - [ ] GitHub Action that builds a make/model/year index (avoids browser API rate limits): repo names (`Make-Model`), plus the generations in each repo's `.gen.yaml`
  - [ ] Match corgi's make/model to a repo: normalise case and punctuation, alias table for makes (`MERCEDES-BENZ` → `Mercedes-Benz`/`Mercedes`, `LAND ROVER` → `Land-Rover`/`Landrover`, `VOLKSWAGEN` → `Volkswagen`/`VW`, `Fiat`/`FIAT`, `Seat`/`SEAT`, `Smart`/`smart`, Vauxhall/Opel → `VauxhallOpel`, Nissan/Infiniti → `NissanInfiniti`), the VIN's fuel type to pick `-Hybrid` / `-Electric` / `-Lightning` / `-Prime` / `-Energi` repos, variant repos (`Mercedes-Benz-C200d`)
  - [ ] Show the best matches, the user confirms; remember the choice per VIN; no match → `SAEJ1979` only
  - [ ] Apply each command's year filters (`filter` / `dbgfilter`: `from`, `to`, `years`); skip `dbg` commands
  - [ ] Map OBDb commands to `DIAG_REQUEST` targets: `hdr` + `pri` + `tst` → `tx_id` (29-bit: `pri` (default `18`), `hdr`, `tst` (default `F1`)), `rax` → `rx_id` (3 hex digits: 11-bit ID; 2: `18DAF1` + `rax`; 6: `pri` + `rax`; 8: the full ID; none: the ISO 15765-4 response ID), `eax` → `tx_ext`, `tmo` → `timeout_ms` (check the units against OBDb's schema), `fcm1` → flow control to `tx_id` (already the default for physical requests)
  - [ ] Per-make answer address byte for extended addressing: BMW answers with `F1`, Toyota with the target byte; OBDb only gives `eax`
  - [ ] Fetch `SAEJ1979`, then the make file, then the model file from jsDelivr pinned to a commit SHA
  - [ ] Cache in IndexedDB
  - [ ] Show OBDb's CC BY-SA 4.0 attribution string (About screen)
  - [ ] Replace the hand-written mode-01 table in `pids.js` with OBDb's `SAEJ1979` set
- [ ] Per-make module discovery and DTC read/clear:
  - [ ] Toyota: `0x750` extended-address sweep; KWP `0x13` read / `0x04` clear on older ECUs; UDS DTC requests to be probed and verified on real cars
  - [ ] Honda: functional `02 3E 00` on `18DBEFF1`, collect `7E 00` replies; `19 02 08` read, `14 FF FF FF` clear; probe PGM-FI at `0x10`, `0x11` and `0x0E`
  - [ ] GM: probe `7E0` and `DA11`; Global A `$A9`/`$04`; Global B DTC service still unconfirmed
  - [ ] VW / Audi / Skoda / Seat / Cupra: modules on `0x7xx` answering at request + `0x6A` (`710`→`77A`, `713`→`77D`, `714`→`77E`), engine `7E0`→`7E8`; newer platforms (MQB-evo, MEB) also 29-bit `17FC00xx` → `17FE00xx`; UDS `19 02` read, `14 FF FF FF` clear; gateway (SFD) on 2020+ cars likely refuses clears
  - [ ] Ford / Lincoln / Mazda: modules at `7xx` → `+8` (PCM `7E0`, BCM `726`, IPC `720`, ABS `760`, gateway `716`); UDS `19 02` on 2009+, KWP `0x18` read on older Ford; many body modules are on MS-CAN (pins 3/11, section 6)
  - [ ] BMW / MINI: everything through `6F1` with the module address as `tx_ext`, answers on `0x600 + address` with `F1` as the address byte; sweep addresses; UDS `19 02`
  - [ ] Hyundai / Kia / Genesis: modules at `7xx` → `+8` (BMS `7E4`→`7EC`, `7A0`, `770`), KWP `0x21` on older ECUs; UDS `19 02`; some 2020+ models have a security gateway
  - [ ] Stellantis (Jeep / Ram / Dodge / Chrysler / Fiat): IDs from OBDb (`740`→`4C0`, `7AB`→`52B`; 29-bit `18DAxxF1` on newer Ram); 2018+ Secure Gateway allows reads but rejects clears (NRC `0x33`) without an AutoAuth login: explain it in the app instead of retrying
  - [ ] Nissan / Infiniti: IDs from OBDb (Leaf `79B`→`7BB`, `797`→`79A`, `743`→`763`); KWP `0x21` on older ECUs
  - [ ] Volvo / Polestar: 29-bit `1DD0xxxx` requests with answers on `1EC6xxxx`-style IDs from OBDb; DTC service to be confirmed
  - [ ] Mercedes, Subaru, Renault, Peugeot / Citroen: data from OBDb only (Subaru `7B0`, Renault `18DAxxF1`, Peugeot `6A8`→`688`); DTC services to be confirmed
  - [ ] Treat a make with no OBDb entry as generic OBD-II only (e.g. Tesla, whose OBDb set is empty)

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
