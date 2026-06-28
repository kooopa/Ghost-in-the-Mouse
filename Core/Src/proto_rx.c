/* proto_rx.c
 * Lightweight framing protocol — transport-agnostic.
 *
 * Frame format:
 *   [0xAA][CMD:1][LEN:1][PAYLOAD:LEN][XOR_CHECKSUM:1]
 *
 * Checksum = XOR of CMD + LEN + all PAYLOAD bytes.
 * Max payload = 32 bytes.
 *
 * In CDC mode:  bytes arrive via CDC_Receive_FS → Proto_FeedByte.
 *               ACK/NAK are sent via CDC_Transmit_FS.
 * In UART mode: bytes arrive via HAL_UART_RxCpltCallback → Proto_FeedByte.
 *               ACK/NAK are sent via HAL_UART_Transmit.
 *               (UART mode kept for fallback / debug builds.)
 */

#include "proto_rx.h"
#include "main.h"
#include <string.h>

#define MAX_PAYLOAD_LEN  32

typedef enum {
    STATE_WAIT_START = 0,
    STATE_CMD,
    STATE_LEN,
    STATE_PAYLOAD,
    STATE_CHECKSUM
} RxState_t;

/* Transport: if s_huart is NULL we are in CDC mode */
static UART_HandleTypeDef *s_huart = NULL;

static RxState_t s_state       = STATE_WAIT_START;
static uint8_t   s_cmd         = 0;
static uint8_t   s_len         = 0;
static uint8_t   s_payload[MAX_PAYLOAD_LEN];
static uint8_t   s_payload_idx = 0;
static uint8_t   s_running_xor = 0;

static uint8_t   s_last_cmd    = 0;
static uint8_t   s_last_payload[MAX_PAYLOAD_LEN];
static uint8_t   s_last_len    = 0;

/* ── Init ───────────────────────────────────────────────────────────────── */

/* Pass NULL for CDC mode, &huart2 for UART mode */
void Proto_Init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_state = STATE_WAIT_START;
}

/* ── State machine ──────────────────────────────────────────────────────── */

bool Proto_FeedByte(uint8_t byte)
{
    switch (s_state) {

        case STATE_WAIT_START:
            if (byte == PROTO_START_BYTE) {
                s_state       = STATE_CMD;
                s_running_xor = 0;
            }
            return false;

        case STATE_CMD:
            s_cmd          = byte;
            s_running_xor ^= byte;
            s_state        = STATE_LEN;
            return false;

        case STATE_LEN:
            s_len          = byte;
            s_running_xor ^= byte;
            s_payload_idx  = 0;
            if (s_len == 0) {
                s_state = STATE_CHECKSUM;
            } else if (s_len > MAX_PAYLOAD_LEN) {
                s_state = STATE_WAIT_START;
            } else {
                s_state = STATE_PAYLOAD;
            }
            return false;

        case STATE_PAYLOAD:
            s_payload[s_payload_idx++] = byte;
            s_running_xor ^= byte;
            if (s_payload_idx >= s_len)
                s_state = STATE_CHECKSUM;
            return false;

        case STATE_CHECKSUM:
            s_state = STATE_WAIT_START;
            if (byte == s_running_xor) {
                s_last_cmd = s_cmd;
                s_last_len = s_len;
                memcpy(s_last_payload, s_payload, s_len);
                return true;
            }
            return false;
    }
    return false;
}

uint8_t  Proto_GetLastCmd(void)        { return s_last_cmd; }
uint8_t *Proto_GetLastPayload(void)    { return s_last_payload; }
uint8_t  Proto_GetLastPayloadLen(void) { return s_last_len; }

/* ── ACK / NAK ──────────────────────────────────────────────────────────── */

static uint8_t ack_buf[2] = { PROTO_START_BYTE, PROTO_ACK };
static uint8_t nak_buf[2] = { PROTO_START_BYTE, PROTO_NAK };

void Proto_SendAck(void)
{
    if (s_huart) {
        HAL_UART_Transmit(s_huart, ack_buf, 2, 10);
    } else {
        /* CDC mode — defined in usbd_cdc_if.c */
        extern HAL_StatusTypeDef CDC_Transmit_FS(uint8_t *buf, uint16_t len);
        CDC_Transmit_FS(ack_buf, 2);
    }
}

void Proto_SendNak(void)
{
    if (s_huart) {
        HAL_UART_Transmit(s_huart, nak_buf, 2, 10);
    } else {
        extern HAL_StatusTypeDef CDC_Transmit_FS(uint8_t *buf, uint16_t len);
        CDC_Transmit_FS(nak_buf, 2);
    }
}
