/*
 * status.c
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#include <BLE/BLE_Services/Status/Power.h>
#include <BLE/BLE_Services/Status.h>

#define UUID_CHAR_STATUS_POWER { 0x00,0x00,0x00,0x01,0x00,0x01,0x55,0xE1,0x8F,0x00,0x00,0x00,0x00,0xA5,0xC2,0x1B }

enum {
	USB_REG = GPIO_PIN_6, CAN_REG = GPIO_PIN_7,
};
typedef struct
{
	uint8_t USB_ST;
	uint8_t CAN_ST;

	uint16_t USB_OUT;
	uint16_t CAN_OUT;
} POWER_STATUS;
extern ADC_HandleTypeDef hadc2;

uint16_t Handle_Char_Status_Power;
__IO uint8_t Subscription_Status_Power;

uint16_t Read_Regulator_Out(uint16_t Regulator) {
	if (Regulator != USB_REG && Regulator != CAN_REG)
		return 0;

	ADC_ChannelConfTypeDef ADC_ChannelConf = { 0 };
	ADC_ChannelConf.Rank = ADC_REGULAR_RANK_1;
	ADC_ChannelConf.SamplingTime = ADC_SAMPLETIME_2CYCLE_5;
	ADC_ChannelConf.SingleDiff = ADC_SINGLE_ENDED;
	ADC_ChannelConf.OffsetNumber = ADC_OFFSET_NONE;
	ADC_ChannelConf.Offset = 0;
	ADC_ChannelConf.Channel =
			Regulator == USB_REG ? ADC_CHANNEL_3 : ADC_CHANNEL_4;

	HAL_ADC_ConfigChannel(&hadc2, &ADC_ChannelConf);
	HAL_ADC_Start(&hadc2);
	HAL_ADC_PollForConversion(&hadc2, 2);

	uint16_t ADC_Res = HAL_ADC_GetValue(&hadc2);
	return ADC_Res;
}

uint8_t Read_Regulator_Status(uint16_t Regulator) {
	return HAL_GPIO_ReadPin(GPIOB, Regulator);
}

tBleStatus ServiceStatus_CharacteristicPower_Update() {
	tBleStatus ret;

	POWER_STATUS status;
	status.USB_ST = Read_Regulator_Status(USB_REG);
	status.CAN_ST = Read_Regulator_Status(CAN_REG);
	status.USB_OUT = Read_Regulator_Out(USB_REG);
	status.CAN_OUT = Read_Regulator_Out(CAN_REG);
	printf("POWER DATA: USB %d %.2f, CAN %d %.2f\r\n", status.USB_ST, status.USB_OUT * 3.3 / 4096, status.CAN_ST, status.CAN_OUT * 3.3 / 4096);

	ret = aci_gatt_update_char_value(Handle_Serv_Status, Handle_Char_Status_Power, 0, sizeof(POWER_STATUS), (uint8_t *) &status);

	if (ret != BLE_STATUS_SUCCESS) {
		PRINT_DBG("Error while updating status power characteristic: 0x%04X\r\n", ret);
		return BLE_STATUS_ERROR;
	}

	return BLE_STATUS_SUCCESS;
}

tBleStatus ServiceStatus_CharacteristicPower_Add() {
	uint8_t ret;

	Char_UUID_t char_uuid = { .Char_UUID_128 = UUID_CHAR_STATUS_POWER };
	ret = aci_gatt_add_char(Handle_Serv_Status, UUID_TYPE_128, &char_uuid, sizeof(POWER_STATUS),
	CHAR_PROP_READ | CHAR_PROP_NOTIFY, ATTR_PERMISSION_NONE,
	GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP, 16, 0,
			&Handle_Char_Status_Power);
	if (ret != BLE_STATUS_SUCCESS)
		goto fail;

	ServiceStatus_CharacteristicPower_Update();

	PRINT_DBG("ServiceStatus_CharacteristicPower_Add() success, handle %02x\r\n", Handle_Char_Status_Power);
	return BLE_STATUS_SUCCESS;

	fail: return BLE_STATUS_ERROR;
}
