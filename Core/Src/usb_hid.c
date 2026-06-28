/* usb_hid.c
 * USB HID Absolute Mouse device implementation.
 *
 * Report format (5 bytes):
 *   Byte 0:     Buttons (bit0=L, bit1=R, bit2=M) | 5-bit padding
 *   Bytes 1-2:  X absolute, little-endian, range 0..32767
 *   Bytes 3-4:  Y absolute, little-endian, range 0..32767
 *
 * Coordinates are in HID space (0..32767). The OS maps this to
 * physical screen pixels based on the logical/physical extents in
 * the report descriptor. No driver-side scaling is needed.
 */


#include "usb_hid.h"
#include "usbd_def.h"
#include "usbd_core.h"
#include "usbd_hid.h"

extern USBD_HandleTypeDef hUsbDeviceFS;
extern uint8_t USBD_HID_SendReport(USBD_HandleTypeDef *pdev,
                                    uint8_t *report, uint16_t len);

static uint8_t s_report[5] = {0};

HAL_StatusTypeDef HID_SendReport(uint8_t buttons,
                                  uint16_t x, uint16_t y)
{
    s_report[0] = buttons & 0x07u;
    s_report[1] = (uint8_t)(x & 0xFFu);
    s_report[2] = (uint8_t)(x >> 8);
    s_report[3] = (uint8_t)(y & 0xFFu);
    s_report[4] = (uint8_t)(y >> 8);

    uint8_t result = USBD_HID_SendReport(&hUsbDeviceFS, s_report, 5);
    return (result == USBD_OK) ? HAL_OK : HAL_ERROR;
}

uint8_t HID_IsReady(void)
{
    return (hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED) ? 1u : 0u;
}
