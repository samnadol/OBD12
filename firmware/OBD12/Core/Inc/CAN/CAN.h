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
	CAN_PCI_SF = 0x0,
	CAN_PCI_FF = 0x1,
	CAN_PCI_CF = 0x2,
	CAN_PCI_FC = 0x3,
} CAN_PCI_TYPES;

void CAN_Config(FDCAN_HandleTypeDef *hfdcan);
void CAN_printFrame(uint8_t *frame);

void CAN_Rx(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs);
void CAN_Tx(FDCAN_HandleTypeDef *hfdcan, uint16_t target_identifier, uint8_t *data, size_t len);

#endif /* CAN_H_ */
