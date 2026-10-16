/*
 * power.c
 *
 * The ADC reference is VDDA (the +3V3 rail), so readings are converted to
 * millivolts assuming VDDA = 3300 mV.
 */

#include "App/power.h"

#include "_Debug.h"
#include "main.h"

#define VDDA_MV 3300U
#define ADC_FULL_SCALE 4095U
#define ADC_TIMEOUT_MS 5

extern ADC_HandleTypeDef hadc2;

void power_init(void)
{
	if (HAL_ADCEx_Calibration_Start(&hadc2, ADC_SINGLE_ENDED) != HAL_OK)
		PRINT_DBG("ADC calibration failed\r\n");
}

static uint16_t read_mv(uint32_t channel)
{
	ADC_ChannelConfTypeDef config = { 0 };
	config.Channel = channel;
	config.Rank = ADC_REGULAR_RANK_1;
	config.SamplingTime = ADC_SAMPLETIME_47CYCLES_5; // the regulator outputs are not buffered
	config.SingleDiff = ADC_SINGLE_ENDED;
	config.OffsetNumber = ADC_OFFSET_NONE;
	config.Offset = 0;

	if (HAL_ADC_ConfigChannel(&hadc2, &config) != HAL_OK || HAL_ADC_Start(&hadc2) != HAL_OK)
		return 0;

	uint16_t mv = 0;
	if (HAL_ADC_PollForConversion(&hadc2, ADC_TIMEOUT_MS) == HAL_OK)
		mv = (uint16_t) (HAL_ADC_GetValue(&hadc2) * VDDA_MV / ADC_FULL_SCALE);

	HAL_ADC_Stop(&hadc2);
	return mv;
}

void power_read(proto_power_t *out)
{
	out->usb_regulator_ok = HAL_GPIO_ReadPin(USB_REG_ST_GPIO_Port, USB_REG_ST_Pin) == GPIO_PIN_SET;
	out->can_regulator_ok = HAL_GPIO_ReadPin(CAN_REG_ST_GPIO_Port, CAN_REG_ST_Pin) == GPIO_PIN_SET;
	out->usb_regulator_mv = read_mv(ADC_CHANNEL_3); // PA6, USB_REG_OUT
	out->can_regulator_mv = read_mv(ADC_CHANNEL_4); // PA7, CAN_REG_OUT
}
