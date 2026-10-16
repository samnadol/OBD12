/*
 * App.h
 *
 * BlueNRG-2 application: device init, advertising, connection and security
 * management. Everything runs in main-loop context.
 */

#ifndef APP_BLUENRG_2_H
#define APP_BLUENRG_2_H

#include <stdbool.h>
#include <stdint.h>

#include "bluenrg1_types.h"
#include "ble_status.h"

// set to 1 to request pairing as soon as a central connects; with 0, pairing
// is only requested when a command needs an authorized link
#define BLE_PAIR_ON_CONNECT 0

// initializes the module; on failure BLE stays unavailable and the rest of the
// firmware keeps running
void MX_BlueNRG_2_Init(void);
void MX_BlueNRG_2_Process(void);

bool BLE_Available(void);
bool BLE_Connected(void);

// asks the connected central to pair / encrypt the link
void BLE_RequestSecurity(void);

tBleStatus BLE_ClearBonds(void);

// called from BLE_Events.c
void BLE_OnConnected(uint16_t connection_handle);
void BLE_OnDisconnected(uint8_t reason);
uint16_t BLE_ConnectionHandle(void);

#endif /* APP_BLUENRG_2_H */
