#ifndef __FLASH_STORE_H
#define __FLASH_STORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>

/* ---------------------------------------------------------------------------
 * External SPI flash storage — MX25L6473E (8MB, 64Mbit)
 *
 * Layout inside the MX25L6473E:
 *   Sector 0  (addr 0x000000, 4KB) : FlashHeader_t
 *   Sector 1+ (addr 0x001000, …  ) : FlashEntry_t[]
 *
 * The MX25L6473E has:
 *   - 4096 sectors of 4KB  (sector erase, smallest unit)
 *   - 256-byte pages       (page program, one SPI transaction)
 *   - 8,388,608 bytes total
 *
 * Capacity:
 *   Data region = 8MB - 4KB header = 8,384,512 bytes
 *   FlashEntry_t = 10 bytes → 838,451 entries max
 *   At 125Hz all-unique: ~6,707 seconds (~111 minutes)
 * ------------------------------------------------------------------------- */

/* Erase header sector + all data sectors. Call before recording. */
HAL_StatusTypeDef Flash_EraseRecording(void);

/* Write the recording header once recording is complete. */
HAL_StatusTypeDef Flash_WriteHeader(uint32_t entry_count,
                                     uint32_t total_duration_ms);

/* Append one RLE entry. Returns HAL_ERROR if flash is full. */
HAL_StatusTypeDef Flash_AppendEntry(const FlashEntry_t *entry);

/* Read back header. Returns false if magic is wrong (no valid recording). */
bool Flash_ReadHeader(FlashHeader_t *header);

/* Read one entry by index. Returns false if index >= entry_count. */
bool Flash_ReadEntry(uint32_t index, FlashEntry_t *entry);

/* How many more entries can fit. */
uint32_t Flash_RemainingEntries(void);

/* Current write position (entry count so far). */
uint32_t Flash_CurrentEntryCount(void);

/* One-time chip init — call from main() before anything else. */
HAL_StatusTypeDef Flash_Init(void);

#ifdef __cplusplus
}
#endif

#endif /* __FLASH_STORE_H */
