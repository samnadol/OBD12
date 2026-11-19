/*
 * power.c
 *
 * The regulator outputs are read as digital inputs: both regulators make
 * 3.3 V, the same as the +3V3 rail (and VDDA) they feed, so they can only be
 * told apart as up or down.
 */

#include "App/power.h"

#include "main.h"

static uint8_t pin_high(GPIO_TypeDef *port, uint16_t pin)
{
	return HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET;
}

void power_read(proto_power_t *out)
{
	out->usb_reg_st = pin_high(USB_REG_ST_GPIO_Port, USB_REG_ST_Pin);
	out->can_reg_st = pin_high(CAN_REG_ST_GPIO_Port, CAN_REG_ST_Pin);
	out->usb_reg_out = pin_high(USB_REG_OUT_GPIO_Port, USB_REG_OUT_Pin);
	out->can_reg_out = pin_high(CAN_REG_OUT_GPIO_Port, CAN_REG_OUT_Pin);
}
