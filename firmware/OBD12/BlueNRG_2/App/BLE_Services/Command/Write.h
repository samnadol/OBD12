/*
 * Write.h
 *
 *  Created on: Aug 8, 2025
 *      Author: samna
 */

#ifndef APP_BLE_SERVICES_COMMAND_WRITE_H_
#define APP_BLE_SERVICES_COMMAND_WRITE_H_

#include "BLE_Services/_Common.h"

#define MAX_COMMAND_LENGTH 128

extern uint16_t Handle_Char_Command_Write;

tBleStatus ServiceCommand_CharacteristricWrite_Process(uint16_t connection_handle, uint16_t attribute_handle, uint8_t *data, size_t length);
tBleStatus ServiceCommand_CharacteristicWrite_Add();

#endif /* APP_BLE_SERVICES_COMMAND_WRITE_H_ */
