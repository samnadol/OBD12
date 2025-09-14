/*
 * ISO_TP.c
 *
 *  Created on: Aug 18, 2025
 *      Author: samna
 */

#include "CAN/ISOTP.h"
#include "CAN/OBD.h"

#include "_Debug.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

extern FDCAN_HandleTypeDef hfdcan1;

typedef struct
{
	uint8_t active;
	uint16_t expectedSize;
	uint16_t receivedSize;
	uint8_t *data;

	uint8_t serviceID;
	uint8_t pID;
} ISO_TP_SESSION;

ISO_TP_SESSION isoTPSessions[8];

uint8_t *ISOTP_DataAccumulator_Add(uint32_t source, uint8_t *canFrame) {
    size_t addSize, frameOffset;
    if (canFrame[0] >> 4 == CAN_PCI_FF)
    {
    	size_t _expectedSize = ((canFrame[0] & 0x0F) << 8) | canFrame[1];
    	isoTPSessions[source - 0x7E8].active = 1;
        isoTPSessions[source - 0x7E8].expectedSize = _expectedSize;
        isoTPSessions[source - 0x7E8].receivedSize = 0;

        if (isoTPSessions[source - 0x7E8].data)
        {
            free(isoTPSessions[source - 0x7E8].data);
        }

        isoTPSessions[source - 0x7E8].data = (uint8_t*) malloc(sizeof(uint8_t) * _expectedSize);

    	addSize = 6;
    	frameOffset = 2;
    }
    else if (canFrame[0] >> 4 == CAN_PCI_CF)
    {
    	addSize = (isoTPSessions[source - 0x7E8].receivedSize + 7 > isoTPSessions[source - 0x7E8].expectedSize) ? isoTPSessions[source - 0x7E8].expectedSize - isoTPSessions[source - 0x7E8].receivedSize : 7;
    	frameOffset = 1;
    }
    else
    {
    	addSize = 0;
    	frameOffset = 0;
    }

	memcpy(isoTPSessions[source - 0x7E8].data + isoTPSessions[source - 0x7E8].receivedSize, canFrame + frameOffset, addSize);
	isoTPSessions[source - 0x7E8].receivedSize += addSize;

	if ((isoTPSessions[source - 0x7E8].expectedSize - isoTPSessions[source - 0x7E8].receivedSize) == 0) // if this conversion is complete, return the data
		return isoTPSessions[source - 0x7E8].data;

	return 0;
}

void ISOTP_ProcessFrame(uint32_t source, uint8_t *frame)
{
	if ((frame[0] >> 4) == CAN_PCI_SF) // single packet data
	{
		OBD_ParseData(frame[0] & 0x0F, frame + 1);
	}
	else if ((frame[0] >> 4) == CAN_PCI_FF) // first frame of multi-frame response
	{
		ISOTP_DataAccumulator_Add(source, frame);

		// send a FC packet back to the ECU to instruct how to send the rest of the packets
		uint8_t packet[8] = { 0 };
		packet[0] = (CAN_PCI_FC << 4) | 0;
		packet[1] = 0; // block size
		packet[2] = 0; // separation time
		CAN_Tx(&hfdcan1, source - 8, (uint8_t*) &packet, 8);
	}
	else if ((frame[0] >> 4) == CAN_PCI_CF) // consecutive frame of multi-frame response
	{
		if (ISOTP_DataAccumulator_Add(source, frame)) // got all parts of this message
		{
			OBD_ParseData(isoTPSessions[source - 0x7E8].receivedSize, isoTPSessions[source - 0x7E8].data);

			free(isoTPSessions[source - 0x7E8].data);

			isoTPSessions[source - 0x7E8].data = 0;
		}
	}
	else
	{
		PRINT_DBG("UNKNOWN CAN FRAME TYPE 0x%x\r\n", (frame[0] >> 4));
	}
}
