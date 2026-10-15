/*
 * transport_usb.c
 *
 * RX: one byte at a time into the comms RX ring, from the UART interrupt.
 * TX: contiguous chunks of the comms TX ring are sent with HAL_UART_Transmit_IT;
 * the completion interrupt consumes the chunk and starts the next one.
 */

#include "Comms/transport_usb.h"

#include "Comms/comms.h"

#define TX_CHUNK_MAX 128

static UART_HandleTypeDef *uart;
static uint8_t rx_byte;
static volatile uint16_t tx_in_flight; // bytes handed to the HAL, not yet consumed

// interrupts must be disabled (or we are in the UART interrupt)
static void start_tx(void)
{
	if (tx_in_flight)
		return;

	const uint8_t *data;
	uint16_t len = ring_contig(comms_tx_ring(TRANSPORT_USB), &data);
	if (len == 0)
		return;
	if (len > TX_CHUNK_MAX)
		len = TX_CHUNK_MAX;

	if (HAL_UART_Transmit_IT(uart, (uint8_t*) data, len) == HAL_OK)
		tx_in_flight = len;
}

static void kick(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	start_tx();
	__set_PRIMASK(primask);
}

void transport_usb_init(UART_HandleTypeDef *huart)
{
	uart = huart;
	comms_set_tx_kick(TRANSPORT_USB, kick);
	HAL_UART_Receive_IT(uart, &rx_byte, 1);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart != uart)
		return;

	comms_rx(TRANSPORT_USB, &rx_byte, 1);
	HAL_UART_Receive_IT(uart, &rx_byte, 1);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
	if (huart != uart)
		return;

	ring_skip(comms_tx_ring(TRANSPORT_USB), tx_in_flight);
	tx_in_flight = 0;
	start_tx();
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart != uart)
		return;

	// overrun / framing / noise errors abort reception; re-arm it
	if (huart->RxState == HAL_UART_STATE_READY)
		HAL_UART_Receive_IT(uart, &rx_byte, 1);
}
