/*
 * CAN.c
 *
 *  Created on: Aug 1, 2025
 *      Author: samna
 *
 * Interrupt handlers here only copy data out of the peripheral and set flags;
 * everything else happens in CAN_Process().
 */

#include "CAN/CAN.h"

#include <string.h>

#include "_Debug.h"
#include "main.h"
#include "App/sched.h"
#include "CAN/ISOTP.h"
#include "Util/ring.h"

#define CAN_RX_RING_SIZE 32
#define BUS_OFF_RECOVERY_INTERVAL_MS 1000

static FDCAN_HandleTypeDef *can_handle;

static can_frame_t rx_storage[CAN_RX_RING_SIZE];
static ring_t rx_ring;

static can_stats_t stats;
static can_state_t state = CAN_STATE_NOT_STARTED;

// set from interrupts, consumed by CAN_Process()
static volatile bool flag_status_changed;
static volatile bool flag_transceiver_fault;

static uint32_t last_recovery_tick;

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
	if (RxFifo0ITs & FDCAN_IT_RX_FIFO0_MESSAGE_LOST)
		stats.rx_dropped++;

	FDCAN_RxHeaderTypeDef header;
	can_frame_t frame;

	while (HAL_FDCAN_GetRxFifoFillLevel(hfdcan, FDCAN_RX_FIFO0) > 0)
	{
		if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &header, frame.data) != HAL_OK)
			break;

		frame.id = header.Identifier;
		frame.dlc = header.DataLength > 8 ? 8 : (uint8_t) header.DataLength;

		if (ring_push(&rx_ring, &frame))
			stats.rx_frames++;
		else
			stats.rx_dropped++;
	}
}

void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *hfdcan, uint32_t ErrorStatusITs)
{
	flag_status_changed = true;
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
	if (GPIO_Pin == FDCAN_FAULT_Pin)
		flag_transceiver_fault = true;
}

static void set_state(can_state_t new_state)
{
	if (new_state == state)
		return;

	state = new_state;
	CAN_OnStateChange(state);
}

static can_state_t read_protocol_state(void)
{
	FDCAN_ProtocolStatusTypeDef ps;
	HAL_FDCAN_GetProtocolStatus(can_handle, &ps);

	if (ps.BusOff)
		return CAN_STATE_BUS_OFF;
	if (ps.ErrorPassive)
		return CAN_STATE_ERROR_PASSIVE;
	if (ps.Warning)
		return CAN_STATE_ERROR_WARNING;
	return CAN_STATE_OK;
}

void CAN_Config(FDCAN_HandleTypeDef *hfdcan)
{
	can_handle = hfdcan;
	ring_init(&rx_ring, rx_storage, sizeof(can_frame_t), CAN_RX_RING_SIZE);

	// normal mode (not listen-only)
	HAL_GPIO_WritePin(FDCAN_SILENT_GPIO_Port, FDCAN_SILENT_Pin, GPIO_PIN_RESET);

	// accept only the OBD-II ECU response IDs, reject everything else in hardware
	FDCAN_FilterTypeDef filter = { 0 };
	filter.IdType = FDCAN_STANDARD_ID;
	filter.FilterIndex = 0;
	filter.FilterType = FDCAN_FILTER_RANGE;
	filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
	filter.FilterID1 = CAN_ID_RESPONSE_FIRST;
	filter.FilterID2 = CAN_ID_RESPONSE_LAST;

	if (HAL_FDCAN_ConfigFilter(hfdcan, &filter) != HAL_OK)
		Error_Handler();

	if (HAL_FDCAN_ConfigGlobalFilter(hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK)
		Error_Handler();

	if (HAL_FDCAN_Start(hfdcan) != HAL_OK)
		Error_Handler();

	uint32_t interrupts = FDCAN_IT_RX_FIFO0_NEW_MESSAGE | FDCAN_IT_RX_FIFO0_MESSAGE_LOST
			| FDCAN_IT_BUS_OFF | FDCAN_IT_ERROR_PASSIVE | FDCAN_IT_ERROR_WARNING;
	if (HAL_FDCAN_ActivateNotification(hfdcan, interrupts, 0) != HAL_OK)
		Error_Handler();

	set_state(CAN_STATE_OK);
	PRINT_DBG("CAN initialized\r\n");
}

void CAN_Process(void)
{
	can_frame_t frame;
	while (ring_pop(&rx_ring, &frame))
		ISOTP_OnFrame(frame.id, frame.data, frame.dlc);

	ISOTP_Poll();

	if (flag_transceiver_fault)
	{
		flag_transceiver_fault = false;
		stats.transceiver_faults++;
		PRINT_DBG("CAN transceiver reported a fault\r\n");
	}

	if (flag_status_changed)
	{
		flag_status_changed = false;
		can_state_t new_state = read_protocol_state();

		if (new_state == CAN_STATE_BUS_OFF && state != CAN_STATE_BUS_OFF)
		{
			stats.bus_off_count++;
			PRINT_DBG("CAN bus-off\r\n");
		}

		set_state(new_state);
	}

	// the controller stays in INIT after bus-off; restarting it starts the
	// standard recovery (128 x 11 recessive bits)
	if (state == CAN_STATE_BUS_OFF && every_ms(&last_recovery_tick, BUS_OFF_RECOVERY_INTERVAL_MS))
	{
		HAL_FDCAN_Stop(can_handle);
		if (HAL_FDCAN_Start(can_handle) == HAL_OK)
			flag_status_changed = true;
	}
}

bool CAN_Tx(uint16_t identifier, const uint8_t data[8])
{
	if (state == CAN_STATE_BUS_OFF || state == CAN_STATE_NOT_STARTED)
	{
		stats.tx_failed++;
		return false;
	}

	FDCAN_TxHeaderTypeDef header = { 0 };
	header.Identifier = identifier;
	header.IdType = FDCAN_STANDARD_ID;
	header.TxFrameType = FDCAN_DATA_FRAME;
	header.DataLength = FDCAN_DLC_BYTES_8;
	header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
	header.BitRateSwitch = FDCAN_BRS_OFF;
	header.FDFormat = FDCAN_CLASSIC_CAN;
	header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
	header.MessageMarker = 0;

	if (HAL_FDCAN_AddMessageToTxFifoQ(can_handle, &header, (uint8_t*) data) != HAL_OK)
	{
		stats.tx_failed++;
		return false;
	}

	stats.tx_frames++;
	return true;
}

void CAN_AbortPendingTx(void)
{
	if (state == CAN_STATE_NOT_STARTED)
		return;

	// a frame on the wire finishes its current attempt but is not retried
	HAL_FDCAN_AbortTxRequest(can_handle, FDCAN_TX_BUFFER0 | FDCAN_TX_BUFFER1 | FDCAN_TX_BUFFER2);
}

can_state_t CAN_GetState(void)
{
	return state;
}

const can_stats_t *CAN_GetStats(void)
{
	return &stats;
}
