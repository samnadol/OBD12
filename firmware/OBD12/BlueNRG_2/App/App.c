#include <App.h>
#include <BLE_Device.h>
#include <BLE_Events.h>
#include <stdlib.h>

#include "hci_tl.h"
#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "bluenrg1_events.h"
#include "bluenrg_utils.h"

#include "BLE_Services/Status.h"
#include "BLE_Services/DeviceInformation.h"

volatile uint16_t BLE_CONNECTION_HANDLE = FALSE;
volatile uint8_t  BLE_ENABLE_CONNECTION_FLAG = TRUE;
volatile uint8_t  BLE_CONNECTED = FALSE;
volatile uint8_t  BLE_PAIRING = FALSE;
volatile uint8_t  BLE_PAIRED = FALSE;

static void User_Init(void) {
	HAL_GPIO_WritePin(STATUS_LED_BANK[0], STATUS_LED_PIN[0], GPIO_PIN_SET);
}

void MX_BlueNRG_2_Init(void) {
	User_Init();
	PRINT_DBG("\033[2J"); /* serial console clear screen */
	PRINT_DBG("\033[H"); /* serial console cursor to home */
	PRINT_DBG("BlueNRG-2 Application\r\n");

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
