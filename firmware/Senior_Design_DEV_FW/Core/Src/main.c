#include "board.h"
#include "demo_app.h"
#include "demo_console.h"
#include "demo_network.h"
int main(void) {
    board_init();
    board_sensors_start();
    console_init();
    network_init(); /* Optional hardware failure leaves the analog path usable. */
    app_init();
    board_start_control();
    board_watchdog_start();
    for (;;) {
        console_poll();
        network_poll();
        app_foreground();
    }
}
