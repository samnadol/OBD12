/*
 * Uart.h
 *
 *  Created on: Aug 26, 2025
 *      Author: samna
 */

#ifndef INC_UART_UART_H_
#define INC_UART_UART_H_

#include <stdint.h>
#include <stddef.h>

typedef struct
{
	uint8_t start;
	uint8_t data_len;
	uint8_t *data;
	uint8_t end;
} UART_COMMAND;

#define UART_STRUCT_STARTCHAR 'A' // 0x02
#define UART_STRUCT_ENDCHAR 'Z' // 0x04

void UART_TX(uint8_t *data, size_t size);
void UART_RX(uint8_t *data, size_t size, uint8_t source);

void UART_BLE_RX(uint8_t *data, size_t size);
void UART_USB_RX(uint8_t *data, size_t size);

void UART_BLE_TX(uint8_t *data, size_t size);
void UART_USB_TX(uint8_t *data, size_t size);

#endif /* INC_UART_UART_H_ */
