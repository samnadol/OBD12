/*
 * uart.c
 *
 *  Created on: Aug 26, 2025
 *      Author: samna
 */

#include "UART/Uart.h"

#include <stdlib.h>

#include "BLE/BLE_Services/NordicUART.h"
#include "stm32g4xx_hal.h"

enum
{
	UART_BLE = 0,
	UART_USB = 1,
} UART_SRC;

#define USB_UART_COMMAND_BUFFER_SIZE 1024
uint8_t UsbUARTCommandBuffer[USB_UART_COMMAND_BUFFER_SIZE];
size_t UsbUARTCommandBufferIndex = 0;
uint8_t UsbUARTExpectedLength = 0;

extern UART_HandleTypeDef huart1;

void UART_RX(uint8_t *data, size_t size, uint8_t source)
{
	if (size != sizeof(UART_COMMAND))
	{
		PRINT_DBG("RECEIVED UART != sizeof(UART_COMMAND)!\r\n");
		return;
	}

	UART_COMMAND *command = (UART_COMMAND *)malloc(sizeof(UART_COMMAND));
	memcpy(command, data, sizeof(UART_COMMAND));

	PRINT_DBG("Got command from %d: %d %s", source, command->data_len, command->data);
}

void UART_TX(uint8_t *data, size_t size)
{
	UART_USB_TX(data, size);
	UART_BLE_TX(data, size);
}

void UART_USB_TX(uint8_t *data, size_t size)
{
	HAL_UART_Transmit(&huart1, data, size, HAL_MAX_DELAY);
}

void UART_BLE_TX(uint8_t *data, size_t size)
{
	ServiceNordicUART_CharacteristicTX_Update(data, size);
}

void UART_BLE_RX(uint8_t *data, size_t size)
{
	UART_RX(data, size, UART_BLE);
}

void UART_USB_RX(uint8_t *data, size_t size)
{
	if (size > 1)
	{
		printf("UART RX SIZE > 1 UNEXPECTEDLY\r\n");
	}
	else if (size == 1)
	{
		if (data[0] == UART_STRUCT_STARTCHAR)
		{
			UsbUARTCommandBufferIndex = 0;
		}

		UsbUARTCommandBuffer[UsbUARTCommandBufferIndex] = data[0];
		if (UsbUARTCommandBufferIndex - 3 == UsbUARTCommandBuffer[1])
		{
			if (UsbUARTCommandBuffer[0] == UART_STRUCT_STARTCHAR && UsbUARTCommandBuffer[UsbUARTCommandBuffer[1] + 3] == UART_STRUCT_ENDCHAR)
			{
				UART_RX(UsbUARTCommandBuffer, UsbUARTCommandBufferIndex + 1, UART_USB);
			}
		}

		UsbUARTCommandBufferIndex++;
	}
}
