/*
 * Command.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/NordicUART.h>

#define UUID_SERV_NORDICUART { \
    0x9E, 0xCA, 0xDC, 0x24, \
    0x0E, 0xE5, 0xA9, 0xE0, \
    0x93, 0xF3, 0xA3, 0xB5, \
    0x01, 0x00, 0x40, 0x6E  \
}

uint16_t Handle_Serv_NordicUART;

tBleStatus ServiceNordicUART_Add(void) {
	tBleStatus ret;

	uint8_t char_number = 2;
	uint8_t max_attribute_records = 1 + (3 * char_number);

	Service_UUID_t service_uuid = { .Service_UUID_128 = UUID_SERV_NORDICUART };
	ret = aci_gatt_add_service(UUID_TYPE_128, &service_uuid, PRIMARY_SERVICE,
			max_attribute_records, &Handle_Serv_NordicUART);
	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	PRINT_DBG("ServiceNordicUART_Add() success, handle %02x\r\n", Handle_Serv_NordicUART);

	ServiceNordicUART_CharacteristicRX_Add();
	ServiceNordicUART_CharacteristicTX_Add();

	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
