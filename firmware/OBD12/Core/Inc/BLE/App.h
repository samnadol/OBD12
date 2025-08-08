#ifndef APP_BLUENRG_2_H
#define APP_BLUENRG_2_H

#include "stm32g4xx_hal.h"

extern GPIO_TypeDef *STATUS_LED_BANK[2];
extern uint16_t STATUS_LED_PIN[2];
extern uint8_t HWCONF[2];

void MX_BlueNRG_2_Init(void);
void MX_BlueNRG_2_Process(void);

#endif /* APP_BLUENRG_2_H */
