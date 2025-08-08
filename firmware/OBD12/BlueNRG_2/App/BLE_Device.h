/*
 * Init.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef APP_BLE_DEVICE_H_
#define APP_BLE_DEVICE_H_

#include <stdint.h>

#define ADVERTISE_NAME 'O','B','D','1','2'
#define ADVERTISE_NAME_LEN 5
#define BDADDR_SIZE        6

#define BLE_SECURE_PAIRING 0
#define BLE_CONNECTION_PASSKEY 123456

extern uint8_t bdaddr[BDADDR_SIZE];

uint8_t BLE_Device_Init(void);
void BLE_Device_SetDiscoverable(void);

#endif /* APP_BLE_DEVICE_H_ */
