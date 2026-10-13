#include "board.h"
#include "demo_config.h"
#include "demo_app.h"
#include "demo_console.h"
#include "demo_network.h"
int main(void) {
    board_init();
#if DEMO_PWM_SCOPE_TEST
    /* ADC-to-PWM test starts without serial, Ethernet, encoder or ARM.
     * TIM7/controller are not started; driver ENABLE stays LOW throughout. */
    board_watchdog_start();
    uint32_t last_poll=HAL_GetTick()-1u, last_feed=HAL_GetTick();
    for (;;) {
        uint32_t now=HAL_GetTick();
        if (now-last_poll>=1u) { last_poll=now; board_pwm_scope_poll(); }
        if (now-last_feed>=20u) { last_feed=now; board_watchdog_feed(); }
    }
#else
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
#endif
}
