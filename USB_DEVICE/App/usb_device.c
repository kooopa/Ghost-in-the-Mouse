/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usb_device.c
  * @brief          : USB Device initialization — HID (playback) or CDC (record)
  ******************************************************************************
  */
/* USER CODE END Header */

#include "usb_device.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_hid.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"

/* USB Device Core handle — shared between both modes */
USBD_HandleTypeDef hUsbDeviceFS;

/* ── HID mode (playback) ─────────────────────────────────────────────────
 * Enumerates as an absolute mouse. Called from main() when PB12 is HIGH. */
void MX_USB_DEVICE_Init_HID(void)
{
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc_HID, DEVICE_FS) != USBD_OK)
        Error_Handler();
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_HID) != USBD_OK)
        Error_Handler();
    if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
        Error_Handler();
}

/* ── CDC mode (recording) ────────────────────────────────────────────────
 * Enumerates as a Virtual COM Port. Called from main() when PB12 is LOW.
 * Python recorder connects to this port exactly like the old UART adapter. */
void MX_USB_DEVICE_Init_CDC(void)
{
    if (USBD_Init(&hUsbDeviceFS, &FS_Desc_CDC, DEVICE_FS) != USBD_OK)
        Error_Handler();
    if (USBD_RegisterClass(&hUsbDeviceFS, &USBD_CDC) != USBD_OK)
        Error_Handler();
    if (USBD_CDC_RegisterInterface(&hUsbDeviceFS, &USBD_Interface_fops_FS) != USBD_OK)
        Error_Handler();
    if (USBD_Start(&hUsbDeviceFS) != USBD_OK)
        Error_Handler();
}
