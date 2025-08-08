#include <BLE/App.h>
#include <BLE/BLE_Device.h>
#include <BLE/BLE_Events.h>
#include <BLE/BLE_Services/DeviceInformation.h>
#include <BLE/BLE_Services/Status.h>
#include <stdlib.h>

#include "hci_tl.h"
#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "bluenrg1_events.h"
#include "bluenrg_utils.h"

GPIO_TypeDef *STATUS_LED_BANK[2] = { GPIOA, GPIOB };
uint16_t STATUS_LED_PIN[2] = { GPIO_PIN_5, GPIO_PIN_0 };

GPIO_TypeDef *HWCONF_BANK[2] = { GPIOA, GPIOA };
uint16_t HWCONF_PIN[2] = { GPIO_PIN_4, GPIO_PIN_8 };

uint8_t HWCONF_VALUE[2] = { 0 };

volatile uint16_t BLE_CONNECTION_HANDLE = FALSE;
volatile uint8_t  BLE_ENABLE_CONNECTION_FLAG = TRUE;
volatile uint8_t  BLE_CONNECTED = FALSE;
volatile uint8_t  BLE_PAIRING = FALSE;
volatile uint8_t  BLE_PAIRED = FALSE;

static void User_Init(void) {
	HAL_GPIO_WritePin(STATUS_LED_BANK[0], STATUS_LED_PIN[0], GPIO_PIN_SET);

	HWCONF_VALUE[0] = HAL_GPIO_ReadPin(HWCONF_BANK[0], HWCONF_PIN[0]);
	HWCONF_VALUE[1] = HAL_GPIO_ReadPin(HWCONF_BANK[1], HWCONF_PIN[1]);
}

void MX_BlueNRG_2_Init(void) {
	User_Init();

	PRINT_DBG("\033[2J"); /* serial console clear screen */
	PRINT_DBG("\033[H"); /* serial console cursor to home */
	PRINT_DBG("BlueNRG-2 Application\r\n");
	PRINT_DBG("Hardware Configuration: 0b%d%d\r\n", HWCONF_VALUE[0], HWCONF_VALUE[1]);

	hci_init(BLE_ProcessUserEvent, NULL);

	if (BLE_Device_Init() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("BLE_DeviceInit() failed\r\n");
		while (1);
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

	if (BLE_PAIRED) {
		if (Subscription_Status_Power)
			ServiceStatus_CharacteristicPower_Update();

		HAL_Delay(1000);
	}
}

void MX_BlueNRG_2_Process(void) {
	hci_user_evt_proc();
	User_Process();
}
