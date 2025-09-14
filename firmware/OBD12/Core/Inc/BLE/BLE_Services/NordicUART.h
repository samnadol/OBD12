/*
 * Command.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_SERVICES_NORDICUART_H_
#define INC_BLE_BLE_SERVICES_NORDICUART_H_

#include <BLE/BLE_Services/NordicUART/TX.h>
#include <BLE/BLE_Services/NordicUART/RX.h>

extern uint16_t Handle_Serv_NordicUART;

tBleStatus ServiceNordicUART_Add(void);

#endif /* INC_BLE_BLE_SERVICES_NORDICUART_H_ */
