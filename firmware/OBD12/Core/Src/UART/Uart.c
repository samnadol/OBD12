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
uint8_t UsbUARTProc = 0;

extern UART_HandleTypeDef huart1;

void UART_RX(uint8_t *data, size_t size, uint8_t source)
{
	if (size != sizeof(UART_COMMAND))
	{
		PRINT_DBG("RECEIVED UART != sizeof(UART_COMMAND) (%d, %d)!\r\n", size, sizeof(UART_COMMAND));
		return;
	}

	UART_COMMAND *command = (UART_COMMAND *)malloc(sizeof(UART_COMMAND));
	memcpy(command, data, sizeof(UART_COMMAND));

	//	PRINT_DBG("Got command from %d: %d %s\r\n", source, command->data_len, command->data);
	PRINT_DBG("Got command from %d, %d\r\n", source, command->data_len);
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
			printf("proc start\r\n");
			UsbUARTCommandBufferIndex = 0;
			UsbUARTProc = 1;
		}

		if (UsbUARTProc)
		{
			UsbUARTCommandBuffer[UsbUARTCommandBufferIndex] = data[0];
			printf("%d %d\r\n", UsbUARTCommandBufferIndex, UsbUARTCommandBuffer[1]);

			if (UsbUARTCommandBufferIndex - 3 == UsbUARTCommandBuffer[1])
			{
				if (UsbUARTCommandBuffer[0] == UART_STRUCT_STARTCHAR && UsbUARTCommandBuffer[UsbUARTCommandBuffer[1] + 3] == UART_STRUCT_ENDCHAR)
				{
					printf("proc end\r\n");
					UART_RX(UsbUARTCommandBuffer, UsbUARTCommandBufferIndex + 1, UART_USB);
				}
				else
				{ // did not get stop when expected
					UsbUARTProc = 0;
					printf("did not get stop when expected\r\n");
				}
			}

			UsbUARTCommandBufferIndex++;
		}
		else
		{
			printf("got data but proc is false\r\n");
		}
	}
}
