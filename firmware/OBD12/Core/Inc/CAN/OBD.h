/*
 * OBD.h
 *
 * Diagnostic request engine (SAE J1979 OBD-II, and manufacturer services such
 * as UDS / KWP2000 over ISO 15765). Requests are queued and sent one at a
 * time. Each request says where it goes and where answers come from (an
 * obd_request_t); the answers are handed back as a list of records:
 *
 *   record = [ecu_id u32 LE][len u16 LE][response message, len bytes]
 *
 * where ecu_id is the CAN ID the ECU answered on (CAN_ID_EXT set for 29-bit
 * IDs) and the response message starts with the response service ID
 * (request SID + 0x40) or 0x7F for a negative response.
 *
 * OBD_FLAG_AUTO stands for the vehicle's OBD functional address: 0x7DF on
 * 11-bit vehicles, 0x18DB33F1 on 29-bit ones. Until a vehicle has answered,
 * an AUTO request nobody answers on 0x7DF is sent again on 0x18DB33F1 (ISO
 * 15765-4 initialisation); the first answer decides the addressing, which is
 * then kept while powered.
 */

#ifndef OBD_H_
#define OBD_H_

#include <stdbool.h>
#include <stdint.h>

#include "Comms/protocol.h"

#define OBD_RECORD_HEADER 6     // ecu_id u32, len u16

#define OBD_RESPONSE_WINDOW_MS   50     // P2_CAN, ISO 15765-4: default timeout
#define OBD_RESPONSE_PENDING_MS  5000   // P2*_CAN, after NRC 0x78; also the longest timeout
#define OBD_RESULT_MAX           PROTO_MAX_PAYLOAD   // bytes of records per request
#define OBD_LIVE_RESULT_MAX      236
#define OBD_LIVE_MAX_PIDS        32

// supported mode 01 PIDs, as the concatenated answers to PIDs 0x00, 0x20, ... 0xE0:
// PID p (0x01-0xFF) is supported if bitmap[(p - 1) / 8] & (0x80 >> ((p - 1) % 8))
#define OBD_PID_BITMAP_LEN       32

#define OBD_NEGATIVE_RESPONSE 0x7F
#define OBD_NRC_RESPONSE_PENDING 0x78

typedef enum {
	OBD_SERVICE_DATA_LIVE = 0x01,
	OBD_SERVICE_DATA_FREEZE = 0x02,
	OBD_SERVICE_DTC_STORED = 0x03,
	OBD_SERVICE_DTC_CLEAR = 0x04,
	OBD_SERVICE_TEST_O2 = 0x05,
	OBD_SERVICE_TEST_OTHER = 0x06,
	OBD_SERVICE_DTC_PENDING = 0x07,
	OBD_SERVICE_CONTROL = 0x08,
	OBD_SERVICE_INFO_VEHICLE = 0x09,
	OBD_SERVICE_DTC_PERMANENT = 0x0A,
} OBD_SERVICE;

typedef enum {
	OBD_S1_PID_SUPPORTED = 0x00,
	OBD_S1_PID_COOLANT_TEMP = 0x05,
	OBD_S1_PID_ENGINE_SPEED = 0x0C,
	OBD_S1_PID_VEHICLE_SPEED = 0x0D,
	OBD_S1_PID_INTAKE_AIR_TEMP = 0x0F,
	OBD_S1_PID_THROTTLE = 0x11,
} OBD_S1_PID;

typedef enum {
	OBD_S9_PID_SUPPORTED = 0x00,
	OBD_S9_PID_VIN = 0x02,
} OBD_S9_PID;

typedef enum
{
	OBD_STATUS_OK = 0,          // at least one ECU answered (possibly negatively)
	OBD_STATUS_TIMEOUT = 1,     // nobody answered
	OBD_STATUS_TX_FAILED = 2,   // could not send (bus-off, TX FIFO full)
} obd_status_t;

typedef enum
{
	OBD_ADDRESSING_UNKNOWN = 0, // no vehicle has answered yet
	OBD_ADDRESSING_11BIT = 1,
	OBD_ADDRESSING_29BIT = 2,
} obd_addressing_t;

typedef enum
{
	OBD_KIND_HOST = 0,          // raw request from a host, result goes to OBD_OnRequestDone()
	OBD_KIND_HOST_VIN = 1,      // VIN request from a host, result goes to OBD_OnRequestDone()
	OBD_KIND_LIVE = 2,          // internal: live data polling
	OBD_KIND_VIN = 3,           // internal: VIN read at startup
	OBD_KIND_PIDS = 4,          // internal: supported PID discovery
} obd_kind_t;

// obd_request_t.flags (same values as the host protocol's DIAG_REQUEST)
#define OBD_FLAG_AUTO       0x01    // the vehicle's OBD broadcast; tx_id, rx_id, rx_mask are not used
#define OBD_FLAG_FUNCTIONAL 0x02    // several ECUs may answer: collect until the timeout
#define OBD_FLAG_EXT_ADDR   0x04    // extended addressing: tx_ext / rx_ext before the PCI
#define OBD_FLAGS_ALL       0x07

typedef struct
{
	uint8_t flags;              // OBD_FLAG_*
	uint32_t tx_id;             // CAN ID the request goes to (CAN_ID_EXT for 29-bit)
	uint32_t rx_id;             // answers come from IDs matching rx_id ...
	uint32_t rx_mask;           // ... on these bits
	uint8_t tx_ext;             // extended addressing: address byte we send
	uint8_t rx_ext;             // extended addressing: address byte answers carry
	uint8_t pad;                // padding byte
	uint16_t timeout_ms;        // physical: wait for the answer; functional: collect answers; 0 = default
	uint8_t len;                // request length, 1-7 (1-6 with extended addressing)
	uint8_t data[7];            // service ID and parameters
	uint8_t kind;               // obd_kind_t
	uint8_t transport;          // which host asked (OBD_KIND_HOST*)
	uint8_t seq;                // host request sequence number
} obd_request_t;

void OBD_Init(void);
void OBD_Process(void);

// false if the queue is full or the request is malformed
bool OBD_Submit(const obd_request_t *req);

// sets up `req` as a request to the vehicle's OBD broadcast (OBD_FLAG_AUTO)
void OBD_SetBroadcast(obd_request_t *req);
// sets up `req` as a request to the ECU that answers on `response_id` (ISO 15765-4)
void OBD_SetPhysical(obd_request_t *req, uint32_t response_id);

obd_addressing_t OBD_GetAddressing(void);

// true (and copies 17 characters) once the VIN is known
bool OBD_GetVin(char vin[17]);

// supported mode 01 PID discovery: asks for PIDs 0x00, 0x20, ... for as long as
// some ECU reports the next range as supported. Runs at startup; call
// OBD_DiscoverPids() to retry after it failed (e.g. ignition was off).
// OBD_OnPidsDiscovered() reports the result.
void OBD_DiscoverPids(void);
// true (and copies OBD_PID_BITMAP_LEN bytes) once the supported PIDs are known
bool OBD_GetSupportedPids(uint8_t bitmap[OBD_PID_BITMAP_LEN]);

// live polling of mode 01 PIDs; only runs while started
void OBD_LiveStart(uint16_t interval_ms, const uint8_t *pids, uint8_t count);
void OBD_LiveStop(void);
bool OBD_LiveActive(void);

// implemented by the application:
// a host request finished
void OBD_OnRequestDone(const obd_request_t *req, obd_status_t status, const uint8_t *records, uint16_t records_len);
// supported PID discovery finished: OBD_STATUS_OK once the PIDs are known,
// otherwise why not (no ECU answered PID 0x00, or the request could not be sent)
void OBD_OnPidsDiscovered(obd_status_t status);
// one live polling cycle finished; records are [pid u8][len u8][data] (data excludes 0x41 and the PID)
void OBD_OnLiveData(const uint8_t *records, uint16_t records_len);

#endif /* OBD_H_ */
