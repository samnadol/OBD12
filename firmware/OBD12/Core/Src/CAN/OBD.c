/*
 * obd.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#include "CAN/OBD.h"
#include "CAN/ISOTP.h"

#include "_Debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;
struct OBD_CachedData *OBD_CachedData_LL;

struct OBD_CachedData *OBD_GetData_Cache(uint8_t service, uint8_t pid)
{
	struct OBD_CachedData *iter = OBD_CachedData_LL;

	while (iter)
	{
		if (iter->service == service && iter->pid == pid)
		{
			return iter;
		}

		iter = iter->next;
	}

	return 0;
}

void OBD_LL_PutData(uint8_t service, uint8_t pid, uint8_t *data, size_t data_size)
{
	struct OBD_CachedData *dataLocation = OBD_GetData_Cache(service, pid);

	if (dataLocation)
	{ // if there is data already, free it
		free(dataLocation->data);
	}
	else
	{ //  if not, allocate space for it and add it to the end of the LL
		dataLocation = (struct OBD_CachedData *)malloc(sizeof(struct OBD_CachedData));
		// the above malloc will never be freed, as it is the data stored in the linked list
		// the data pointed to it can be freed

		if (OBD_CachedData_LL)
		{ // if the LL is > 0 size
			struct OBD_CachedData *iter = OBD_CachedData_LL;
			while (iter->next)
				iter = iter->next;
			iter->next = dataLocation;
		}
		else
		{ // else, set first element to this one
			OBD_CachedData_LL = dataLocation;
		}

		// this is now the end of the list send next to null
		dataLocation->next = 0;
	}

	uint8_t *copiedData = (uint8_t *)malloc(data_size);
	memcpy(copiedData, data, data_size);

	dataLocation->service = service;
	dataLocation->pid = pid;
	dataLocation->data = copiedData;
	dataLocation->data_size = data_size;
	dataLocation->timestamp = HAL_GetTick();
}

struct OBD_CachedData *OBD_GetData_Poll(uint32_t destIdentifier, uint8_t service, uint8_t pid, uint32_t timeoutMs)
{
	uint32_t startTime = HAL_GetTick();

	struct OBD_CachedData *oldData = OBD_GetData_Cache(service, pid);
	uint32_t oldTimestamp = (oldData ? oldData->timestamp : 0);

	OBD_Tx(&hfdcan1, destIdentifier, service, pid);

	struct OBD_CachedData *newData;
	do
	{
		newData = OBD_GetData_Cache(service, pid);

		// if the timeout period elapses, return null as no data was found
		if (HAL_GetTick() - startTime > timeoutMs)
		{
			return 0;
		}


	} while (newData->timestamp <= oldTimestamp);

	// newData's timestamp is newer than oldTimestamp's, return this data
	// theoretically this could also return the old data if it is not updated within the timeoutMs period, but that is not desired functionality
	return newData;
}

void OBD_ParseData(size_t length, uint8_t *frame) {
	if (frame[0] == 0x7F) // negative response to request
	{
		PRINT_DBG("NEGATIVE RESPONSE, requested service: %x, code: %x\r\n", frame[1], frame[2]);
	}
	else if (frame[0] > 0x40) // response to service command
	{
		if (frame[0] == 0x41 || frame[0] == 0x49)
		{
			OBD_LL_PutData(frame[0] - 0x40 - 1, frame[1], &frame[2], length);
		}
		else
		{
			PRINT_DBG("UNKNOWN SERVICE (response) %x\r\n", frame[0]);
		}
	} else {
		PRINT_DBG("UNKNOWN SERVICE (non-response) %x\r\n", frame[0]);
	}
}

void OBD_Rx(uint32_t sourceIdentifier, uint8_t *frame)
{
	if (sourceIdentifier >= 0x7E8 && sourceIdentifier <= 0x7EF) // valid ECU identifier range
	{
		ISOTP_ProcessFrame(sourceIdentifier, frame);
	}
	else
	{
		PRINT_DBG("UNKNOWN CAN IDENTIFIER 0x%lx\r\n", sourceIdentifier);
	}
}

void OBD_Tx(FDCAN_HandleTypeDef *hfdcan, uint32_t destIdentifier, uint8_t service, uint8_t pid)
{
	uint8_t packet[8] = { 0 };
	packet[0] = (CAN_PCI_SF << 4) | 2;
	packet[1] = service;
	packet[2] = pid;
	CAN_Tx(hfdcan, destIdentifier, packet, 8);
}
