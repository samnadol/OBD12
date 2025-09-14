#ifndef APP_BLUENRG_2_H
#define APP_BLUENRG_2_H

#include "stm32g4xx_hal.h"

#define STATUS_LED_1_BANK GPIOA
#define STATUS_LED_2_BANK GPIOB
#define STATUS_LED_1_PIN GPIO_PIN_5
#define STATUS_LED_2_PIN GPIO_PIN_0

#define HWCONF_1_BANK GPIOA
#define HWCONF_2_BANK GPIOA
#define HWCONF_1_PIN GPIO_PIN_4
#define HWCONF_2_PIN GPIO_PIN_8

extern uint8_t HWCONF[2];

void MX_BlueNRG_2_Init(void);
void MX_BlueNRG_2_Process(void);

#endif /* APP_BLUENRG_2_H */
