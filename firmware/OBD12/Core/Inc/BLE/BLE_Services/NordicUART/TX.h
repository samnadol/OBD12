/*
 * Write.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_NORDICUART_TX_H_
#define INC_BLE_BLE_SERVICES_NORDICUART_TX_H_

#include <BLE/BLE_Services/_Common.h>

extern uint16_t Handle_Char_NordicUART_TX;
extern __IO uint8_t Subscription_NordicUART_TX;

tBleStatus ServiceNordicUART_CharacteristicTX_Update(uint8_t *data, size_t data_len);
tBleStatus ServiceNordicUART_CharacteristicTX_Add();

#endif /* INC_BLE_BLE_SERVICES_NORDICUART_TX_H_ */
