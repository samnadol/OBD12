/*
 * protocol.h
 *
 * OBD12 host protocol, shared by the USB serial port and the BLE Nordic UART
 * service. This file is the source of truth; software/obd12/protocol.py and
 * docs/protocol.md mirror it.
 *
 * On the wire, each frame is COBS-encoded and terminated by a 0x00 byte.
 * Decoded, a frame is:
 *
 *   [type u8][seq u8][id u8][payload 0..PROTO_MAX_PAYLOAD][crc16 u16 LE]
 *
 * crc16 (CRC-16/CCITT-FALSE) covers type, seq, id and payload. All multi-byte
 * fields are little-endian.
 *
 *  - REQ: host -> device. seq is chosen by the host.
 *  - RSP / ERR: device -> host, answering the REQ with the same seq and id.
 *  - EVT: device -> host, unsolicited. seq counts events per transport so a
 *         host can detect drops.
 */

#ifndef INC_COMMS_PROTOCOL_H_
#define INC_COMMS_PROTOCOL_H_

#include <stdint.h>

#define PROTO_VERSION 1

#define PROTO_HEADER_LEN   3
#define PROTO_CRC_LEN      2
#define PROTO_MAX_PAYLOAD  240
#define PROTO_MAX_FRAME    (PROTO_HEADER_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN)
#define PROTO_MAX_ENCODED  (PROTO_MAX_FRAME + PROTO_MAX_FRAME / 254 + 1)   // without the 0x00 delimiter

typedef enum
{
	PT_REQ = 0x01,
	PT_RSP = 0x02,
	PT_ERR = 0x03,
	PT_EVT = 0x04,
} proto_type_t;

typedef enum
{
	// general
	CMD_PING = 0x01,            // req: any bytes          rsp: same bytes
	CMD_GET_INFO = 0x02,        // req: -                  rsp: proto_info_t
	CMD_GET_POWER = 0x03,       // req: -                  rsp: proto_power_t
	CMD_SET_EVENTS = 0x04,      // req: u8 topic mask      rsp: -
	CMD_GET_CAN_STATS = 0x05,   // req: -                  rsp: proto_can_stats_t
	CMD_RESET = 0x06,           // req: -                  rsp: - (then reboots)        [auth]
	CMD_BLE_CLEAR_BONDS = 0x07, // req: -                  rsp: -                       [auth]

	// vehicle
	CMD_OBD_REQUEST = 0x10,     // req: u16 dest, 1-7 request bytes   rsp: OBD records (see below)
	                            //      [auth] unless the service is read-only (01,02,03,05,06,07,09,0A)
	CMD_GET_VIN = 0x11,         // req: -                  rsp: 17 ASCII characters
	CMD_LIVE_START = 0x12,      // req: u16 interval_ms, 1-32 mode 01 PIDs    rsp: -
	CMD_LIVE_STOP = 0x13,       // req: -                  rsp: -
	CMD_GET_SUPPORTED_PIDS = 0x14, // req: -               rsp: 32-byte bitmap of supported mode 01 PIDs:
	                            //      PID p (0x01-0xFF) is supported if byte (p-1)/8 has bit 0x80 >> ((p-1)%8)
} proto_cmd_t;

typedef enum
{
	EVT_LOG = 0x80,             // UTF-8 text, one line, no newline
	EVT_POWER = 0x81,           // proto_power_t, every second
	EVT_CAN_STATUS = 0x82,      // u8 can state, on change
	EVT_LIVE_DATA = 0x83,       // live records: [pid u8][len u8][data] ..., once per polling cycle
} proto_evt_t;

// CMD_SET_EVENTS topic mask (live data is controlled by CMD_LIVE_START/STOP)
#define TOPIC_LOG        0x01
#define TOPIC_POWER      0x02
#define TOPIC_CAN_STATUS 0x04
#define TOPIC_LIVE       0x80   // internal, set by CMD_LIVE_START

typedef enum
{
	ERR_BAD_REQUEST = 0x01,     // malformed payload
	ERR_UNKNOWN_CMD = 0x02,
	ERR_BUSY = 0x03,            // request queue full, try again
	ERR_TIMEOUT = 0x04,         // no ECU answered
	ERR_NOT_AUTHORIZED = 0x05,  // needs an authenticated link (USB, or bonded BLE); BLE pairing has been requested
	ERR_CAN = 0x06,             // CAN bus not available (bus-off, TX failure)
	ERR_UNAVAILABLE = 0x07,     // feature not available (e.g. BLE module missing)
} proto_err_t;
// ERR payload: [code u8]

/*
 * OBD records (CMD_OBD_REQUEST response): one record per answering ECU,
 *   [ecu_id u16 LE][len u8][response message, len bytes]
 * The message starts with the response service ID (request SID + 0x40), or
 * 0x7F <SID> <NRC> for a negative response.
 */

typedef struct __attribute__((packed))
{
	uint8_t proto_version;
	uint8_t fw_major;
	uint8_t fw_minor;
	uint8_t fw_patch;
	uint8_t hwconf;
	uint8_t reset_cause;        // reset_cause_t
	uint8_t last_fault;         // fault_code_t before the last reset
	uint8_t can_state;          // can_state_t
	uint32_t last_fault_info;
	uint32_t uid[3];
	uint8_t ble_mac[6];         // MSB first, as shown by phones
	uint8_t flags;              // INFO_FLAG_*
	uint8_t reserved;
	uint32_t ble_passkey;       // only filled in over USB, 0 otherwise
} proto_info_t;

#define INFO_FLAG_BLE_AVAILABLE   0x01
#define INFO_FLAG_BLE_CONNECTED   0x02
#define INFO_FLAG_AUTHORIZED      0x04   // the transport this answer is sent on
#define INFO_FLAG_VIN_KNOWN       0x08
#define INFO_FLAG_PIDS_KNOWN      0x10   // supported mode 01 PIDs discovered

typedef struct __attribute__((packed))
{
	uint8_t usb_regulator_ok;   // USB_REG_ST pin
	uint8_t can_regulator_ok;   // CAN_REG_ST pin
	uint16_t usb_regulator_mv;
	uint16_t can_regulator_mv;
} proto_power_t;

typedef struct __attribute__((packed))
{
	uint32_t rx_frames;
	uint32_t rx_dropped;
	uint32_t tx_frames;
	uint32_t tx_failed;
	uint32_t bus_off_count;
	uint32_t transceiver_faults;
} proto_can_stats_t;

#endif /* INC_COMMS_PROTOCOL_H_ */
