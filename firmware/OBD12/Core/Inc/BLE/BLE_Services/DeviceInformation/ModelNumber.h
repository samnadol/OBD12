/*
 * ModelNumber.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_DEVICEINFORMATION_MODELNUMBER_H_
#define INC_BLE_BLE_SERVICES_DEVICEINFORMATION_MODELNUMBER_H_

#include <BLE/BLE_Services/_Common.h>

extern uint16_t Handle_Char_DeviceInformation_ModelNumber;

tBleStatus ServiceDeviceInformation_CharacteristicModelNumber_Update();
tBleStatus ServiceDeviceInformation_CharacteristicModelNumber_Add();

#endif /* INC_BLE_BLE_SERVICES_DEVICEINFORMATION_MODELNUMBER_H_ */
