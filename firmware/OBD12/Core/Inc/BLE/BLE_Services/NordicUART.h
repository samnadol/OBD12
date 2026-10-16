/*
 * NordicUART.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 *
 * Nordic UART Service (NUS): the host writes protocol bytes to RX and receives
 * protocol bytes as TX notifications. See Comms/transport_ble.c.
 */

#ifndef INC_BLE_BLE_SERVICES_NORDICUART_H_
#define INC_BLE_BLE_SERVICES_NORDICUART_H_

#include <BLE/BLE_Services/NordicUART/TX.h>
#include <BLE/BLE_Services/NordicUART/RX.h>

extern uint16_t Handle_Serv_NordicUART;

tBleStatus ServiceNordicUART_Add(void);

#endif /* INC_BLE_BLE_SERVICES_NORDICUART_H_ */
