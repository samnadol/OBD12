/*
 * power.h
 *
 * Supply status: the LM66100 status pins of the USB and CAN (vehicle) supply
 * paths, and whether each regulator's output is up.
 */

#ifndef INC_APP_POWER_H_
#define INC_APP_POWER_H_

#include "Comms/protocol.h"

void power_read(proto_power_t *out);

#endif /* INC_APP_POWER_H_ */
