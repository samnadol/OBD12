/*
 * BLE_Events.h
 *
 * Dispatch of BlueNRG HCI/ACI events to the application.
 */

#ifndef INC_BLE_BLE_EVENTS_H_
#define INC_BLE_BLE_EVENTS_H_

#include "hci.h"

typedef enum
{
	EVENT_WRITE = 1,            // characteristic value handle = char handle + 1
	EVENT_SUBSCRIBE_CHANGE = 2, // CCCD handle = char handle + 2
} EVENT_HANDLE_OFFSET;

// registered with hci_init(); called from hci_user_evt_proc()
void BLE_ProcessUserEvent(void *pData);

#endif /* INC_BLE_BLE_EVENTS_H_ */
