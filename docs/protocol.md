# OBD12 host protocol (version 1)

The same protocol runs over the USB serial port (115200 8N1) and the BLE Nordic UART service. The source of truth is `firmware/OBD12/Core/Inc/Comms/protocol.h` and `software/obd12/protocol.py` mirrors it.

## Transport

**USB:** a byte stream on the FT230X serial port.

**BLE:** Nordic UART service `6e400001-b5a3-f393-e0a9-e50e24dcca9e`.

| Characteristic | UUID | Direction |
|---|---|---|
| RX | `6e400002-b5a3-f393-e0a9-e50e24dcca9e` | host → device: write or write-without-response, **at most 100 bytes per write** |
| TX | `6e400003-b5a3-f393-e0a9-e50e24dcca9e` | device → host: notifications of at most `min(ATT_MTU − 3, 120)` bytes |

Treat BLE as a byte stream: frames are split across writes and notifications and must be reassembled on the delimiter. The device sends nothing on BLE until TX notifications are enabled.

## Framing

Each frame is [COBS](https://en.wikipedia.org/wiki/Consistent_Overhead_Byte_Stuffing)-encoded and followed by a `0x00` delimiter. Decoded, a frame is:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | type |
| 1 | 1 | seq |
| 2 | 1 | id (command or event) |
| 3 | 0–240 | payload |
| end − 2 | 2 | CRC-16/CCITT-FALSE over type, seq, id, payload (little-endian) |

CRC-16/CCITT-FALSE: polynomial `0x1021`, initial value `0xFFFF`, no reflection, no final XOR. `crc16("123456789") = 0x29B1`.

Frames that fail COBS decoding or the CRC are dropped silently; the requester times out. A receiver resynchronises on the next `0x00`.

All multi-byte fields are **little-endian**.

## Frame types

| Value | Type | Direction | seq |
|---|---|---|---|
| `0x01` | REQ | host → device | chosen by the host, echoed in the answer |
| `0x02` | RSP | device → host | same as the request |
| `0x03` | ERR | device → host | same as the request; payload `[code u8]` |
| `0x04` | EVT | device → host | per-transport event counter (detects dropped events) |

Every REQ gets exactly one RSP or ERR with the same `seq` and `id`. Requests may be pipelined. Answers to OBD requests can arrive out of order relative to other requests, so match on `seq`.

## Commands

| ID | Name | Request payload | Response payload | Auth |
|---|---|---|---|---|
| `0x01` | PING | any bytes | the same bytes | |
| `0x02` | GET_INFO | – | `info` (36 bytes, below) | |
| `0x03` | GET_POWER | – | `power` (6 bytes, below) | |
| `0x04` | SET_EVENTS | `topics u8` | – | |
| `0x05` | GET_CAN_STATS | – | 6 × `u32`: rx_frames, rx_dropped, tx_frames, tx_failed, bus_off_count, transceiver_faults | |
| `0x06` | RESET | – | – (the device reboots after the answer) | yes |
| `0x07` | BLE_CLEAR_BONDS | – | – | yes |
| `0x10` | OBD_REQUEST | `dest u16`, 1–7 request bytes | OBD records | for services other than 01, 02, 03, 05, 06, 07, 09, 0A |
| `0x11` | GET_VIN | – | 17 ASCII characters | |
| `0x12` | LIVE_START | `interval_ms u16`, 1–32 mode 01 PIDs | – | |
| `0x13` | LIVE_STOP | – | – | |
| `0x14` | GET_SUPPORTED_PIDS | – | 32-byte bitmap of supported mode 01 PIDs (below) | |

### Authorization

USB is always authorized. A BLE link is authorized once it is encrypted. Pairing requires MITM protection, using the 6-digit passkey reported by `GET_INFO` over USB. A command that needs authorization on an unauthorized BLE link fails with `NOT_AUTHORIZED`, and the device asks the central to pair. Retry the command once pairing completes.

### OBD_REQUEST

`dest` is `0x7DF` (functional broadcast) or `0x7E0`–`0x7E7` (one ECU). The request bytes are the OBD service ID followed by its parameters, e.g. `01 0C` for engine RPM. They are sent as one ISO-TP single frame.

- **Broadcast:** the device collects every answer that arrives within 50 ms. The window stays open while a multi-frame answer is still arriving.
- **Physical (one ECU):** the request completes at the first answer from `dest + 8`.
- **"Response pending" (NRC `0x78`):** extends the wait to 5 s.

The response is a list of records, one per answering ECU:

| Size | Field |
|---|---|
| 2 | ECU response ID (`0x7E8`–`0x7EF`) |
| 1 | message length `n` |
| n | response message: `SID + 0x40` followed by data, or `7F <SID> <NRC>` for a negative response |

No answer at all gives `ERR TIMEOUT`. CAN being unusable (bus-off, TX failure) gives `ERR CAN`. A full request queue gives `ERR BUSY`.

### GET_VIN and GET_SUPPORTED_PIDS

At startup the device reads the VIN (mode 09 PID 02) and the supported mode 01 PIDs, and keeps both for as long as it is powered.

- **Supported PIDs:** the device asks for PID `0x00`, then `0x20`, `0x40`, … up to `0xE0`. It moves to the next range only while some ECU reports that range as supported (the last bit of the previous answer). Answers from all ECUs are combined.
- **Bitmap:** the 32 bytes are the A–D data bytes of the answers to `0x00`, `0x20`, … `0xE0`, one after another. PID `p` (`0x01`–`0xFF`) is supported if byte `(p − 1) / 8` has bit `0x80 >> ((p − 1) % 8)` set. The range PIDs (`0x20`, `0x40`, …) appear as supported when the next range exists; they carry no data.
- **If the startup read failed:** for example, the ignition was off. A `GET_VIN` or `GET_SUPPORTED_PIDS` request then reads again and answers when done, or with `ERR TIMEOUT` if no ECU answered. Allow several seconds.
- **Older firmware:** firmware before 0.3.0 answers `GET_SUPPORTED_PIDS` with `UNKNOWN_CMD`. The host clients then send `01 00`, `01 20`, … through `OBD_REQUEST` themselves.

### LIVE_START / LIVE_STOP

The device polls the listed mode 01 PIDs. One pass covers every listed PID.

- **First pass:** each PID is requested on its own on the broadcast address, which waits out the 50 ms response window. The answers show which ECU answers each PID and how many data bytes it has.
- **Later passes:** learned PIDs are requested up to six per request (SAE J1979), physically addressed to the ECU that answers them. That ECU answers within a few ms, so a pass of 32 learned PIDs takes a few tens of ms instead of about 1.6 s.
- **Fallback:** a batched PID that does not come back is requested on its own again in the same pass. After three such failures the PID stays on single broadcast requests. PIDs no ECU answers stay on single broadcast requests too, and each costs 50 ms per pass. Hosts should only ask for supported PIDs.
- **Event order:** the records in an `EVT_LIVE_DATA` are not necessarily in the order of the `LIVE_START` list. After each pass, at most once per `interval_ms` (minimum 50 ms), it sends an `EVT_LIVE_DATA`.

- One polling configuration is shared by all hosts; the latest `LIVE_START` wins.
- Polling stops when no host is subscribed. A host's subscription ends when it sends `LIVE_STOP`, disconnects (BLE), or sends no request for **30 s**. While streaming, send a `PING` every ~10 s to keep the subscription alive.

## Events

| ID | Name | Payload | Topic |
|---|---|---|---|
| `0x80` | LOG | UTF-8 text, one line, no line ending | LOG |
| `0x81` | POWER | `power`, every second | POWER |
| `0x82` | CAN_STATUS | `can_state u8`, on change | CAN_STATUS |
| `0x83` | LIVE_DATA | records `[pid u8][len u8][data]`; data excludes `41` and the PID | started by LIVE_START |

Topics for `SET_EVENTS` are bit flags: `0x01` LOG, `0x02` POWER, `0x04` CAN_STATUS. Defaults: USB = LOG | CAN_STATUS; BLE = CAN_STATUS, reset on every connection.

## Errors

| Code | Name | Meaning |
|---|---|---|
| `0x01` | BAD_REQUEST | malformed payload |
| `0x02` | UNKNOWN_CMD | unknown command ID |
| `0x03` | BUSY | request queue full; try again |
| `0x04` | TIMEOUT | no ECU answered |
| `0x05` | NOT_AUTHORIZED | needs an authorized link (see above) |
| `0x06` | CAN | CAN bus not usable |
| `0x07` | UNAVAILABLE | feature not available (e.g. no BLE module) |

## Structures

### info (GET_INFO, 36 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | protocol version (1) |
| 1 | u8 × 3 | firmware version major, minor, patch |
| 4 | u8 | HWCONF strap (bit 0 = HWCONF0) |
| 5 | u8 | reset cause: 0 unknown, 1 power-on, 2 reset pin, 3 software, 4 watchdog, 5 window watchdog, 6 low-power, 7 fatal error |
| 6 | u8 | fault before the last reset: 0 none, 1 Error_Handler, 2 HardFault, 3 MemManage, 4 BusFault, 5 UsageFault, 6 NMI |
| 7 | u8 | CAN state: 0 OK, 1 error warning, 2 error passive, 3 bus-off, 4 not started |
| 8 | u32 | fault detail (SCB->CFSR for CPU faults) |
| 12 | u32 × 3 | STM32 UID (zero on unauthorized links) |
| 24 | u8 × 6 | BLE address, most significant byte first |
| 30 | u8 | flags: `0x01` BLE available, `0x02` BLE connected, `0x04` this link is authorized, `0x08` VIN known, `0x10` supported PIDs known |
| 31 | u8 | reserved |
| 32 | u32 | BLE passkey (USB only, otherwise 0) |

### power (GET_POWER, EVT_POWER, BLE Power characteristic; 6 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | USB regulator status pin (`USB_REG_ST`) |
| 1 | u8 | CAN regulator status pin (`CAN_REG_ST`) |
| 2 | u16 | USB regulator output, mV |
| 4 | u16 | CAN regulator output, mV |

The voltages are measured against VDDA (the +3V3 rail), so on the current hardware they read at or near full scale. See README design note 3.
