/*
 * system.c
 *
 * The fault record lives in the TAMP backup registers, which survive a system
 * reset (but not a power cycle). BKP0R holds a magic value plus the fault code,
 * BKP1R holds extra information about the fault.
 */

#include "App/system.h"

#include "main.h"

#define FAULT_MAGIC 0xFA170000UL
#define FAULT_MAGIC_MASK 0xFFFF0000UL

static reset_cause_t reset_cause = RESET_CAUSE_UNKNOWN;
static fault_code_t last_fault = FAULT_NONE;
static uint32_t last_fault_info = 0;
static uint8_t hwconf = 0;

static void backup_access_enable(void)
{
	RCC->APB1ENR1 |= RCC_APB1ENR1_PWREN | RCC_APB1ENR1_RTCAPBEN;
	(void) RCC->APB1ENR1;
	PWR->CR1 |= PWR_CR1_DBP;
}

void system_init(void)
{
	uint32_t csr = RCC->CSR;

	backup_access_enable();

	uint32_t bkp0 = TAMP->BKP0R;
	if ((bkp0 & FAULT_MAGIC_MASK) == FAULT_MAGIC)
	{
		last_fault = (fault_code_t) (bkp0 & 0xFFFF);
		last_fault_info = TAMP->BKP1R;
	}
	TAMP->BKP0R = 0;
	TAMP->BKP1R = 0;

	// a watchdog or software reset also asserts NRST, so PINRSTF is checked last
	if (csr & RCC_CSR_IWDGRSTF)
		reset_cause = RESET_CAUSE_IWDG;
	else if (csr & RCC_CSR_WWDGRSTF)
		reset_cause = RESET_CAUSE_WWDG;
	else if (csr & RCC_CSR_SFTRSTF)
		reset_cause = (last_fault != FAULT_NONE) ? RESET_CAUSE_FATAL : RESET_CAUSE_SOFTWARE;
	else if (csr & RCC_CSR_LPWRRSTF)
		reset_cause = RESET_CAUSE_LOW_POWER;
	else if (csr & RCC_CSR_BORRSTF)
		reset_cause = RESET_CAUSE_POWER_ON;
	else if (csr & RCC_CSR_PINRSTF)
		reset_cause = RESET_CAUSE_PIN;

	RCC->CSR |= RCC_CSR_RMVF;

	hwconf = (HAL_GPIO_ReadPin(HWCONF0_GPIO_Port, HWCONF0_Pin) == GPIO_PIN_SET ? 0x01 : 0x00)
			| (HAL_GPIO_ReadPin(HWCONF1_GPIO_Port, HWCONF1_Pin) == GPIO_PIN_SET ? 0x02 : 0x00);
}

reset_cause_t system_reset_cause(void)
{
	return reset_cause;
}

fault_code_t system_last_fault(void)
{
	return last_fault;
}

uint32_t system_last_fault_info(void)
{
	return last_fault_info;
}

void system_fatal(fault_code_t code, uint32_t info)
{
	__disable_irq();

	backup_access_enable();
	TAMP->BKP0R = FAULT_MAGIC | (uint32_t) code;
	TAMP->BKP1R = info;

	NVIC_SystemReset();
}

uint8_t system_hwconf(void)
{
	return hwconf;
}

void system_uid(uint32_t uid[3])
{
	uid[0] = HAL_GetUIDw0();
	uid[1] = HAL_GetUIDw1();
	uid[2] = HAL_GetUIDw2();
}

uint32_t system_ble_passkey(void)
{
	uint32_t uid[3];
	system_uid(uid);

	// FNV-1a over the 12 UID bytes
	uint32_t hash = 2166136261UL;
	const uint8_t *bytes = (const uint8_t*) uid;
	for (int i = 0; i < 12; i++)
	{
		hash ^= bytes[i];
		hash *= 16777619UL;
	}

	return hash % 1000000UL;
}
