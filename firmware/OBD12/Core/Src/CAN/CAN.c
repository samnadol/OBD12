/*
 * CAN.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 */

#include "CAN/CAN.h"

#include "CAN/OBD.h"

#include "_Debug.h"
#include <stdio.h>
#include <string.h>

extern void Error_Handler(void);

#define FDCAN_FAULT_BANK GPIOA
#define FDCAN_FAULT_PIN GPIO_PIN_2

#define FDCAN_SILENT_BANK GPIOA
#define FDCAN_SILENT_PIN GPIO_PIN_3

void CAN_printFrame(uint8_t *frame) {
	for (int i = 0; i < 8; i++)
		PRINT_DBG("%02x ", frame[i]);
	PRINT_DBG("\r\n");
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == FDCAN_FAULT_PIN) {
		PRINT_DBG("FDCAN TRANSCEIVER IN FAULT STATE\r\n");
	}
}

void CAN_Config(FDCAN_HandleTypeDef *hfdcan) {
	HAL_GPIO_WritePin(FDCAN_SILENT_BANK, FDCAN_SILENT_PIN, GPIO_PIN_RESET);

	FDCAN_FilterTypeDef sFilterConfig;
	sFilterConfig.IdType = FDCAN_STANDARD_ID;        		// Standard 11-bit IDs
	sFilterConfig.FilterIndex = 0;                   		// First filter slot
	sFilterConfig.FilterType = FDCAN_FILTER_RANGE;  		// Accept a range of IDs
	sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;  	// Send matching frames to FIFO0
	sFilterConfig.FilterID1 = 0x000;                 		// Range start
	sFilterConfig.FilterID2 = 0x7FF;                 		// Range end (all 11-bit IDs)

	if (HAL_FDCAN_ConfigFilter(hfdcan, &sFilterConfig) != HAL_OK) {
		Error_Handler();
	}

//	HAL_FDCAN_ConfigGlobalFilter(
//	    hfdcan,
//	    FDCAN_ACCEPT_IN_RX_FIFO0,   // Non-matching standard frames → FIFO0
//	    FDCAN_ACCEPT_IN_RX_FIFO0,   // Non-matching extended frames → FIFO0
//	    FDCAN_REJECT,               // Reject remote frames (std)
//	    FDCAN_REJECT                // Reject remote frames (ext)
//	);

	if (HAL_FDCAN_Start(hfdcan) != HAL_OK) {
		Error_Handler();
	}

	if (HAL_FDCAN_ActivateNotification(hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0)
			!= HAL_OK) {
		Error_Handler();
	}

	PRINT_DBG("CAN Initialized\r\n");
}

void CAN_Rx(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
	if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != RESET) {
		FDCAN_RxHeaderTypeDef rxHeader;
		uint8_t rxData[32];

		memset(rxData, 0, 32);
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rxHeader, rxData)
				!= HAL_OK) {
			Error_Handler();
		}

//		printf("CAN RX %lx: ", rxHeader.Identifier);
//		for (int i = 0; i < rxHeader.DataLength; i++)
//			printf("%02x ", rxData[i]);
//		printf("\r\n");

		OBD_Rx(rxHeader.Identifier, rxData);
	}
}

void CAN_Tx(FDCAN_HandleTypeDef *hfdcan, uint16_t target_identifier,
		uint8_t *data, size_t len) {
	FDCAN_TxHeaderTypeDef txHeader;
	txHeader.Identifier = target_identifier;
	txHeader.IdType = FDCAN_STANDARD_ID;
	txHeader.TxFrameType = FDCAN_DATA_FRAME;
	txHeader.DataLength = len;
	txHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	txHeader.BitRateSwitch = FDCAN_BRS_OFF;
	txHeader.FDFormat = FDCAN_CLASSIC_CAN;
	txHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	txHeader.MessageMarker = 0;

//	printf("CAN TX %lx: ", txHeader.Identifier);
//	for (int i = 0; i < txHeader.DataLength; i++)
//		printf("%02x ", data[i]);
//	printf("\r\n");

	HAL_FDCAN_AddMessageToTxFifoQ(hfdcan, &txHeader, data);
}
