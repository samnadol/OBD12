/*
 * system.h
 *
 * Reset cause, fatal error recording, and per-device identity (UID,
 * hardware configuration strap, BLE passkey).
 */

#ifndef INC_APP_SYSTEM_H_
#define INC_APP_SYSTEM_H_

#include <stdint.h>

#define FW_VERSION_MAJOR 0
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0

typedef enum
{
	RESET_CAUSE_UNKNOWN = 0,
	RESET_CAUSE_POWER_ON = 1,
	RESET_CAUSE_PIN = 2,
	RESET_CAUSE_SOFTWARE = 3,
	RESET_CAUSE_IWDG = 4,
	RESET_CAUSE_WWDG = 5,
	RESET_CAUSE_LOW_POWER = 6,
	RESET_CAUSE_FATAL = 7,      // software reset requested by system_fatal()
} reset_cause_t;

typedef enum
{
	FAULT_NONE = 0,
	FAULT_ERROR_HANDLER = 1,
	FAULT_HARDFAULT = 2,
	FAULT_MEMMANAGE = 3,
	FAULT_BUSFAULT = 4,
	FAULT_USAGEFAULT = 5,
	FAULT_NMI = 6,
} fault_code_t;

// call once, early (after the clock is configured)
void system_init(void);

reset_cause_t system_reset_cause(void);

// fault recorded before the last reset (FAULT_NONE if none), plus extra info
// (e.g. SCB->CFSR for CPU faults)
fault_code_t system_last_fault(void);
uint32_t system_last_fault_info(void);

// record the fault in a backup register and reset the MCU
void system_fatal(fault_code_t code, uint32_t info) __attribute__((noreturn));

// HWCONF0/HWCONF1 straps, bit 0 = HWCONF0
uint8_t system_hwconf(void);

void system_uid(uint32_t uid[3]);

// 6-digit BLE pairing passkey, derived from the UID so it can be printed on a label
uint32_t system_ble_passkey(void);

#endif /* INC_APP_SYSTEM_H_ */
