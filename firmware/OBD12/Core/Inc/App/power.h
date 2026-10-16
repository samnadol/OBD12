/*
 * power.h
 *
 * Supply status: the USB and CAN (vehicle) regulator "power good" pins and
 * their output voltages, measured by ADC2.
 */

#ifndef INC_APP_POWER_H_
#define INC_APP_POWER_H_

#include "Comms/protocol.h"

void power_init(void);
void power_read(proto_power_t *out);

#endif /* INC_APP_POWER_H_ */
