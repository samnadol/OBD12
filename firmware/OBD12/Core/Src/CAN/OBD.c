/*
 * obd.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#include <stdio.h>

#include "CAN/OBD.h"
#include "CAN/CAN.h"

void OBD_ReadVin(FDCAN_HandleTypeDef *hfdcan)
{
	OBD_Tx(hfdcan, ECU_BROADCAST_IDENTIFIER, OBD_SERVICE_INFO_VEHICLE, OBD_S9_PID_VIN_MESSAGECOUNT);
	OBD_Tx(hfdcan, ECU_BROADCAST_IDENTIFIER, OBD_SERVICE_INFO_VEHICLE, OBD_S9_PID_VIN);
}

void OBD_Rx(uint32_t sourceIdentifier, CAN_FRAME *frame) {
	OBD_PAYLOAD *payload = (OBD_PAYLOAD*) frame->payload;

	printf("CAN FRAME from %lx: ", sourceIdentifier);
	CAN_printFrame(frame);

	// valid ECU identifier range
	if (sourceIdentifier >= 0x7E8 && sourceIdentifier <= 0x7EF) {
		// single packet data
		if (frame->pci.type == CAN_PCI_SF) {
			// response to service command
			if (payload->service > 0x40) {
				// response to service 0x01
				if (payload->service == 0x41) {
					if (payload->pid == OBD_PID_ENGINE_SPEED) {
						printf("CURRENT ENGINE SPEED: %d RPM\r\n", ((payload->data[0] << 8) + payload->data[1]) / 4);
					} else {
						printf("UNKNOWN SERVICE 01 PID\r\n");
					}
				} else {
					printf("UNKNOWN SERVICE (response)\r\n");
				}
			} else {
				printf("UNKNOWN SERVICE (non-response)\r\n");
			}
		} else {
			printf("UNKNOWN CAN FRAME TYPE\r\n");
		}
	} else {
		printf("UNKNOWN CAN IDENTIFIER\r\n");
	}
}

void OBD_Tx(FDCAN_HandleTypeDef *hfdcan, uint32_t destIdentifier, uint8_t service, uint8_t pid) {
	CAN_FRAME frame;
	OBD_PAYLOAD *payload = (OBD_PAYLOAD*) frame.payload;

	payload->service = service;
	payload->pid = pid;
	payload->data[0] = 0;
	payload->data[1] = 0;
	payload->data[2] = 0;
	payload->data[3] = 0;
	payload->data[4] = 0;

	frame.pci.type = CAN_PCI_SF;
	frame.pci.len = 2;

	CAN_Tx(hfdcan, destIdentifier, (uint8_t*) &frame, sizeof(CAN_FRAME));
}
