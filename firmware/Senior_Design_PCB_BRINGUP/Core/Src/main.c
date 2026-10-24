#include "board.h"
#include "demo_app.h"

int main(void) {

	board_init();       /* Outputs start inhibited. */
    app_init();
    board_start_control();
    board_watchdog_start();
    for (;;) app_foreground();
}
