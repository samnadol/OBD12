/*
 * Power.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 *
 * Value: proto_power_t (6 bytes, little-endian), see Comms/protocol.h.
 */

#include <BLE/BLE_Services/Status/Power.h>
#include <BLE/BLE_Services/Status.h>

#include "App/power.h"

#define UUID_CHAR_STATUS_POWER { 0x00,0x00,0x00,0x01,0x00,0x01,0x55,0xE1,0x8F,0x00,0x00,0x00,0x00,0xA5,0xC2,0x1B }

uint16_t Handle_Char_Status_Power;
__IO uint8_t Subscription_Status_Power;

tBleStatus ServiceStatus_CharacteristicPower_Update(void) {
	proto_power_t status;
	power_read(&status);

	tBleStatus ret = aci_gatt_update_char_value(Handle_Serv_Status, Handle_Char_Status_Power, 0, sizeof(status), (uint8_t*) &status);
	if (ret != BLE_STATUS_SUCCESS)
		PRINT_DBG("Error while updating status power characteristic: 0x%02x\r\n", ret);

	return ret;
}

tBleStatus ServiceStatus_CharacteristicPower_Add(void) {
	tBleStatus ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_STATUS_POWER };
	ret = aci_gatt_add_char(Handle_Serv_Status, UUID_TYPE_128, &char_uuid, sizeof(proto_power_t),
			CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
			GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_Status_Power);
	if (ret != BLE_STATUS_SUCCESS)
		return ret;

	PRINT_DBG("ServiceStatus_CharacteristicPower_Add() success, handle %02x\r\n", Handle_Char_Status_Power);
	return ServiceStatus_CharacteristicPower_Update();
}
