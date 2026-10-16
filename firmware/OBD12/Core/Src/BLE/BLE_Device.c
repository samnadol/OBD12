/*
 * BLE_Device.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 *
 * Security: bonding with MITM protection. The device has no display or
 * keyboard, so it uses a fixed 6-digit passkey derived from the MCU UID (see
 * system_ble_passkey(); it is reported over USB by CMD_GET_INFO). Bonds are
 * kept across reboots in the BlueNRG flash.
 */

#include "BLE/BLE_Device.h"

#include "_Debug.h"
#include "App/system.h"
#include "BLE/BLE_Services/DeviceInformation.h"
#include "BLE/BLE_Services/NordicUART.h"
#include "BLE/BLE_Services/Status.h"

#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"
#include "hci_const.h"

#define MAC_NVM_OFFSET 0x80

uint8_t bdaddr[BDADDR_SIZE];

uint8_t BLE_Device_Init(void) {
	uint16_t service_handle, dev_name_char_handle, appearance_char_handle;

	hci_reset();
	HAL_Delay(100);

	uint8_t bdaddr_len_out;
	if (aci_hal_read_config_data(MAC_NVM_OFFSET, &bdaddr_len_out, bdaddr)) {
		PRINT_DBG("Read Static Random address failed.\r\n");
		return BLE_STATUS_ERROR;
	}

	if ((bdaddr[5] & 0xC0) != 0xC0) {
		PRINT_DBG("Static Random address not well formed.\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_hal_write_config_data(CONFIG_DATA_PUBADDR_OFFSET, bdaddr_len_out, bdaddr) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_hal_write_config_data() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	// high power, level 4 (-2 dBm)
	if (aci_hal_set_tx_power_level(1, 4) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_hal_set_tx_power_level() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gatt_init() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gatt_init() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gap_init(GAP_PERIPHERAL_ROLE, 0x00, 0x07, &service_handle, &dev_name_char_handle, &appearance_char_handle) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gap_init() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	uint8_t device_name[] = { ADVERTISE_NAME };
	if (aci_gatt_update_char_value(service_handle, dev_name_char_handle, 0, sizeof(device_name), device_name) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gatt_update_char_value() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gap_set_io_capability(IO_CAP_DISPLAY_ONLY) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error Setting I/O Capability\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gap_set_authentication_requirement(BONDING, MITM_PROTECTION_REQUIRED, SC_IS_SUPPORTED, KEYPRESS_IS_NOT_SUPPORTED, 7, 16, USE_FIXED_PIN_FOR_PAIRING, system_ble_passkey(), 0x00) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gap_set_authentication_requirement() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	if (ServiceDeviceInformation_Add() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while adding DeviceInformation Service\r\n");
		return BLE_STATUS_ERROR;
	}

	if (ServiceStatus_Add() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while adding Status Service\r\n");
		return BLE_STATUS_ERROR;
	}

	if (ServiceNordicUART_Add() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while adding NordicUART Service\r\n");
		return BLE_STATUS_ERROR;
	}

	PRINT_DBG("BLE initialized, address %02X:%02X:%02X:%02X:%02X:%02X\r\n",
			bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0]);
	return BLE_STATUS_SUCCESS;
}

void BLE_Device_SetDiscoverable(void) {
	tBleStatus ret;
	uint8_t local_name[] = { AD_TYPE_COMPLETE_LOCAL_NAME, ADVERTISE_NAME };

	// the stack adds the flags and the local name itself; this adds the rest
	// (3 + 7 + 4 + 11 = 25 of 31 bytes)
	uint8_t adv_data[] = {
			/* incomplete list of 16-bit services: Device Information */
			3, AD_TYPE_16_BIT_SERV_UUID, 0x0A, 0x18,

			/* manufacturer data: company ID, then the address (MSB first) */
			9, AD_TYPE_MANUFACTURER_SPECIFIC_DATA,
			ADVERTISE_COMPANY_ID & 0xFF, ADVERTISE_COMPANY_ID >> 8,
			bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0]
	};

	hci_le_set_scan_response_data(0, NULL);

	ret = aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR, NO_WHITE_LIST_USE, sizeof(local_name), local_name, 0, NULL, 0, 0);
	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gap_set_discoverable() failed: 0x%02x\r\n", ret);
		return;
	}

	ret = aci_gap_update_adv_data(sizeof(adv_data), adv_data);
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gap_update_adv_data() failed: 0x%02x\r\n", ret);
	else
		PRINT_DBG("BLE advertising\r\n");
}
