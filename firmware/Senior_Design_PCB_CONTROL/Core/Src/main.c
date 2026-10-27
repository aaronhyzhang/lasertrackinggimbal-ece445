#include "board.h"
#include "demo_app.h"
#include "demo_network.h"

int main(void) {

	board_init();       /* Outputs start inhibited. */
    network_init(); /* Before control/watchdog; missing Ethernet is nonfatal. */
    app_init(); /* Start freshness deadlines after potentially slow PHY initialization. */
    board_start_control();
    board_watchdog_start();
    for (;;) app_foreground();
}
