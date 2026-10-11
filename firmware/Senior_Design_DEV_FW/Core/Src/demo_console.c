#include "demo_console.h"
#include "demo_app.h"
#include "board.h"
#include "demo_protocol.h"
#include <string.h>
#define RX_SIZE 256u
#define TX_SIZE 2048u
static uint8_t rx_byte, rx[RX_SIZE], tx[TX_SIZE];
static volatile uint32_t rx_write, rx_read, tx_read, tx_count;
static uint32_t tx_write;
static volatile bool tx_busy;
static volatile bool rx_overflow;
static uint16_t tx_active;
static char line[80];
static size_t line_size;
static bool discard_line;
static void write_text(const char *s) {
    size_t length=strlen(s); uint32_t mask=board_lock();
    if (length<=TX_SIZE-tx_count) {
        for (size_t i=0;i<length;++i) { tx[tx_write]=s[i]; tx_write=(tx_write+1u)%TX_SIZE; }
        tx_count+=(uint32_t)length;
    }
    board_unlock(mask);
}
void console_init(void) {
    if (HAL_UART_Receive_IT(&huart3,&rx_byte,1)!=HAL_OK) Error_Handler();
    write_text("H753 FIT1036 voltage servo; inhibited at boot. HELP for commands.\r\n");
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *h) {
    if (h!=&huart3) return;
    if (rx_byte=='!') app_stop();
    else {
        uint32_t next=(rx_write+1u)%RX_SIZE;
        if (next==rx_read) { rx_overflow=true; app_stop(); }
        else { rx[rx_write]=rx_byte; __DMB(); rx_write=next; }
    }
    if (HAL_UART_Receive_IT(h,&rx_byte,1)!=HAL_OK) { rx_overflow=true; app_stop(); }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *h) {
    if (h!=&huart3) return;
    tx_read=(tx_read+tx_active)%TX_SIZE; tx_count-=tx_active; tx_busy=false;
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *h) {
    if (h!=&huart3) return;
    app_stop(); rx_overflow=true;
    /* HAL aborts RX on overrun; start a fresh byte reception if now ready. */
    (void)HAL_UART_Receive_IT(h,&rx_byte,1);
}
static void execute(void) {
    if (!strcmp(line,"HELP")) {
        write_text("STATUS CALIBRATE ZERO ARM DISABLE CLEAR_FAULT SOURCE ANALOG|SERIAL|ETHERNET\r\n"
                   "POSITION <0..360000 millidegrees> HEARTBEAT; ! immediate stop.\r\n"); return;
    }
    if (!strcmp(line,"STATUS")) { char response[640]; app_status_text(response,sizeof(response)); write_text(response); return; }
    motor_action_t a=ACTION_NONE;
    if (!strcmp(line,"CALIBRATE")) a=ACTION_CALIBRATE;
    else if (!strcmp(line,"ZERO")) a=ACTION_ZERO;
    else if (!strcmp(line,"ARM")) a=ACTION_ARM;
    else if (!strcmp(line,"DISABLE")) a=ACTION_DISABLE;
    else if (!strcmp(line,"CLEAR_FAULT")) a=ACTION_CLEAR_FAULT;
    bool ok=false;
    if (a!=ACTION_NONE) { ok=app_request(a); write_text(ok?"QUEUED; check STATUS\r\n":"ERR state or command busy\r\n"); return; }
    if (!strcmp(line,"SOURCE ANALOG")) ok=app_source(SOURCE_ANALOG);
    else if (!strcmp(line,"SOURCE SERIAL")) ok=app_source(SOURCE_SERIAL);
    else if (!strcmp(line,"SOURCE ETHERNET")) ok=app_source(SOURCE_ETHERNET);
    else if (!strcmp(line,"HEARTBEAT")) ok=app_heartbeat(SOURCE_SERIAL);
    else if (!strncmp(line,"POSITION ",9)) {
        uint32_t value;
        if (protocol_u32(line+9,&value)) ok=app_position(SOURCE_SERIAL,value);
    }
    write_text(ok?"OK\r\n":"ERR syntax, range, source, or state\r\n");
}
void console_poll(void) {
    if (rx_overflow) { uint32_t m=board_lock(); rx_read=rx_write; rx_overflow=false; board_unlock(m);
        line_size=0; discard_line=true; write_text("ERR serial loss; drive disabled\r\n"); }
    for (unsigned budget=0;budget<128 && rx_read!=rx_write;++budget) {
        uint8_t ch=rx[rx_read]; rx_read=(rx_read+1u)%RX_SIZE;
        if (ch=='\r' || ch=='\n') {
            if (!discard_line && line_size) { line[line_size]=0; execute(); }
            line_size=0; discard_line=false;
        } else if (ch>=32u && ch<=126u && !discard_line) {
            if (line_size<sizeof(line)-1u) line[line_size++]=(char)ch;
            else { discard_line=true; app_stop(); write_text("ERR line too long; disabled\r\n"); }
        }
    }
    uint32_t mask=board_lock();
    if (!tx_busy && tx_count) {
        tx_active=(uint16_t)(tx_count<TX_SIZE-tx_read ? tx_count : TX_SIZE-tx_read);
        tx_busy=true;
        if (HAL_UART_Transmit_IT(&huart3,&tx[tx_read],tx_active)!=HAL_OK) tx_busy=false;
    }
    board_unlock(mask);
}
