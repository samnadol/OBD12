/*
 * DeviceInformation.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef APP_BLE_SERVICES_DEVICEINFORMATION_H_
#define APP_BLE_SERVICES_DEVICEINFORMATION_H_

#include "BLE_Services/DeviceInformation/ManufacturerName.h"
#include "BLE_Services/DeviceInformation/ModelNumber.h"

extern uint16_t Handle_Serv_DeviceInformation;

tBleStatus ServiceDeviceInformation_Add(void);

#endif /* APP_BLE_SERVICES_DEVICEINFORMATION_H_ */
