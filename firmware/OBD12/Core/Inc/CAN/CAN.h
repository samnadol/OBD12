/*
 * fdcan.h
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#ifndef CAN_H_
#define CAN_H_

#include <stdint.h>
#include "stm32g4xx_hal.h"

typedef enum {
	CAN_PCI_SF = 0x0, CAN_PCI_FF = 0x1, CAN_PCI_CF = 0x2, CAN_PCI_FC = 0x3
} CAN_PCI_TYPES;

typedef union {
	struct {
		CAN_PCI_TYPES type :4;
		uint8_t len :4;
	};
	uint8_t raw;
} CAN_PCI;

typedef struct {
	CAN_PCI pci;
	uint8_t payload[7];
} CAN_FRAME;

void CAN_Config(FDCAN_HandleTypeDef *hfdcan);
void CAN_printFrame(CAN_FRAME *frame);
void CAN_Rx(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);
void CAN_Tx(FDCAN_HandleTypeDef *hfdcan, uint16_t target_identifier,
		uint8_t *data, size_t len);

#endif /* CAN_H_ */
