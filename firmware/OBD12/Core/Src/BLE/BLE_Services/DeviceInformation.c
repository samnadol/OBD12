/*
 * DeviceInformation.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/DeviceInformation.h>

#define UUID_SERV_DEVICEINFORMATION 0x180A

uint16_t Handle_Serv_DeviceInformation;

tBleStatus ServiceDeviceInformation_Add(void) {
	tBleStatus ret;

	uint8_t char_number = 2;
	uint8_t max_attribute_records = 1 + (3 * char_number);

	Service_UUID_t service_uuid = { .Service_UUID_16 = UUID_SERV_DEVICEINFORMATION };
	ret = aci_gatt_add_service(UUID_TYPE_16, &service_uuid, PRIMARY_SERVICE,
			max_attribute_records, &Handle_Serv_DeviceInformation);
	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceDeviceInformation_CharacteristicManufacturerName_Add();
	ServiceDeviceInformation_CharacteristicModelNumber_Add();

	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
