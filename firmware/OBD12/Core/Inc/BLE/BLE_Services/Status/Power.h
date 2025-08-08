/*
 * status.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_STATUS_POWER_H_
#define INC_BLE_BLE_SERVICES_STATUS_POWER_H_

#include <BLE/BLE_Services/_Common.h>

extern uint16_t Handle_Char_Status_Power;
extern __IO uint8_t Subscription_Status_Power;

tBleStatus ServiceStatus_CharacteristicPower_Update();
tBleStatus ServiceStatus_CharacteristicPower_Add();

#endif /* INC_BLE_BLE_SERVICES_STATUS_POWER_H_ */
