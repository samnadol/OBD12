/*
 * status.h
 *
 * Once per second: power status to subscribers (BLE Status/Power
 * characteristic, EVT_POWER), status LEDs.
 *
 *   STATUS_LED_1: on while idle, blinks while a BLE central is connected
 *   STATUS_LED_2: on while the CAN bus is healthy and ECUs answered recently
 */

#ifndef INC_APP_STATUS_H_
#define INC_APP_STATUS_H_

void status_init(void);
void status_process(void);

#endif /* INC_APP_STATUS_H_ */
