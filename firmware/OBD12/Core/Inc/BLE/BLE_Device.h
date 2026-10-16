/*
 * BLE_Device.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef INC_BLE_BLE_DEVICE_H_
#define INC_BLE_BLE_DEVICE_H_

#include <stdint.h>

#define ADVERTISE_NAME 'O','B','D','1','2'
#define ADVERTISE_NAME_LEN 5
#define BDADDR_SIZE        6

// placeholder company ID in the manufacturer-specific advertising data
#define ADVERTISE_COMPANY_ID 0x1234

// static random address, least significant byte first
extern uint8_t bdaddr[BDADDR_SIZE];

uint8_t BLE_Device_Init(void);
void BLE_Device_SetDiscoverable(void);

#endif /* INC_BLE_BLE_DEVICE_H_ */
