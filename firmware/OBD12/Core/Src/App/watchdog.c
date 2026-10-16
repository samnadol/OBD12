/*
 * watchdog.c
 *
 * Driven through registers directly; the HAL IWDG driver is not part of this
 * CubeMX project. The timeout must stay above the longest blocking HCI command
 * (HCI_DEFAULT_TIMEOUT_MS = 1000).
 */

#include "App/watchdog.h"

#include "stm32g4xx.h"

#define IWDG_KEY_ENABLE  0xCCCCU
#define IWDG_KEY_ACCESS  0x5555U
#define IWDG_KEY_RELOAD  0xAAAAU

#define IWDG_PRESCALER_64 4U    // 32 kHz LSI / 64 = 500 Hz
#define IWDG_RELOAD 1500U       // 1500 / 500 Hz = 3 s

void wdg_init(void)
{
	DBGMCU->APB1FZR1 |= DBGMCU_APB1FZR1_DBG_IWDG_STOP;

	IWDG->KR = IWDG_KEY_ENABLE;
	IWDG->KR = IWDG_KEY_ACCESS;
	IWDG->PR = IWDG_PRESCALER_64;
	IWDG->RLR = IWDG_RELOAD;
	while (IWDG->SR)
		;
	IWDG->KR = IWDG_KEY_RELOAD;
}

void wdg_kick(void)
{
	IWDG->KR = IWDG_KEY_RELOAD;
}
