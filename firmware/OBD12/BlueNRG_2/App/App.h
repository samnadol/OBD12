#ifndef APP_BLUENRG_2_H
#define APP_BLUENRG_2_H

#include "stm32g4xx_hal.h"

static GPIO_TypeDef *STATUS_LED_BANK[2] = { GPIOA, GPIOB };
static uint16_t STATUS_LED_PIN[2] = { GPIO_PIN_5, GPIO_PIN_0 };

void MX_BlueNRG_2_Init(void);
void MX_BlueNRG_2_Process(void);

#endif /* APP_BLUENRG_2_H */
