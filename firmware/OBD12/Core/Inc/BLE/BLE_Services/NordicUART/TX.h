/*
 * TX.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_NORDICUART_TX_H_
#define INC_BLE_BLE_SERVICES_NORDICUART_TX_H_

#include <BLE/BLE_Services/_Common.h>

extern uint16_t Handle_Char_NordicUART_TX;

// sends data as a notification; returns the raw BlueNRG status
// (BLE_STATUS_INSUFFICIENT_RESOURCES means "try again later")
tBleStatus ServiceNordicUART_CharacteristicTX_Update(const uint8_t *data, uint16_t data_len);
tBleStatus ServiceNordicUART_CharacteristicTX_Add(void);

#endif /* INC_BLE_BLE_SERVICES_NORDICUART_TX_H_ */
