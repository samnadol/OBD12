#include "BLE/App.h"

#include <stdlib.h>

#include "CAN/OBD.h"
#include "BLE/BLE_Device.h"
#include "BLE/BLE_Events.h"
#include "BLE/BLE_Services/DeviceInformation.h"
#include "BLE/BLE_Services/Status.h"

#include "hci_tl.h"
#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "bluenrg1_events.h"
#include "bluenrg_utils.h"

uint8_t HWCONF_VALUE[2] = { 0 };

extern FDCAN_HandleTypeDef hfdcan1;
extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim7;

volatile uint16_t BLE_CONNECTION_HANDLE = FALSE;
volatile uint8_t BLE_ENABLE_CONNECTION_FLAG = TRUE;
volatile uint8_t BLE_CONNECTED = FALSE;
volatile uint8_t BLE_PAIRING = FALSE;
volatile uint8_t BLE_PAIRED = FALSE;

uint8_t counter = 0;
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	if (htim == &htim6) {
		if (Subscription_Status_Power)
		{
			ServiceStatus_CharacteristicPower_Update();
		}

		if (BLE_CONNECTED)
		{
			HAL_GPIO_TogglePin(STATUS_LED_1_BANK, STATUS_LED_1_PIN);
		}
	}

	if (htim == &htim7)
	{
		counter++;
		switch (counter)
		{
		case 1:
			OBD_Tx(&hfdcan1, ECU_IDENTIFER_BROADCAST, OBD_SERVICE_DATA_LIVE, OBD_S1_PID_ENGINE_SPEED);
			break;
		case 2:
			OBD_Tx(&hfdcan1, ECU_IDENTIFER_BROADCAST, OBD_SERVICE_DATA_LIVE, OBD_S1_PID_COOLANT_TEMP);
			break;
		case 3:
			OBD_Tx(&hfdcan1, ECU_IDENTIFER_BROADCAST, OBD_SERVICE_DATA_LIVE, OBD_S1_PID_INTAKE_AIR_TEMP);
			break;
		case 4:
			OBD_Tx(&hfdcan1, ECU_IDENTIFER_BROADCAST, OBD_SERVICE_DATA_LIVE, OBD_S1_PID_THROTTLE);
			break;
		default:
			struct OBD_CachedData *rpm = OBD_GetData_Cache(OBD_SERVICE_DATA_LIVE, OBD_S1_PID_ENGINE_SPEED);
			struct OBD_CachedData *coolant_temp = OBD_GetData_Cache(OBD_SERVICE_DATA_LIVE, OBD_S1_PID_COOLANT_TEMP);
			struct OBD_CachedData *air_temp = OBD_GetData_Cache(OBD_SERVICE_DATA_LIVE, OBD_S1_PID_INTAKE_AIR_TEMP);
			struct OBD_CachedData *throttle = OBD_GetData_Cache(OBD_SERVICE_DATA_LIVE, OBD_S1_PID_THROTTLE);

			if (rpm && coolant_temp && air_temp && throttle)
			{
				printf("RPM: %d, Coolant Temp: %d C, Intake Air Temp: %d C, Throttle: %.2f\r\n", ((rpm->data[0] << 8) + rpm->data[1]) / 4, coolant_temp->data[0] - 40, air_temp->data[0] - 40, throttle->data[0] / 255.0 * 100);
			}

			counter = 0;
		}
	}
}

static void User_Init(void) {
	HWCONF_VALUE[0] = HAL_GPIO_ReadPin(HWCONF_1_BANK, HWCONF_1_PIN);
	HWCONF_VALUE[1] = HAL_GPIO_ReadPin(HWCONF_2_BANK, HWCONF_2_PIN);

	PRINT_DBG("\033[2J"); /* serial console clear screen */
	PRINT_DBG("\033[H"); /* serial console cursor to home */

	uint32_t uid[3];
	uid[0] = HAL_GetUIDw0();
	uid[1] = HAL_GetUIDw1();
	uid[2] = HAL_GetUIDw2();

	PRINT_DBG("OBD12\r\n");
	PRINT_DBG("Hardware Configuration: 0b%d%d\r\n", HWCONF_VALUE[0], HWCONF_VALUE[1]);

	PRINT_DBG("STM32 UID: ");
	for (int i = 0; i < 3; i++)
		PRINT_DBG("%lX", uid[i]);
	PRINT_DBG("\r\n");

	struct OBD_CachedData *vinData = OBD_GetData_Poll(ECU_IDENTIFIER_ENGINE, OBD_SERVICE_INFO_VEHICLE, OBD_S9_PID_VIN, 2000);
	if (vinData)
	{
		PRINT_DBG("CAR VIN: ");
		for (int i = 0; i < vinData->data_size; i++)
			PRINT_DBG("%c", vinData->data[i]);
		PRINT_DBG("\r\n");
	}

	HAL_TIM_Base_Start_IT(&htim6);
	HAL_TIM_Base_Start_IT(&htim7);

	HAL_GPIO_WritePin(STATUS_LED_1_BANK, STATUS_LED_1_PIN, GPIO_PIN_SET);
}

void MX_BlueNRG_2_Init(void) {
	User_Init();

	hci_init(BLE_ProcessUserEvent, NULL);

	if (BLE_Device_Init() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("BLE_DeviceInit() failed\r\n");
		while (1)
			;
	}
}

static void User_Process(void) {
	uint8_t ret;

	if (BLE_ENABLE_CONNECTION_FLAG) {
		BLE_Device_SetDiscoverable();
		BLE_ENABLE_CONNECTION_FLAG = FALSE;
	}

	if (BLE_CONNECTED && !BLE_PAIRING) {
		ret = aci_gap_slave_security_req(BLE_CONNECTION_HANDLE);
		if (ret != BLE_STATUS_SUCCESS)
			PRINT_DBG("aci_gap_slave_security_req() failed:0x%02x\r\n", ret);
		BLE_PAIRING = TRUE;
	}

//	if (BLE_PAIRED) {
//		HAL_Delay(1000);
//	}
}

void MX_BlueNRG_2_Process(void) {
	hci_user_evt_proc();
	User_Process();
}
