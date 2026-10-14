/*
 * sched.h
 *
 * Tick helpers for the main loop. All timing is based on HAL_GetTick() (1 ms);
 * unsigned subtraction makes the comparisons safe across tick wrap-around.
 */

#ifndef INC_APP_SCHED_H_
#define INC_APP_SCHED_H_

#include <stdbool.h>
#include <stdint.h>

#include "stm32g4xx_hal.h"

// true at most once per period; *last holds the time of the previous firing
static inline bool every_ms(uint32_t *last, uint32_t period_ms)
{
	uint32_t now = HAL_GetTick();
	if (now - *last < period_ms)
		return false;

	*last = now;
	return true;
}

static inline bool elapsed_ms(uint32_t since, uint32_t duration_ms)
{
	return HAL_GetTick() - since >= duration_ms;
}

#endif /* INC_APP_SCHED_H_ */
