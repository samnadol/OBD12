/*
 * status.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef APP_BLE_SERVICES_STATUS_POWER_H_
#define APP_BLE_SERVICES_STATUS_POWER_H_

#include "../_Common.h"

extern uint16_t Handle_Char_Status_Power;
extern __IO uint8_t Subscription_Status_Power;

tBleStatus ServiceStatus_CharacteristicPower_Update();
tBleStatus ServiceStatus_CharacteristicPower_Add();

#endif /* APP_BLE_SERVICES_STATUS_POWER_H_ */
