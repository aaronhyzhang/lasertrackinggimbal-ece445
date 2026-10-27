#ifndef CAL_FLASH_H
#define CAL_FLASH_H
/* Calibration log in the last 128 KB sector of flash bank 2 (STM32H743ZI, 2 MB).
 * The linker script stops the program 128 KB short, so code never lands here.
 * Code runs from bank 1, so a bank-2 erase does not stall the control interrupt.
 * Records are appended in 64-byte slots; the sector is erased only when full.
 * All calls are foreground-only; cal_flash_service() never blocks on an erase. */
#include <stdbool.h>
#include <stdint.h>
#include "cal_store.h"

#define CAL_FLASH_ADDRESS 0x081E0000u /* Bank 2, sector 7 */
#define CAL_FLASH_SECTOR 7u
#define CAL_FLASH_SIZE 0x20000u

typedef enum { CAL_FLASH_IDLE,CAL_FLASH_ERASING,CAL_FLASH_ERROR } cal_flash_state_t;

void cal_flash_init(cal_scan_t *scan);                         /* Reads the sector. */
bool cal_flash_write(const uint8_t record[CAL_RECORD_BYTES]);  /* False unless IDLE. */
void cal_flash_service(void);                                  /* Advances a pending erase. */
cal_flash_state_t cal_flash_state(void);
#endif
