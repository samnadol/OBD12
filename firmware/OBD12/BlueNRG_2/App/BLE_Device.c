/*
 * Init.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include "BLE_Device.h"

#include "bluenrg1_aci.h"
#include "bluenrg1_hci_le.h"

#include "BLE_Services/Status.h"
#include "BLE_Services/DeviceInformation.h"
#include "BLE_Services/Command.h"

#define MAC_NVM_OFFSET 0x80

uint8_t bdaddr[BDADDR_SIZE];

//uint8_t getBlueNRGVersion(uint8_t *hwVersion, uint16_t *fwVersion) {
//	uint8_t status;
//	uint8_t hci_version, lmp_pal_version;
//	uint16_t hci_revision, manufacturer_name, lmp_pal_subversion;
//
//	status = hci_read_local_version_information(&hci_version, &hci_revision,
//			&lmp_pal_version, &manufacturer_name, &lmp_pal_subversion);
//
//	if (status == BLE_STATUS_SUCCESS) {
//		*hwVersion = hci_revision >> 8;
//		*fwVersion = (hci_revision & 0xFF) << 8;         // Major Version Number
//		*fwVersion |= ((lmp_pal_subversion >> 4) & 0xF) << 4; // Minor Version Number
//		*fwVersion |= lmp_pal_subversion & 0xF;          // Patch Version Number
//	}
//	return status;
//}

uint8_t BLE_Device_Init(void) {
	uint16_t service_handle, dev_name_char_handle, appearance_char_handle;

	hci_reset();
	HAL_Delay(2000);

//	uint8_t hwVersion = 0;
//	uint16_t fwVersion = 0;
//	getBlueNRGVersion(&hwVersion, &fwVersion);
//	PRINT_DBG("HWver %d, FWver %d\r\n", hwVersion, fwVersion);

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

	if (aci_hal_set_tx_power_level(1, 7) != BLE_STATUS_SUCCESS) {
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

	if (aci_gap_clear_security_db() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("aci_gap_clear_security_db() failed\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gap_set_io_capability(IO_CAP_DISPLAY_ONLY) != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error Setting I/O Capability\r\n");
		return BLE_STATUS_ERROR;
	}

	if (aci_gap_set_authentication_requirement(BONDING, MITM_PROTECTION_REQUIRED, SC_IS_SUPPORTED, KEYPRESS_IS_NOT_SUPPORTED, 7, 16, DONOT_USE_FIXED_PIN_FOR_PAIRING, BLE_CONNECTION_PASSKEY, 0x00) != BLE_STATUS_SUCCESS) {
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

	if (ServiceCommand_Add() != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while adding Command Service\r\n");
		return BLE_STATUS_ERROR;
	}

	return BLE_STATUS_SUCCESS;
}

void BLE_Device_SetDiscoverable(void) {
	uint8_t ret;
	uint8_t local_name[] = { AD_TYPE_COMPLETE_LOCAL_NAME, ADVERTISE_NAME };

	uint8_t manuf_data[] = {
			/* incomplete list of services */
			3, 						0x02, 0x0A, 0x18,

			/* device name */
			1 + ADVERTISE_NAME_LEN, 0x09, ADVERTISE_NAME,

			/* flags */
			2, 						0x01, 0b00000110,

			/* manufacturer data */
			9, 						0xFF, 0x34, 0x12, bdaddr[5], bdaddr[4], bdaddr[3], bdaddr[2], bdaddr[1], bdaddr[0]
		    /*     					COMPANY-ID  -----------------------------MAC-ADDR---------------------------*/
	};

	PRINT_DBG("Advertisement Data (%d): ", sizeof(manuf_data));
	for (int i = 0; i < sizeof(manuf_data); i++)
		PRINT_DBG("%02x ", manuf_data[i]);
	PRINT_DBG("\r\n");

	hci_le_set_scan_response_data(0, NULL);
	ret = aci_gap_set_discoverable(ADV_DATA_TYPE, ADV_INTERV_MIN, ADV_INTERV_MAX, PUBLIC_ADDR, NO_WHITE_LIST_USE, sizeof(local_name), local_name, 0, NULL, 0, 0);
	aci_gap_update_adv_data(sizeof(manuf_data), manuf_data);

	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("aci_gap_set_discoverable() failed: 0x%02x\r\n", ret);
}
