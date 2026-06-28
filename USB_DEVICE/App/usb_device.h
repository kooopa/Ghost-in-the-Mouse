/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.h
  * @brief          : Header for usb_device.c
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __USB_DEVICE__H__
#define __USB_DEVICE__H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx.h"
#include "stm32f1xx_hal.h"
#include "usbd_def.h"

/* HID init — call in playback mode (PB12 HIGH) */
void MX_USB_DEVICE_Init_HID(void);

/* CDC init — call in record mode (PB12 LOW) */
void MX_USB_DEVICE_Init_CDC(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_DEVICE__H__ */
