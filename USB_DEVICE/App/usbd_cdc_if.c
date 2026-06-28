/* usbd_cdc_if.c
 * CDC (Virtual COM Port) interface for Mouse Jiggler recording mode.
 *
 * KEY DESIGN: CDC_Receive_FS runs inside a USB interrupt. We must NOT
 * call CDC_Transmit_FS (ACK/NAK) from within that context — doing so
 * re-enters the USB stack and deadlocks it, causing all writes to fail.
 *
 * Solution: CDC_Receive_FS sets a volatile flag + copies the command.
 * The main loop in Run_RecordMode polls the flag, processes the command,
 * and sends ACK/NAK from thread context where USB TX is safe.
 */

#include "usbd_cdc_if.h"
#include "proto_rx.h"
#include <string.h>

#define CDC_RX_BUF_SIZE   64u
static uint8_t cdc_rx_buf[CDC_RX_BUF_SIZE];

/* ── Pending command flag ───────────────────────────────────────────────── */
/* Set by CDC_Receive_FS (IRQ context), cleared by main loop after handling */
volatile bool cdc_cmd_pending = false;

/* ── Interface callbacks ────────────────────────────────────────────────── */

static int8_t CDC_Init_FS(void);
static int8_t CDC_DeInit_FS(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length);
static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS
};

extern USBD_HandleTypeDef hUsbDeviceFS;

static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, cdc_rx_buf);
    return USBD_OK;
}

static int8_t CDC_DeInit_FS(void)
{
    return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)cmd; (void)pbuf; (void)length;
    return USBD_OK;
}

static int8_t CDC_Receive_FS(uint8_t *pbuf, uint32_t *Len)
{
    /* Feed bytes into protocol state machine.
     * If a complete frame is ready, set the flag for the main loop.
     * Do NOT call Proto_SendAck/Nak here — we are in IRQ context. */
    for (uint32_t i = 0; i < *Len; i++) {
        if (Proto_FeedByte(pbuf[i])) {
            cdc_cmd_pending = true;
            /* Don't break — there may be more bytes in this bulk transfer
             * but in practice each 9-byte frame arrives in its own packet. */
        }
    }

    /* Re-arm receive buffer */
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, cdc_rx_buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

/* ── Transmit — call only from main loop (thread context) ───────────────── */

HAL_StatusTypeDef CDC_Transmit_FS(uint8_t *buf, uint16_t len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

    if (hcdc == NULL) return HAL_ERROR;

    uint32_t t0 = HAL_GetTick();
    while (hcdc->TxState != 0) {
        if (HAL_GetTick() - t0 > 10u) return HAL_TIMEOUT;
    }

    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, buf, len);
    USBD_CDC_TransmitPacket(&hUsbDeviceFS);
    return HAL_OK;
}