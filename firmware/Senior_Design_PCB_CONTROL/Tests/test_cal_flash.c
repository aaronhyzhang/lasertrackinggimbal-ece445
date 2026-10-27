/* Actual cal_flash.c with the flash controller and calibration sector mapped as
 * private memory at their real addresses (Windows VirtualAlloc). The HAL program /
 * erase entry points are scripted: programming ANDs bits like real flash, and an
 * erase stays in progress (QW set) until the test completes it. */
#include "cal_flash.h"
#include "stm32h7xx_hal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
/* Built as 32-bit x86 (Tools/test_bsp.py): HAL_FLASH_Program takes a uint32_t source
 * address, as on the Cortex-M7, which would truncate a 64-bit host pointer. */
__declspec(dllimport) void *__stdcall VirtualAlloc(void *,size_t,unsigned long,unsigned long);

static bool erase_started,program_fails,stuck_bit,erase_error;
static unsigned programs,unlocks,locks;
HAL_StatusTypeDef HAL_FLASH_Unlock(void) { ++unlocks;return HAL_OK; }
HAL_StatusTypeDef HAL_FLASH_Lock(void) { ++locks;return HAL_OK; }
void FLASH_Erase_Sector(uint32_t sector,uint32_t banks,uint32_t range) {
    assert(sector==CAL_FLASH_SECTOR && banks==FLASH_BANK_2 && range==FLASH_VOLTAGE_RANGE_3);
    erase_started=true;FLASH->SR2|=FLASH_SR_QW;FLASH->CR2|=FLASH_CR_SER|(sector<<FLASH_CR_SNB_Pos);
}
HAL_StatusTypeDef HAL_FLASH_Program(uint32_t type,uint32_t address,uint32_t data) {
    assert(type==FLASH_TYPEPROGRAM_FLASHWORD && address%32u==0 && data%4u==0);
    assert(address>=CAL_FLASH_ADDRESS && address+32u<=CAL_FLASH_ADDRESS+CAL_FLASH_SIZE);
    assert(!(FLASH->SR2&FLASH_SR_QW));                       /* Never while the bank is busy. */
    ++programs;if (program_fails) return HAL_ERROR;
    uint8_t *dst=(uint8_t *)(uintptr_t)address;const uint8_t *src=(const uint8_t *)(uintptr_t)data;
    for (unsigned i=0;i<32;++i) dst[i]&=src[i];             /* Flash can only clear bits. */
    if (stuck_bit) dst[3]|=0x80u;
    return HAL_OK;
}
static uint8_t *sector(void) { return (uint8_t *)(uintptr_t)CAL_FLASH_ADDRESS; }
static void complete_erase(void) {
    if (erase_error) FLASH->SR2|=FLASH_SR_PGSERR;
    memset(sector(),0xFF,CAL_FLASH_SIZE);FLASH->SR2&=~FLASH_SR_QW;erase_started=false;
}
static void record(uint32_t sequence,uint16_t zero_raw,uint8_t out[CAL_RECORD_BYTES]) {
    cal_data_t d={0};d.sequence=sequence;d.axis[1]=(cal_axis_t){true,true,-1,zero_raw,1.0f};cal_pack(&d,out);
}
static void reset(void) {
    memset(sector(),0xFF,CAL_FLASH_SIZE);memset((void *)FLASH,0,sizeof(FLASH_TypeDef));
    erase_started=program_fails=stuck_bit=erase_error=false;programs=0;
}

int main(void) {
    assert(VirtualAlloc((void *)0x52000000,0x10000,0x3000,4)==(void *)0x52000000);       /* FLASH regs */
    assert(VirtualAlloc((void *)(uintptr_t)CAL_FLASH_ADDRESS,CAL_FLASH_SIZE,0x3000,4)==(void *)(uintptr_t)CAL_FLASH_ADDRESS);
    uint8_t r[CAL_RECORD_BYTES];cal_scan_t scan;

    reset();cal_flash_init(&scan);assert(!scan.found && cal_flash_state()==CAL_FLASH_IDLE);
    record(1,111,r);assert(cal_flash_write(r) && programs==2 && cal_flash_state()==CAL_FLASH_IDLE);
    record(2,222,r);assert(cal_flash_write(r) && programs==4 && locks==unlocks);
    cal_flash_init(&scan);                                   /* "Reboot": newest record found. */
    assert(scan.found && scan.latest.sequence==2 && scan.latest.axis[1].zero_raw==222 && scan.next_offset==128);

    /* Full sector: the next write erases without blocking, then programs slot 0. */
    for (uint32_t i=3;programs<CAL_FLASH_SIZE/32u;++i) { record(i,(uint16_t)i,r);assert(cal_flash_write(r)); }
    cal_flash_init(&scan);assert(scan.full && scan.latest.sequence==CAL_FLASH_SIZE/CAL_RECORD_BYTES);
    record(5000,500,r);assert(cal_flash_write(r) && erase_started && cal_flash_state()==CAL_FLASH_ERASING);
    for (unsigned k=0;k<100;++k) cal_flash_service();       /* Still erasing: nothing programmed. */
    assert(cal_flash_state()==CAL_FLASH_ERASING);
    record(5001,501,r);assert(!cal_flash_write(r));          /* Busy: refused, caller retries. */
    unsigned before=programs;complete_erase();cal_flash_service();
    assert(cal_flash_state()==CAL_FLASH_IDLE && programs==before+2 && !(FLASH->CR2&FLASH_CR_SER));
    cal_flash_init(&scan);assert(scan.found && scan.latest.sequence==5000 && scan.next_offset==64);
    puts("PASS calibration flash append, reboot scan, non-blocking erase when full");

    reset();cal_flash_init(&scan);program_fails=true;record(1,1,r);
    assert(cal_flash_write(r) && cal_flash_state()==CAL_FLASH_ERROR && !cal_flash_write(r));
    reset();cal_flash_init(&scan);stuck_bit=true;record(1,1,r);   /* Read-back verify catches it. */
    assert(cal_flash_write(r) && cal_flash_state()==CAL_FLASH_ERROR);
    reset();memset(sector(),0,CAL_FLASH_SIZE);cal_flash_init(&scan);  /* Full of garbage: erase first. */
    assert(!scan.found && scan.full);erase_error=true;record(1,1,r);
    assert(cal_flash_write(r) && cal_flash_state()==CAL_FLASH_ERASING);
    complete_erase();cal_flash_service();assert(cal_flash_state()==CAL_FLASH_ERROR);
    puts("PASS calibration flash program failure, verify mismatch and erase error reporting");
    return 0;
}
