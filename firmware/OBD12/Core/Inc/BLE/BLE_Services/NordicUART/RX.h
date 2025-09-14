/*
 * Write.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_NORDICUART_RX_H_
#define INC_BLE_BLE_SERVICES_NORDICUART_RX_H_

#include <BLE/BLE_Services/_Common.h>

#define MAX_COMMAND_LENGTH 128

extern uint16_t Handle_Char_NordicUART_RX;

tBleStatus ServiceNordicUART_CharacteristicRX_Process(uint16_t connection_handle, uint16_t attribute_handle, uint8_t *data, size_t length);
tBleStatus ServiceNordicUART_CharacteristicRX_Add();

#endif /* INC_BLE_BLE_SERVICES_NORDICUART_RX_H_ */
