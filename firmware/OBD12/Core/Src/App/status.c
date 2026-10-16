/*
 * status.c
 */

#include "App/status.h"

#include "main.h"
#include "App/power.h"
#include "App/sched.h"
#include "BLE/App.h"
#include "BLE/BLE_Services/Status.h"
#include "CAN/CAN.h"
#include "Comms/comms.h"

#define STATUS_PERIOD_MS 1000

static uint32_t last_tick;
static uint32_t last_rx_frames;

void status_init(void)
{
	power_init();
	HAL_GPIO_WritePin(STATUS_LED_1_GPIO_Port, STATUS_LED_1_Pin, GPIO_PIN_SET);
}

void status_process(void)
{
	if (!every_ms(&last_tick, STATUS_PERIOD_MS))
		return;

	if (BLE_Connected() && Subscription_Status_Power)
		ServiceStatus_CharacteristicPower_Update();

	if (comms_any_subscribed(TOPIC_POWER))
	{
		proto_power_t power;
		power_read(&power);
		comms_event(TOPIC_POWER, EVT_POWER, &power, sizeof(power));
	}

	if (BLE_Connected())
		HAL_GPIO_TogglePin(STATUS_LED_1_GPIO_Port, STATUS_LED_1_Pin);
	else
		HAL_GPIO_WritePin(STATUS_LED_1_GPIO_Port, STATUS_LED_1_Pin, GPIO_PIN_SET);

	uint32_t rx_frames = CAN_GetStats()->rx_frames;
	bool can_active = CAN_GetState() == CAN_STATE_OK && rx_frames != last_rx_frames;
	last_rx_frames = rx_frames;
	HAL_GPIO_WritePin(STATUS_LED_2_GPIO_Port, STATUS_LED_2_Pin, can_active ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
