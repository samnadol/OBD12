/*
 * transport_usb.h
 *
 * Host protocol over USART1 (FT230X USB-UART bridge), interrupt driven.
 */

#ifndef INC_COMMS_TRANSPORT_USB_H_
#define INC_COMMS_TRANSPORT_USB_H_

#include "stm32g4xx_hal.h"

void transport_usb_init(UART_HandleTypeDef *huart);

#endif /* INC_COMMS_TRANSPORT_USB_H_ */
