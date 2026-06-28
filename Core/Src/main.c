/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"
#include "spi.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "flash_store.h"
#include "proto_rx.h"
#include "usb_hid.h"
#include "usbd_cdc_if.h"
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* CDC mode: no UART needed for recording — USB CDC replaces UART adapter */

/* Erase request flag — set in IRQ, acted on in main loop */
static volatile bool erase_requested = false;

/* RLE compressor state */
static FlashEntry_t rle_pending;
static bool         rle_has_pending  = false;
static uint32_t     rle_delay_accum  = 0;   /* accumulated delay_ms across merged repeats */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */
static void Run_RecordMode(void);
static void Run_PlaybackMode(void);
static void Process_Command(void);
static void RLE_Feed(uint8_t buttons, uint16_t x, uint16_t y, uint16_t delay_ms);
static void RLE_Flush(void);
static void LED_Set(GPIO_TypeDef *port, uint16_t pin, bool on);
static void Blink_Error(void);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ============================================================
 *  UART RX COMPLETE CALLBACK
 *  Called from IRQ for each received byte.
 * ============================================================ */
/* Process one complete protocol frame — called from main loop (thread context)
 * so that CDC_Transmit_FS (ACK/NAK) is safe to call. */
static void Process_Command(void)
{
    uint8_t  cmd     = Proto_GetLastCmd();
    uint8_t *payload = Proto_GetLastPayload();

    switch (cmd) {

        case PROTO_CMD_PING:
            Proto_SendAck();
            break;

        case PROTO_CMD_START:
            rle_has_pending = false;
            rle_delay_accum = 0;
            Proto_SendAck();
            break;

        case PROTO_CMD_STOP:
            RLE_Flush();
            Flash_WriteHeader(Flash_CurrentEntryCount(), HAL_GetTick());
            Proto_SendAck();
            break;

        case PROTO_CMD_ERASE:
            /* Never erase inside an IRQ — it would block for ~2 minutes.
             * Set a flag and ACK immediately; main loop does the real work. */
            erase_requested = true;
            Proto_SendAck();
            break;

        case PROTO_CMD_REPORT: {
            ProtoReportPayload_t *rpt = (ProtoReportPayload_t *)payload;

            if (Flash_RemainingEntries() == 0) {
                Proto_SendNak();
                break;
            }

            /* Record only — do NOT call HID_SendReport here.
             * USB is enumerated as CDC in record mode, not HID.
             * Calling HID_SendReport would corrupt the USB state. */
            RLE_Feed(rpt->buttons, rpt->x, rpt->y, rpt->delay_ms);
            Proto_SendAck();
            break;
        }

        default:
            Proto_SendNak();
            break;
    }
}

/* ============================================================
 *  RLE COMPRESSOR
 * ============================================================ */
static void RLE_Feed(uint8_t buttons, uint16_t x, uint16_t y,
                     uint16_t delay_ms)
{
    if (rle_has_pending) {
        /* With absolute coordinates, consecutive identical positions
         * (cursor not moving, same button state) can be RLE-merged.
         * delay_ms is averaged across merged repeats. */
        bool same = (rle_pending.buttons == buttons &&
                     rle_pending.x       == x       &&
                     rle_pending.y       == y        &&
                     rle_pending.repeat  < 0xFFFF);
        if (same) {
            rle_pending.repeat++;
            rle_delay_accum += delay_ms;
            rle_pending.delay_ms = (uint16_t)(rle_delay_accum / rle_pending.repeat);
            return;
        }
        Flash_AppendEntry(&rle_pending);
    }

    rle_pending.buttons  = buttons;
    rle_pending.x        = x;
    rle_pending.y        = y;
    rle_pending.reserved = 0;
    rle_pending.delay_ms = delay_ms;
    rle_pending.repeat   = 1;
    rle_delay_accum      = delay_ms;
    rle_has_pending      = true;
}

static void RLE_Flush(void)
{
    if (rle_has_pending) {
        Flash_AppendEntry(&rle_pending);
        rle_has_pending = false;
    }
}

/* ============================================================
 *  LED HELPERS
 * ============================================================ */
static void LED_Set(GPIO_TypeDef *port, uint16_t pin, bool on)
{
    /* LED_BUILTIN (PC13) is active LOW on Blue Pill */
    if (port == LED_BUILTIN_GPIO_Port && pin == LED_BUILTIN_Pin)
        HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
    else
        HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void Blink_Error(void)
{
    while (1) {
        LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, true);
        HAL_Delay(100);
        LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, false);
        HAL_Delay(100);
    }
}

/* ============================================================
 *  RECORD MODE
 * ============================================================ */
static void Run_RecordMode(void)
{
    uint32_t last_blink = 0;
    bool     led_on     = false;

    /* CDC replaces UART — no serial init needed.
     * Proto_Init is called without a UART handle since
     * CDC_CommandReceived_Callback feeds frames directly. */
    Proto_Init(NULL);

    while (1) {
        uint32_t now = HAL_GetTick();

        /* Process any complete CDC frame — safe here (thread context) */
        if (cdc_cmd_pending) {
            cdc_cmd_pending = false;
            Process_Command();
        }

        /* Handle deferred erase request from CMD_ERASE */
        if (erase_requested) {
            erase_requested = false;
            /* Solid LED during erase (~2 min), then resume blinking */
            LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, true);
            Flash_EraseRecording();   /* result ignored — recorder will
                                         detect failure via NAK on next cmd */
            LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, false);
            last_blink = HAL_GetTick();
        }

        if (now - last_blink >= 500) {
            last_blink = now;
            led_on = !led_on;
            LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, led_on);
        }
    }
}

/* ============================================================
 *  PLAYBACK MODE
 * ============================================================ */
static void Run_PlaybackMode(void)
{
    FlashHeader_t header;

    if (!Flash_ReadHeader(&header) || header.entry_count == 0)
        Blink_Error();

    /* Solid LED = playing back */
    LED_Set(LED_STATUS_GPIO_Port, LED_STATUS_Pin, true);

    HAL_Delay(1000);  /* Wait for USB enumeration */

    uint32_t idx = 0;

    while (1) {
        FlashEntry_t entry;

        if (!Flash_ReadEntry(idx, &entry)) {
            idx = 0;
            continue;
        }

        /* Each repeat represents one original report period.
         * delay_ms is the average interval between reports for this
         * motion segment, so we wait BEFORE every send — not just once
         * before the first. This matches the cadence the OS sees on
         * playback to what the user recorded, preventing cursor drift. */
        for (uint16_t r = 0; r < entry.repeat; r++) {
            uint32_t interval = (entry.delay_ms > 0) ? entry.delay_ms : 8u;
            uint32_t remaining = interval;
            while (remaining > 0) {
                uint32_t chunk = (remaining > 10u) ? 10u : remaining;
                HAL_Delay(chunk);
                remaining -= chunk;
            }
            if (HID_IsReady())
                HID_SendReport(entry.buttons, entry.x, entry.y);
        }

        idx++;
        if (idx >= header.entry_count) {
            idx = 0;
            /* Absolute mouse: no drift on loop — cursor jumps to the
             * first entry's position automatically on next iteration. */
            HAL_Delay(200);
        }
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();

  /* USER CODE BEGIN 2 */
  /* Read mode pin BEFORE USB init so we enumerate with the right class */
  AppMode_t mode = (HAL_GPIO_ReadPin(MODE_PIN_GPIO_Port, MODE_PIN_Pin) == GPIO_PIN_RESET)
                 ? APP_MODE_RECORD
                 : APP_MODE_PLAYBACK;

  if (mode == APP_MODE_RECORD)
      MX_USB_DEVICE_Init_CDC();
  else
      MX_USB_DEVICE_Init_HID();

  HAL_Delay(500);  /* Let USB host enumerate */

  if (mode == APP_MODE_RECORD)
      Run_RecordMode();
  else
      Run_PlaybackMode();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {}
  /* USER CODE END Error_Handler_Debug */
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */