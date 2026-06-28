#ifndef __USB_HID_H
#define __USB_HID_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include "main.h"
#include <stdint.h>

/* Send an absolute mouse report.
 * x, y: HID coordinates 0..32767 (full range maps to full screen). */
HAL_StatusTypeDef HID_SendReport(uint8_t buttons,
                                  uint16_t x, uint16_t y);
uint8_t HID_IsReady(void);

#ifdef __cplusplus
}
#endif

#endif /* __USB_HID_H */
