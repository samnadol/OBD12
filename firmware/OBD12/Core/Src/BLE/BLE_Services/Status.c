/*
 * status.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/Status.h>

#define UUID_SERV_STATUS { 0x00,0x00,0x00,0x00,0x00,0x01,0x55,0xE1,0x8F,0x00,0x00,0x00,0x00,0xA5,0xC2,0x1B }

uint16_t Handle_Serv_Status;

tBleStatus ServiceStatus_Add(void) {
	tBleStatus ret;

	uint8_t char_number = 1;
	uint8_t max_attribute_records = 1 + (3 * char_number);

	Service_UUID_t service_uuid = { .Service_UUID_128 = UUID_SERV_STATUS };
	ret = aci_gatt_add_service(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE,
			max_attribute_records, &Handle_Serv_Status);
	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceStatus_CharacteristicPower_Add();

	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
