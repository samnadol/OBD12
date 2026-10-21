/*
 * OBD.h
 *
 * OBD-II (SAE J1979 over ISO 15765-4) request engine. Requests are queued and
 * sent one at a time; responses from every ECU that answers within the
 * response window are collected and handed back as a list of records:
 *
 *   record = [ecu_id u16 LE][len u8][response message, len bytes]
 *
 * where the response message starts with the response service ID
 * (e.g. 0x41 ...) or 0x7F for a negative response.
 */

#ifndef OBD_H_
#define OBD_H_

#include <stdbool.h>
#include <stdint.h>

#define OBD_ID_BROADCAST 0x7DF
#define OBD_ID_ENGINE    0x7E0

#define OBD_RESPONSE_WINDOW_MS   50     // P2_CAN, ISO 15765-4
#define OBD_RESPONSE_PENDING_MS  5000   // P2*_CAN, after NRC 0x78
#define OBD_RESULT_MAX           236    // bytes of records per request
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
	OBD_KIND_HOST = 0,          // raw request from a host, result goes to OBD_OnRequestDone()
	OBD_KIND_HOST_VIN = 1,      // VIN request from a host, result goes to OBD_OnRequestDone()
	OBD_KIND_LIVE = 2,          // internal: live data polling
	OBD_KIND_VIN = 3,           // internal: VIN read at startup
	OBD_KIND_PIDS = 4,          // internal: supported PID discovery
} obd_kind_t;

typedef struct
{
	uint16_t dest;              // 0x7DF (broadcast) or 0x7E0-0x7E7 (physical)
	uint8_t len;                // request length, 1-7
	uint8_t data[7];            // service ID, PID(s)
	uint8_t kind;               // obd_kind_t
	uint8_t transport;          // which host asked (OBD_KIND_HOST*)
	uint8_t seq;                // host request sequence number
} obd_request_t;

void OBD_Init(void);
void OBD_Process(void);

// false if the queue is full
bool OBD_Submit(const obd_request_t *req);

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
