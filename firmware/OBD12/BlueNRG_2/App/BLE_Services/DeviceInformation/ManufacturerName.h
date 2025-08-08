/*
 * ManufacturerName.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef APP_BLE_SERVICES_DEVICEINFORMATION_MANUFACTURERNAME_H_
#define APP_BLE_SERVICES_DEVICEINFORMATION_MANUFACTURERNAME_H_

#include "BLE_Services/_Common.h"

extern uint16_t Handle_Char_DeviceInformation_ManufacturerName;

tBleStatus ServiceDeviceInformation_CharacteristicManufacturerName_Update();
tBleStatus ServiceDeviceInformation_CharacteristicManufacturerName_Add();

#endif /* APP_BLE_SERVICES_DEVICEINFORMATION_MANUFACTURERNAME_H_ */
