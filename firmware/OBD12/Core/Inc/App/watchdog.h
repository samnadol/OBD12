/*
 * watchdog.h
 *
 * Independent watchdog (LSI, ~3 s timeout). Frozen while the core is halted
 * by a debugger.
 */

#ifndef INC_APP_WATCHDOG_H_
#define INC_APP_WATCHDOG_H_

void wdg_init(void);
void wdg_kick(void);

#endif /* INC_APP_WATCHDOG_H_ */
