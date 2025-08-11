/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

#include "hci_tl_interface.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define SPI1_IRQ_Pin GPIO_PIN_0
#define SPI1_IRQ_GPIO_Port GPIOA
#define SPI1_IRQ_EXTI_IRQn EXTI0_IRQn
#define BLE_NRST_Pin GPIO_PIN_1
#define BLE_NRST_GPIO_Port GPIOA
#define FDCAN_FAULT_Pin GPIO_PIN_2
#define FDCAN_FAULT_GPIO_Port GPIOA
#define FDCAN_FAULT_EXTI_IRQn EXTI2_IRQn
#define FDCAN_SILENT_Pin GPIO_PIN_3
#define FDCAN_SILENT_GPIO_Port GPIOA
#define HWCONF0_Pin GPIO_PIN_4
#define HWCONF0_GPIO_Port GPIOA
#define STATUS_LED_1_Pin GPIO_PIN_5
#define STATUS_LED_1_GPIO_Port GPIOA
#define USB_REG_OUT_Pin GPIO_PIN_6
#define USB_REG_OUT_GPIO_Port GPIOA
#define CAN_REG_OUT_Pin GPIO_PIN_7
#define CAN_REG_OUT_GPIO_Port GPIOA
#define STATUS_LED_2_Pin GPIO_PIN_0
#define STATUS_LED_2_GPIO_Port GPIOB
#define HWCONF1_Pin GPIO_PIN_8
#define HWCONF1_GPIO_Port GPIOA
#define SPI1_CS_Pin GPIO_PIN_15
#define SPI1_CS_GPIO_Port GPIOA
#define USB_REG_ST_Pin GPIO_PIN_6
#define USB_REG_ST_GPIO_Port GPIOB
#define CAN_REG_ST_Pin GPIO_PIN_7
#define CAN_REG_ST_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
