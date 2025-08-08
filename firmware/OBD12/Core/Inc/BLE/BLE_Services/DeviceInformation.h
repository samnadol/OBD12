/*
 * DeviceInformation.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_DEVICEINFORMATION_H_
#define INC_BLE_BLE_SERVICES_DEVICEINFORMATION_H_

#include <BLE/BLE_Services/DeviceInformation/ManufacturerName.h>
#include <BLE/BLE_Services/DeviceInformation/ModelNumber.h>

extern uint16_t Handle_Serv_DeviceInformation;

tBleStatus ServiceDeviceInformation_Add(void);

#endif /* INC_BLE_BLE_SERVICES_DEVICEINFORMATION_H_ */
