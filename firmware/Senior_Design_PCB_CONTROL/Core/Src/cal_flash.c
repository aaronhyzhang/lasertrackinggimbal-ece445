#include "cal_flash.h"
#include "stm32h7xx_hal.h"
#include <string.h>

static cal_flash_state_t state;
static uint32_t next_offset;
static uint32_t pending[CAL_RECORD_BYTES/4]; /* Word-aligned source for HAL_FLASH_Program. */

static const uint8_t *sector(void) { return (const uint8_t *)CAL_FLASH_ADDRESS; }

void cal_flash_init(cal_scan_t *scan) {
    cal_scan(sector(),CAL_FLASH_SIZE,scan);
    next_offset=scan->full?CAL_FLASH_SIZE:(uint32_t)scan->next_offset;
    state=CAL_FLASH_IDLE;
}
cal_flash_state_t cal_flash_state(void) { return state; }

/* Two 256-bit flash words. Each HAL_FLASH_Program waits only for its own word
 * (tens of microseconds), which is acceptable in the foreground loop. */
static void program(void) {
    uint32_t address=CAL_FLASH_ADDRESS+next_offset;
    HAL_StatusTypeDef status=HAL_FLASH_Unlock();
    for (uint32_t w=0;w<CAL_RECORD_BYTES/32u && status==HAL_OK;++w)
        status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,address+32u*w,(uint32_t)(uintptr_t)&pending[8u*w]);
    (void)HAL_FLASH_Lock();
    if (status!=HAL_OK || memcmp((const void *)(uintptr_t)address,pending,CAL_RECORD_BYTES)!=0) {
        state=CAL_FLASH_ERROR;return;
    }
    next_offset+=CAL_RECORD_BYTES;state=CAL_FLASH_IDLE;
}
bool cal_flash_write(const uint8_t record[CAL_RECORD_BYTES]) {
    if (state!=CAL_FLASH_IDLE) return false;
    memcpy(pending,record,CAL_RECORD_BYTES);
    if (next_offset+CAL_RECORD_BYTES<=CAL_FLASH_SIZE) { program();return true; }
    /* Sector full: start a non-blocking erase; cal_flash_service() finishes it. */
    if (HAL_FLASH_Unlock()!=HAL_OK) { state=CAL_FLASH_ERROR;return true; }
    __HAL_FLASH_CLEAR_FLAG_BANK2(FLASH_FLAG_ALL_ERRORS_BANK2);
    FLASH_Erase_Sector(CAL_FLASH_SECTOR,FLASH_BANK_2,FLASH_VOLTAGE_RANGE_3);
    state=CAL_FLASH_ERASING;return true;
}
void cal_flash_service(void) {
    if (state!=CAL_FLASH_ERASING || (FLASH->SR2&(FLASH_SR_QW|FLASH_SR_BSY))) return;
    CLEAR_BIT(FLASH->CR2,FLASH_CR_SER|FLASH_CR_SNB);
    bool failed=(FLASH->SR2&(FLASH_FLAG_ALL_ERRORS_BANK2&0x7FFFFFFFu))!=0;
    __HAL_FLASH_CLEAR_FLAG_BANK2(FLASH_FLAG_ALL_ERRORS_BANK2);
    (void)HAL_FLASH_Lock();
    if (failed) { state=CAL_FLASH_ERROR;return; }
    next_offset=0;program();
}
