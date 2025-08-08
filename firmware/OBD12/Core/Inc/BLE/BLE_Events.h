#ifndef GATT_DB_H
#define GATT_DB_H

#include "hci.h"

#define NUMBER_OF_APPLICATION_SERVICES 1
enum {
	STATUS_SERVICE_INDEX = 0,
};

enum
{
	SUBSCRIPTION_NO = 0,
	SUBSCRIPTION_YES = 1,
};

typedef enum
{
	EVENT_WRITE = 1,
	EVENT_SUBSCRIBE_CHANGE = 2,
} EVENT_HANDLE_OFFSET;

void Read_Request_CB(uint16_t handle);
void Attribute_Modified_Request_CB(uint16_t Connection_Handle, uint16_t attr_handle, uint16_t Offset, uint8_t data_length, uint8_t *att_data);
void BLE_WriteRequest(uint16_t connection_handle, uint16_t attr_handle, uint8_t data_length, uint8_t *data);

void BLE_ProcessUserEvent(void *pData);

#endif /* GATT_DB_H */
