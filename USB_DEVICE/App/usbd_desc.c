/* usbd_desc.c
 * USB descriptors for dual-mode Mouse Jiggler.
 *
 * Two separate USBD_DescriptorsTypeDef structs are provided:
 *   FS_Desc_HID  — used when PB12 is HIGH (playback, absolute mouse)
 *   FS_Desc_CDC  — used when PB12 is LOW  (recording, virtual COM port)
 *
 * Different VID/PID pairs are used so the OS assigns the correct driver
 * for each mode and doesn't get confused by cached descriptors.
 *
 * HID: VID=0x0483 PID=0x5710  (STM32 HID, custom)
 * CDC: VID=0x0483 PID=0x5740  (STM32 VCP — already has inbox drivers on
 *                               Windows 10/11, Linux, macOS)
 */

#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

/* ── Common strings ─────────────────────────────────────────────────────── */

#define USBD_VID                    0x0483u
#define USBD_LANGID_STRING          0x0409u   /* English */
#define USBD_MANUFACTURER_STRING    "Mouse Jiggler"

/* HID-specific */
#define USBD_PID_HID                0x5710u
#define USBD_PRODUCT_HID            "Mouse Jiggler HID"
#define USBD_CONFIG_HID             "HID Config"
#define USBD_IFACE_HID              "HID Interface"

/* CDC-specific */
#define USBD_PID_CDC                0x5740u
#define USBD_PRODUCT_CDC            "Mouse Jiggler Recorder"
#define USBD_CONFIG_CDC             "CDC Config"
#define USBD_IFACE_CDC              "CDC Interface"

/* ── Shared buffers ─────────────────────────────────────────────────────── */

static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);

__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING),
    HIBYTE(USBD_LANGID_STRING)
};

__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;
__ALIGN_BEGIN uint8_t USBD_StringSerial[USB_SIZ_STRING_SERIAL] __ALIGN_END = {
    USB_SIZ_STRING_SERIAL,
    USB_DESC_TYPE_STRING,
};

/* ── HID device descriptor ──────────────────────────────────────────────── */

__ALIGN_BEGIN uint8_t USBD_HID_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,
    USB_DESC_TYPE_DEVICE,
    0x00, 0x02,             /* USB 2.0 */
    0x00,                   /* bDeviceClass: defined at interface level */
    0x00,
    0x00,
    USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID),   HIBYTE(USBD_VID),
    LOBYTE(USBD_PID_HID), HIBYTE(USBD_PID_HID),
    0x00, 0x02,             /* bcdDevice 2.00 */
    USBD_IDX_MFC_STR,
    USBD_IDX_PRODUCT_STR,
    USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION
};

/* ── CDC device descriptor ──────────────────────────────────────────────── */

__ALIGN_BEGIN uint8_t USBD_CDC_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,
    USB_DESC_TYPE_DEVICE,
    0x00, 0x02,             /* USB 2.0 */
    0x02,                   /* bDeviceClass: CDC */
    0x02,                   /* bDeviceSubClass */
    0x00,
    USB_MAX_EP0_SIZE,
    LOBYTE(USBD_VID),   HIBYTE(USBD_VID),
    LOBYTE(USBD_PID_CDC), HIBYTE(USBD_PID_CDC),
    0x00, 0x02,
    USBD_IDX_MFC_STR,
    USBD_IDX_PRODUCT_STR,
    USBD_IDX_SERIAL_STR,
    USBD_MAX_NUM_CONFIGURATION
};

/* ── Descriptor callbacks — HID ─────────────────────────────────────────── */

static uint8_t *HID_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_HID_DeviceDesc);
    return USBD_HID_DeviceDesc;
}

static uint8_t *HID_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_PRODUCT_HID, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *HID_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_CONFIG_HID, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *HID_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_IFACE_HID, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/* ── Descriptor callbacks — CDC ─────────────────────────────────────────── */

static uint8_t *CDC_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_CDC_DeviceDesc);
    return USBD_CDC_DeviceDesc;
}

static uint8_t *CDC_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_PRODUCT_CDC, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *CDC_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_CONFIG_CDC, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *CDC_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_IFACE_CDC, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/* ── Shared callbacks (same for both modes) ─────────────────────────────── */

static uint8_t *FS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = sizeof(USBD_LangIDDesc);
    return USBD_LangIDDesc;
}

static uint8_t *FS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

static uint8_t *FS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    UNUSED(speed);
    *length = USB_SIZ_STRING_SERIAL;
    Get_SerialNum();
    return USBD_StringSerial;
}

/* ── Exported descriptor tables ─────────────────────────────────────────── */

USBD_DescriptorsTypeDef FS_Desc_HID = {
    HID_DeviceDescriptor,
    FS_LangIDStrDescriptor,
    FS_ManufacturerStrDescriptor,
    HID_ProductStrDescriptor,
    FS_SerialStrDescriptor,
    HID_ConfigStrDescriptor,
    HID_InterfaceStrDescriptor,
};

USBD_DescriptorsTypeDef FS_Desc_CDC = {
    CDC_DeviceDescriptor,
    FS_LangIDStrDescriptor,
    FS_ManufacturerStrDescriptor,
    CDC_ProductStrDescriptor,
    FS_SerialStrDescriptor,
    CDC_ConfigStrDescriptor,
    CDC_InterfaceStrDescriptor,
};

/* ── Serial number helpers (unchanged from CubeMX) ──────────────────────── */

static void Get_SerialNum(void)
{
    uint32_t deviceserial0 = *(uint32_t *)DEVICE_ID1;
    uint32_t deviceserial1 = *(uint32_t *)DEVICE_ID2;
    uint32_t deviceserial2 = *(uint32_t *)DEVICE_ID3;
    deviceserial0 += deviceserial2;
    if (deviceserial0 != 0) {
        IntToUnicode(deviceserial0, &USBD_StringSerial[2],  8);
        IntToUnicode(deviceserial1, &USBD_StringSerial[18], 4);
    }
}

static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    for (uint8_t idx = 0; idx < len; idx++) {
        pbuf[2 * idx] = (((value >> 28)) < 0xAu)
                        ? (value >> 28) + '0'
                        : (value >> 28) + 'A' - 10u;
        value <<= 4;
        pbuf[2 * idx + 1] = 0;
    }
}
