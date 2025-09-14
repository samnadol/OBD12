/*
 * obd.h
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#ifndef OBD_H_
#define OBD_H_

#include <CAN/CAN.h>
#include <stdint.h>

#define ECU_IDENTIFER_BROADCAST 0x7DF
#define ECU_IDENTIFIER_ENGINE 0x7E0

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
	OBD_S1_PID_STATUS_SINCE_CLEAR = 0x01,
	OBD_S1_PID_FREEZE_DTC = 0x02,
	OBD_S1_PID_LIVE_FUEL_SYSTEM_STATUS = 0x03,
	OBD_S1_PID_ENGINE_LOAD = 0x04,
	OBD_S1_PID_COOLANT_TEMP = 0x05,
	OBD_S1_PID_STFT1 = 0x06,
	OBD_S1_PID_LTFT1 = 0x07,
	OBD_S1_PID_STFT2 = 0X08,
	OBD_S1_PID_LTFT2 = 0X09,
	OBD_S1_PID_FUEL_PRESSURE = 0X0A,
	OBD_S1_PID_MAP = 0x0B,
	OBD_S1_PID_ENGINE_SPEED = 0x0C,
	OBD_S1_PID_VEHICLE_SPEED = 0x0D,
	OBD_S1_PID_TIMING_ADVANCE = 0x0E,
	OBD_S1_PID_INTAKE_AIR_TEMP = 0x0F,
	OBD_S1_PID_MAF = 0x10,
	OBD_S1_PID_THROTTLE = 0x11,
	OBD_S1_PID_SECONDARY_AIR_STATUS = 0x12,
} OBD_S1_PID;

typedef enum {
	OBD_S9_PID_SUPPORTED = 0x00,
	OBD_S9_PID_VIN_MESSAGECOUNT = 0x01,
	OBD_S9_PID_VIN = 0x02,
} OBD_S9_PID;

struct OBD_CachedData
{
	uint8_t *data;
	size_t data_size;

	uint8_t service;
	uint8_t pid;

	uint32_t timestamp;

	struct OBD_CachedData* next;
};

void OBD_Rx(uint32_t sourceIdentifier, uint8_t *frame);
void OBD_Tx(FDCAN_HandleTypeDef *hfdcan, uint32_t destIdentifier, uint8_t service, uint8_t pid);

struct OBD_CachedData *OBD_GetData_Cache(uint8_t service, uint8_t pid);
struct OBD_CachedData *OBD_GetData_Poll(uint32_t destIdentifier, uint8_t service, uint8_t pid, uint32_t timeout);

void OBD_LL_PutData(uint8_t service, uint8_t pid, uint8_t *data, size_t data_size);

void OBD_ParseData(size_t length, uint8_t *frame);

#endif /* OBD_H_ */
