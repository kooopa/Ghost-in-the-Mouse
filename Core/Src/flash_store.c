/* flash_store.c
 * External SPI flash driver for MX25L6473E (8MB).
 *
 * SPI wiring (configured in CubeMX as SPI1, prescaler 4 = 18MHz):
 *   PA4  = FLASH_CS   (GPIO Output, active LOW)
 *   PA5  = SPI1_SCK
 *   PA6  = SPI1_MISO
 *   PA7  = SPI1_MOSI
 *   WP   = 3.3V  (write protect disabled)
 *   HOLD = 3.3V  (hold disabled)
 *
 * MX25L6473E geometry:
 *   Total:        8,388,608 bytes (8MB)
 *   Sector size:  4,096 bytes  (smallest erasable unit)
 *   Page size:    256 bytes    (largest single program transaction)
 *
 * Flash layout:
 *   0x000000  Sector 0  (4KB)  : FlashHeader_t (16 bytes, rest unused)
 *   0x001000  Sector 1+        : FlashEntry_t[] (10 bytes each)
 *
 * All SPI transactions follow the pattern:
 *   CS low → send command [+ address] [+ data] → CS high
 *
 * Write operations (Page Program, Sector Erase) require a WREN command
 * immediately before, and must poll WIP (Write-In-Progress) until done.
 */

#include "flash_store.h"
#include "main.h"
#include "spi.h"
#include <string.h>

/* ── SPI handle ─────────────────────────────────────────────────────────── */

extern SPI_HandleTypeDef hspi1;
#define FLASH_SPI       (&hspi1)
#define FLASH_SPI_TIMEOUT  100u   /* ms */

/* ── CS pin helpers ─────────────────────────────────────────────────────── */

#define CS_LOW()   HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_RESET)
#define CS_HIGH()  HAL_GPIO_WritePin(FLASH_CS_GPIO_Port, FLASH_CS_Pin, GPIO_PIN_SET)

/* ── MX25L6473E command set ─────────────────────────────────────────────── */

#define CMD_WREN        0x06u   /* Write Enable */
#define CMD_WRDI        0x04u   /* Write Disable */
#define CMD_RDSR        0x05u   /* Read Status Register */
#define CMD_READ        0x03u   /* Read Data */
#define CMD_PP          0x02u   /* Page Program (256 bytes max) */
#define CMD_BE          0xC7u   /* Bulk Erase (whole chip) */
#define CMD_RDID        0x9Fu   /* Read JEDEC ID */

#define SR_WIP          0x01u   /* Status Register: Write In Progress bit */

/* ── Address layout ─────────────────────────────────────────────────────── */

#define SECTOR_SIZE         4096u
#define PAGE_SIZE           256u
#define TOTAL_BYTES         (8u * 1024u * 1024u)   /* 8MB */

#define HEADER_ADDR         0x000000UL
#define DATA_START_ADDR     0x001000UL   /* sector 1 */
#define DATA_END_ADDR       TOTAL_BYTES

#define MAX_ENTRIES         ((DATA_END_ADDR - DATA_START_ADDR) / sizeof(FlashEntry_t))

/* ── State ──────────────────────────────────────────────────────────────── */

static uint32_t s_write_addr  = DATA_START_ADDR;
static uint32_t s_entry_count = 0;

/* ── Low-level SPI helpers ──────────────────────────────────────────────── */

static HAL_StatusTypeDef spi_tx(const uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Transmit(FLASH_SPI, (uint8_t *)buf, len, FLASH_SPI_TIMEOUT);
}

static HAL_StatusTypeDef spi_rx(uint8_t *buf, uint16_t len)
{
    return HAL_SPI_Receive(FLASH_SPI, buf, len, FLASH_SPI_TIMEOUT);
}

/* Send a 3-byte address in big-endian order (MSB first, as MX25L requires) */
static HAL_StatusTypeDef spi_tx_addr(uint32_t addr)
{
    uint8_t a[3] = {
        (uint8_t)(addr >> 16),
        (uint8_t)(addr >>  8),
        (uint8_t)(addr      ),
    };
    return spi_tx(a, 3);
}

/* Poll the WIP bit until the chip is idle or timeout expires.
 * MX25L6473E typical times: Page Program ~1.4ms, Sector Erase ~60ms.
 * We give each operation 500ms maximum before declaring failure. */
static HAL_StatusTypeDef wait_not_busy(uint32_t timeout_ms)
{
    uint8_t cmd  = CMD_RDSR;
    uint8_t sr   = 0;
    uint32_t t0  = HAL_GetTick();

    CS_LOW();
    spi_tx(&cmd, 1);

    do {
        if (HAL_GetTick() - t0 > timeout_ms) {
            CS_HIGH();
            return HAL_TIMEOUT;
        }
        spi_rx(&sr, 1);
    } while (sr & SR_WIP);

    CS_HIGH();
    return HAL_OK;
}

/* Send WREN — must be called immediately before every write/erase command */
static HAL_StatusTypeDef write_enable(void)
{
    uint8_t cmd = CMD_WREN;
    CS_LOW();
    HAL_StatusTypeDef s = spi_tx(&cmd, 1);
    CS_HIGH();
    return s;
}

/* ── Page Program ───────────────────────────────────────────────────────── */

/* Write up to 256 bytes starting at 'addr'.
 * Caller MUST ensure the write does not cross a 256-byte page boundary —
 * the chip wraps around within the page otherwise. */
static HAL_StatusTypeDef page_program(uint32_t addr,
                                       const uint8_t *data,
                                       uint16_t len)
{
    HAL_StatusTypeDef s;

    s = write_enable();
    if (s != HAL_OK) return s;

    CS_LOW();
    uint8_t cmd = CMD_PP;
    s = spi_tx(&cmd, 1);
    if (s == HAL_OK) s = spi_tx_addr(addr);
    if (s == HAL_OK) s = spi_tx(data, len);
    CS_HIGH();
    if (s != HAL_OK) return s;

    return wait_not_busy(20u);   /* page program: typ 1.4ms, max 10ms */
}

/* ── Multi-page write ───────────────────────────────────────────────────── */

/* Write an arbitrary buffer, splitting across 256-byte page boundaries.
 * The target region must already be erased (0xFF). */
static HAL_StatusTypeDef flash_write(uint32_t addr,
                                      const uint8_t *data,
                                      uint32_t len)
{
    while (len > 0) {
        /* How many bytes until the next 256-byte page boundary? */
        uint32_t page_offset  = addr & (PAGE_SIZE - 1u);
        uint32_t chunk        = PAGE_SIZE - page_offset;
        if (chunk > len) chunk = len;

        HAL_StatusTypeDef s = page_program(addr, data, (uint16_t)chunk);
        if (s != HAL_OK) return s;

        addr += chunk;
        data += chunk;
        len  -= chunk;
    }
    return HAL_OK;
}

/* ── Read ───────────────────────────────────────────────────────────────── */

static HAL_StatusTypeDef flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmd = CMD_READ;
    CS_LOW();
    HAL_StatusTypeDef s = spi_tx(&cmd, 1);
    if (s == HAL_OK) s = spi_tx_addr(addr);
    if (s == HAL_OK) s = spi_rx(buf, (uint16_t)len);
    CS_HIGH();
    return s;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

HAL_StatusTypeDef Flash_Init(void)
{
    CS_HIGH();   /* ensure CS is deasserted */
    HAL_Delay(10);  /* tVSL: chip needs ~800us after power-on before accepting commands */

    /* Read JEDEC ID — sanity check that the chip is alive.
     * MX25L6473E: manufacturer=0xC2, memory type=0x20, capacity=0x17 */
    uint8_t cmd = CMD_RDID;
    uint8_t id[3] = {0};
    CS_LOW();
    HAL_StatusTypeDef s = spi_tx(&cmd, 1);
    if (s == HAL_OK) s = spi_rx(id, 3);
    CS_HIGH();

    if (s != HAL_OK)    return HAL_ERROR;
    if (id[0] != 0xC2u) return HAL_ERROR;  /* wrong manufacturer */
    if (id[1] != 0x20u) return HAL_ERROR;  /* wrong memory type  */
    if (id[2] < 0x16u)  return HAL_ERROR;  /* < 32Mbit */

    /* Recover from any incomplete previous write by waiting for WIP to clear */
    s = wait_not_busy(500u);
    if (s != HAL_OK) return s;

    return HAL_OK;
}

HAL_StatusTypeDef Flash_EraseRecording(void)
{
    HAL_StatusTypeDef s;

    /* Bulk erase — wipes the entire chip in one command.
     * MX25L6473E typical time: ~25s, max 80s.
     * Much faster and simpler than erasing 2044 sectors individually. */
    s = write_enable();
    if (s != HAL_OK) return s;

    uint8_t cmd = CMD_BE;
    CS_LOW();
    s = spi_tx(&cmd, 1);
    CS_HIGH();
    if (s != HAL_OK) return s;

    /* Poll WIP — bulk erase can take up to 80s */
    s = wait_not_busy(90000u);
    if (s != HAL_OK) return s;

    s_write_addr  = DATA_START_ADDR;
    s_entry_count = 0;
    return HAL_OK;
}

HAL_StatusTypeDef Flash_WriteHeader(uint32_t entry_count,
                                     uint32_t total_duration_ms)
{
    FlashHeader_t hdr;
    hdr.magic             = RECORDING_MAGIC;
    hdr.entry_count       = entry_count;
    hdr.total_duration_ms = total_duration_ms;
    hdr.reserved          = 0xFFFFFFFFUL;

    return flash_write(HEADER_ADDR, (const uint8_t *)&hdr, sizeof(FlashHeader_t));
}

HAL_StatusTypeDef Flash_AppendEntry(const FlashEntry_t *entry)
{
    if (s_write_addr + sizeof(FlashEntry_t) > DATA_END_ADDR) {
        return HAL_ERROR;   /* full */
    }

    HAL_StatusTypeDef s = flash_write(s_write_addr,
                                       (const uint8_t *)entry,
                                       sizeof(FlashEntry_t));
    if (s == HAL_OK) {
        s_write_addr  += sizeof(FlashEntry_t);
        s_entry_count++;
    }
    return s;
}

bool Flash_ReadHeader(FlashHeader_t *header)
{
    HAL_StatusTypeDef s = flash_read(HEADER_ADDR,
                                      (uint8_t *)header,
                                      sizeof(FlashHeader_t));
    if (s != HAL_OK) return false;
    return (header->magic == RECORDING_MAGIC);
}

bool Flash_ReadEntry(uint32_t index, FlashEntry_t *entry)
{
    if (index >= MAX_ENTRIES) return false;
    uint32_t addr = DATA_START_ADDR + index * sizeof(FlashEntry_t);
    if (addr + sizeof(FlashEntry_t) > DATA_END_ADDR) return false;
    return (flash_read(addr, (uint8_t *)entry, sizeof(FlashEntry_t)) == HAL_OK);
}

uint32_t Flash_RemainingEntries(void)
{
    if (s_write_addr >= DATA_END_ADDR) return 0;
    return (DATA_END_ADDR - s_write_addr) / sizeof(FlashEntry_t);
}

uint32_t Flash_CurrentEntryCount(void)
{
    return s_entry_count;
}
