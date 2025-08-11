/*
 * ManufacturerName.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/DeviceInformation/ManufacturerName.h>
#include <BLE/BLE_Services/DeviceInformation.h>

#define UUID_CHAR_DEVICEINFORMATION_MANUFACTURERNAME 0x2A29
#define ManufacturerName "SN"

uint16_t Handle_Char_DeviceInformation_ManufacturerName;

tBleStatus ServiceDeviceInformation_CharacteristicManufacturerName_Update() {
	tBleStatus ret;

	ret = aci_gatt_update_char_value(Handle_Serv_DeviceInformation,
			Handle_Char_DeviceInformation_ManufacturerName, 0, strlen(ManufacturerName),
			(uint8_t*) &ManufacturerName);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while updating DeviceInformation ManufacturerName: 0x%04X\r\n", ret);
		return BLE_STATUS_ERROR;
	}

	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceDeviceInformation_CharacteristicManufacturerName_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_16 = UUID_CHAR_DEVICEINFORMATION_MANUFACTURERNAME };
	ret = aci_gatt_add_char(Handle_Serv_DeviceInformation, UUID_TYPE_16,
			&char_uuid, strlen(ManufacturerName),
			CHAR_PROP_READ, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_DeviceInformation_ManufacturerName);

	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceDeviceInformation_CharacteristicManufacturerName_Update();

	PRINT_DBG("ServiceDeviceInformation_CharacteristicManufacturerName_Add() success, handle %02x\r\n", Handle_Char_DeviceInformation_ManufacturerName);
	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
