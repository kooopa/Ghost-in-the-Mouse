#ifndef __USBD_CDC_IF_H
#define __USBD_CDC_IF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "usbd_cdc.h"
#include "stm32f1xx_hal.h"

/* Interface ops struct — passed to USBD_RegisterClass */
extern USBD_CDC_ItfTypeDef USBD_Interface_fops_FS;

/* Set by CDC_Receive_FS (IRQ) when a complete frame arrives.
 * Polled and cleared by Run_RecordMode main loop. */
extern volatile bool cdc_cmd_pending;

/* Send bytes over CDC — call only from main loop (thread context) */
HAL_StatusTypeDef CDC_Transmit_FS(uint8_t *buf, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* __USBD_CDC_IF_H */