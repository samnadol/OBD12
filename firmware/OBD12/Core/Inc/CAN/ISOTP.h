/*
 * ISO_TP.h
 *
 *  Created on: Aug 18, 2025
 *      Author: samna
 */

#ifndef INC_CAN_ISOTP_H_
#define INC_CAN_ISOTP_H_

#include <stdint.h>

void ISOTP_ProcessFrame(uint32_t source, uint8_t *frame);

#endif /* INC_CAN_ISOTP_H_ */
