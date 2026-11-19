# OBD12 host protocol (version 4)

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
| 3 | 0–512 | payload |
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
| `0x03` | GET_POWER | – | `power` (4 bytes, below) | |
| `0x04` | SET_EVENTS | `topics u8` | – | |
| `0x05` | GET_CAN_STATS | – | 6 × `u32`: rx_frames, rx_dropped, tx_frames, tx_failed, bus_off_count, transceiver_faults | |
| `0x06` | RESET | – | – (the device reboots after the answer) | yes |
| `0x07` | BLE_CLEAR_BONDS | – | – | yes |
| `0x10` | DIAG_REQUEST | target (18 bytes), 1–7 request bytes | diagnostic records | clearing DTCs and J1979 tests (below) |
| `0x11` | GET_VIN | – | 17 ASCII characters | |
| `0x12` | LIVE_START | `interval_ms u16`, 1–32 mode 01 PIDs | – | |
| `0x13` | LIVE_STOP | – | – | |
| `0x14` | GET_SUPPORTED_PIDS | – | 32-byte bitmap of supported mode 01 PIDs (below) | |

### Authorization

USB is always authorized. A BLE link is authorized once it is encrypted. Pairing requires MITM protection, using the 6-digit passkey reported by `GET_INFO` over USB. A command that needs authorization on an unauthorized BLE link fails with `NOT_AUTHORIZED`, and the device asks the central to pair. Retry the command once pairing completes.

### DIAG_REQUEST

Sends one diagnostic request (SAE J1979 OBD-II, UDS, KWP2000 or GMLAN over ISO 15765) and returns the answers. The request is described by a target, followed by the request bytes: the service ID and its parameters, e.g. `01 0C` for engine RPM or `22 F1 90` for the UDS VIN. The request is sent as one ISO-TP single frame, so it is 1–7 bytes, or 1–6 with extended addressing. Answers can be single- or multi-frame, up to 256 bytes each.

CAN IDs are u32. Bit 31 marks a 29-bit ID, as in Linux SocketCAN: `0x000007E8` is 11-bit `7E8`, `0x98DAF110` is 29-bit `18DAF110`.

**Target** (18 bytes):

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | flags: `0x01` AUTO, `0x02` FUNCTIONAL, `0x04` EXT_ADDR |
| 1 | u32 | `tx_id`: CAN ID the request is sent on |
| 5 | u32 | `rx_id`: answers come from CAN IDs that match `rx_id` … |
| 9 | u32 | `rx_mask`: … on these bits. 0 = all bits (one ECU) |
| 13 | u8 | `tx_ext`: with EXT_ADDR, the address byte sent before the PCI |
| 14 | u8 | `rx_ext`: with EXT_ADDR, the address byte answers carry; frames with another byte are ignored |
| 15 | u8 | `pad`: padding byte for unused bytes of the frames the device sends (request and flow control) |
| 16 | u16 | `timeout_ms`: how long to wait, at most 5000. 0 = 50 ms |

- **AUTO:** the vehicle's OBD broadcast, `7DF` on 11-bit vehicles and `18DB33F1` on 29-bit ones, collecting the answers from `7E8`–`7EF` or `18DAF1xx`. `tx_id`, `rx_id` and `rx_mask` must be 0 and EXT_ADDR must be clear. This is what generic OBD-II requests use.
- **FUNCTIONAL:** several ECUs may answer. The device collects every answer that arrives within the timeout, and keeps waiting while a multi-frame answer is still arriving. Flow control goes to each ECU's ISO 15765-4 request ID (`7E8` → `7E0`, `18DAF110` → `18DA10F1`).
- **Physical (neither flag):** the request completes at the first answer. Flow control goes to `tx_id`.
- **EXT_ADDR:** ISO 15765-2 extended addressing, as Toyota uses on `750`/`758`: every frame carries an address byte before the PCI, so single frames carry 6 bytes.
- **"Response pending" (NRC `0x78`):** extends the wait to 5 s.
- **Addressing detection:** until a vehicle has answered, an AUTO request that gets no answer within its timeout is sent again on `18DB33F1` (the ISO 15765-4 initialisation sequence). The first answer to an AUTO request decides 11- or 29-bit, which `GET_INFO` reports, and the device keeps it for as long as it is powered.

Examples:

| Request | flags | `tx_id` | `rx_id` | `rx_mask` | ext | timeout |
|---|---|---|---|---|---|---|
| OBD-II, any vehicle | AUTO | 0 | 0 | 0 | | 0 |
| Engine ECU, 11-bit | 0 | `0x7E0` | `0x7E8` | 0 | | 0 |
| Engine ECU, 29-bit | 0 | `0x98DA10F1` | `0x98DAF110` | 0 | | 0 |
| Toyota body ECU | EXT_ADDR | `0x750` | `0x758` | 0 | `40` / `40` | 600 |
| GM Global B module `40` | 0 | `0x94DA40F1` | `0x942AF140` | 0 | | 0 |
| Every Honda module | FUNCTIONAL | `0x98DBEFF1` | `0x98DAF100` | `0x1FFFFF00` | | 100 |

The response is a list of records, one per answering ECU:

| Size | Field |
|---|---|
| 4 | ECU ID: the CAN ID it answered on |
| 2 | message length `n` |
| n | response message: `SID + 0x40` followed by data, or `7F <SID> <NRC>` for a negative response |

Records that would not fit the 512-byte payload are dropped. No answer at all gives `ERR TIMEOUT`. CAN being unusable (bus-off, TX failure) gives `ERR CAN`. A full request queue gives `ERR BUSY`. A malformed target gives `ERR BAD_REQUEST`.

**What the device sends.** It never sends a request that could change how the vehicle behaves. Requests outside these rules fail with `ERR NOT_PERMITTED`, on every link:

| Services | Allowed |
|---|---|
| J1979 `01`, `02`, `03`, `05`, `06`, `07`, `09`, `0A`; `19` ReadDTCInformation; `1A`, `21`, `22` read data; `3E` TesterPresent; GMLAN `A9`, `AA` | always |
| `10` DiagnosticSessionControl | default (`01`) and extended (`03`) sessions only |
| J1979 `04` and UDS/KWP `14` clear DTCs, J1979 `08` on-board tests | on authorized links only |
| everything else: writes, security access, routines, I/O control, ECU reset, communication control, downloads, programming sessions | never |

`tx_id` must be a diagnostic ID: 11-bit `0x600`–`0x7FF`, GMLAN `0x101` and `0x240`–`0x25F`, or 29-bit `xxDAxxxx` / `xxDBxxxx` (ISO 15765-2 normal fixed addressing). The device never sends on other IDs, where modules would read the frame as normal bus traffic.

### GET_VIN and GET_SUPPORTED_PIDS

At startup the device reads the VIN (mode 09 PID 02) and the supported mode 01 PIDs, and keeps both for as long as it is powered.

- **Supported PIDs:** the device asks for PID `0x00`, then `0x20`, `0x40`, … up to `0xE0`. It moves to the next range only while some ECU reports that range as supported (the last bit of the previous answer). Answers from all ECUs are combined.
- **Bitmap:** the 32 bytes are the A–D data bytes of the answers to `0x00`, `0x20`, … `0xE0`, one after another. PID `p` (`0x01`–`0xFF`) is supported if byte `(p − 1) / 8` has bit `0x80 >> ((p − 1) % 8)` set. The range PIDs (`0x20`, `0x40`, …) appear as supported when the next range exists; they carry no data.
- **If the startup read failed:** for example, the ignition was off. A `GET_VIN` or `GET_SUPPORTED_PIDS` request then reads again and answers when done, or with `ERR TIMEOUT` if no ECU answered. Allow several seconds.

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
| `0x08` | NOT_PERMITTED | `DIAG_REQUEST` with a service or CAN ID the device never sends (see above) |

## Structures

### info (GET_INFO, 36 bytes)

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | protocol version (4) |
| 1 | u8 × 3 | firmware version major, minor, patch |
| 4 | u8 | HWCONF strap (bit 0 = HWCONF0) |
| 5 | u8 | reset cause: 0 unknown, 1 power-on, 2 reset pin, 3 software, 4 watchdog, 5 window watchdog, 6 low-power, 7 fatal error |
| 6 | u8 | fault before the last reset: 0 none, 1 Error_Handler, 2 HardFault, 3 MemManage, 4 BusFault, 5 UsageFault, 6 NMI |
| 7 | u8 | CAN state: 0 OK, 1 error warning, 2 error passive, 3 bus-off, 4 not started |
| 8 | u32 | fault detail (SCB->CFSR for CPU faults) |
| 12 | u32 × 3 | STM32 UID (zero on unauthorized links) |
| 24 | u8 × 6 | BLE address, most significant byte first |
| 30 | u8 | flags: `0x01` BLE available, `0x02` BLE connected, `0x04` this link is authorized, `0x08` VIN known, `0x10` supported PIDs known |
| 31 | u8 | OBD addressing: 0 not known yet, 1 11-bit, 2 29-bit |
| 32 | u32 | BLE passkey (USB only, otherwise 0) |

### power (GET_POWER, EVT_POWER, BLE Power characteristic; 4 bytes)

Each field is a pin level, 1 = high.

| Offset | Type | Field |
|---|---|---|
| 0 | u8 | `USB_REG_ST`: LM66100 status pin of the USB supply path |
| 1 | u8 | `CAN_REG_ST`: LM66100 status pin of the vehicle supply path |
| 2 | u8 | `USB_REG_OUT`: the USB regulator output is up |
| 3 | u8 | `CAN_REG_OUT`: the vehicle regulator output is up |

Version 1 sent 6 bytes with the regulator outputs in mV. Those readings were taken against VDDA, the +3V3 rail the regulators feed, so they could only show up or down.
