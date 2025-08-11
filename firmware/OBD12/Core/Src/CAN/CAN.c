/*
 * CAN.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#include "CAN/CAN.h"

#include "CAN/OBD.h"

#include <stdio.h>

extern void Error_Handler(void);

GPIO_TypeDef *FDCAN_FAULT_BANK = GPIOA;
uint16_t FDCAN_FAULT_PIN = GPIO_PIN_2;

GPIO_TypeDef *FDCAN_SILENT_BANK = GPIOA;
uint16_t FDCAN_SILENT_PIN = GPIO_PIN_3;

void CAN_printFrame(CAN_FRAME *frame) {
	uint8_t *raw = (uint8_t*) frame;

	for (int i = 0; i < 8; i++)
		printf("%02x ", raw[i]);
	printf("\r\n");
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == FDCAN_FAULT_PIN) {
		printf("FDCAN TRANSCEIVER IN FAULT STATE\r\n");
	}
}

void CAN_Config(FDCAN_HandleTypeDef *hfdcan) {
	HAL_GPIO_WritePin(FDCAN_SILENT_BANK, FDCAN_SILENT_PIN, GPIO_PIN_RESET);

	FDCAN_FilterTypeDef sFilterConfig;
	sFilterConfig.IdType = FDCAN_STANDARD_ID;
	sFilterConfig.FilterIndex = 0;
	sFilterConfig.FilterType = FDCAN_FILTER_MASK;
	sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	sFilterConfig.FilterID1 = 0x000;
	sFilterConfig.FilterID2 = 0x000;

	if (HAL_FDCAN_ConfigFilter(hfdcan, &sFilterConfig) != HAL_OK) {
		Error_Handler();
	}

	if (HAL_FDCAN_Start(hfdcan) != HAL_OK) {
		Error_Handler();
	}

	if (HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0)
			!= HAL_OK) {
		Error_Handler();
	}
}

void CAN_Rx(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
	if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
		FDCAN_RxHeaderTypeDef rxHeader;
		uint8_t rxData[8];

		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHeader, rxData)
				!= HAL_OK) {
			Error_Handler();
		}

		OBD_Rx(rxHeader.Identifier, (CAN_FRAME*) rxData);
	}
}

void CAN_Tx(FDCAN_HandleTypeDef *hfdcan, uint16_t target_identifier,
		uint8_t *data, size_t len) {
	FDCAN_TxHeaderTypeDef txHeader;
	txHeader.Identifier = target_identifier;
	txHeader.IdType = FDCAN_STANDARD_ID;
	txHeader.TxFrameType = FDCAN_DATA_FRAME;
	txHeader.DataLength = len;
	txHeader.ErrorStateIndicator = FDCAN_ESI_PASSIVE;
	txHeader.BitRateSwitch = FDCAN_BRS_OFF;
	txHeader.FDFormat = FDCAN_CLASSIC_CAN;
	txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	txHeader.MessageMarker = 0;

	HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &txHeader, data);
}
