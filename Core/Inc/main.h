/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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
#include "stm32f1xx_hal.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

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
/* Flash layout is managed by flash_store.c (external MX25L6473E via SPI) */


#define LED_BUILTIN_Pin GPIO_PIN_13
#define LED_BUILTIN_GPIO_Port GPIOC
#define MODE_PIN_Pin GPIO_PIN_12
#define MODE_PIN_GPIO_Port GPIOB
#define LED_STATUS_Pin GPIO_PIN_13
#define LED_STATUS_GPIO_Port GPIOB
#define FLASH_CS_Pin        GPIO_PIN_4
#define FLASH_CS_GPIO_Port  GPIOA

#define PROTO_START_BYTE            0xAAU
#define PROTO_CMD_REPORT            0x01U
#define PROTO_CMD_START             0x02U
#define PROTO_CMD_STOP              0x03U
#define PROTO_CMD_PING              0x04U
#define PROTO_CMD_ERASE             0x05U
#define PROTO_ACK                   0x06U
#define PROTO_NAK                   0x07U

#define RECORDING_MAGIC             0xCAFEF103UL

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

/* Absolute position entry stored in flash.
 * X and Y are in HID coordinate space (0-32767), independent of screen resolution.
 * The host OS maps this range to the physical screen automatically. */
typedef struct __attribute__((packed)) {
    uint8_t  buttons;    /* HID button mask (bits 0-2)         */
    uint8_t  reserved;   /* Must be 0x00                       */
    uint16_t x;          /* Absolute X, 0-32767                */
    uint16_t y;          /* Absolute Y, 0-32767                */
    uint16_t delay_ms;   /* Delay BEFORE sending this report   */
    uint16_t repeat;     /* Send count (1 = once)              */
} FlashEntry_t;          /* 10 bytes, always even for STM32F1 flash */

/* Flash header stored at FLASH_HEADER_ADDR */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t entry_count;
    uint32_t total_duration_ms;
    uint32_t reserved;
} FlashHeader_t;

/* 8-byte payload inside a PROTO_CMD_REPORT frame */
typedef struct __attribute__((packed)) {
    uint8_t  buttons;    /* HID button mask                    */
    uint8_t  reserved;   /* 0x00                               */
    uint16_t x;          /* Absolute X, 0-32767                */
    uint16_t y;          /* Absolute Y, 0-32767                */
    uint16_t delay_ms;   /* Delay before this report           */
} ProtoReportPayload_t;

typedef enum {
    APP_MODE_PLAYBACK = 0,
    APP_MODE_RECORD   = 1
} AppMode_t;

typedef enum {
    REC_STATE_IDLE      = 0,
    REC_STATE_WAITING   = 1,
    REC_STATE_RECORDING = 2,
    REC_STATE_FULL      = 3,
    REC_STATE_DONE      = 4
} RecordState_t;

void Error_Handler(void);

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */