/*
 * Command.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/Command.h>

#define UUID_SERV_COMMAND { 0x00,0x00,0x00,0x00,0x00,0x02,0x55,0xE1,0x8F,0x00,0x00,0x00,0x00,0xA5,0xC2,0x1B }

uint16_t Handle_Serv_Command;

tBleStatus ServiceCommand_Add(void) {
	tBleStatus ret;

	uint8_t char_number = 1;
	uint8_t max_attribute_records = 1 + (3 * char_number);

	Service_UUID_t service_uuid = { .Service_UUID_128 = UUID_SERV_COMMAND };
	ret = aci_gatt_add_service(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE,
			max_attribute_records, &Handle_Serv_Command);
	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceCommand_CharacteristicWrite_Add();

	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
