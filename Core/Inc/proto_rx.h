#ifndef __PROTO_RX_H
#define __PROTO_RX_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/* Call once with the UART handle to use */
void Proto_Init(UART_HandleTypeDef *huart);

/* Feed one received byte into the state machine.
   Returns true when a complete valid frame has been parsed.
   Check Proto_GetLastCmd() / Proto_GetLastPayload() after. */
bool Proto_FeedByte(uint8_t byte);

uint8_t  Proto_GetLastCmd(void);
uint8_t *Proto_GetLastPayload(void);
uint8_t  Proto_GetLastPayloadLen(void);

/* Send ACK or NAK back to client */
void Proto_SendAck(void);
void Proto_SendNak(void);

/* Send a status string (optional debug) */
void Proto_SendStatus(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* __PROTO_RX_H */
