/* Actual dual-axis application: calibration restore at boot, save on change,
 * FORGET, and HOME through physical S1. Board, encoders and flash are scripted;
 * the flash log is an in-memory sector read by the real cal_store.c. */
#include "demo_app.c"
#include <assert.h>
#include <stdlib.h>
unsigned board_active_axis;volatile unsigned pcb_current_axis;
void board_select_axis(unsigned i){board_active_axis=i;}
static uint32_t us;static uint32_t mask;static bool arm,stop;
static float coord[2];             /* Simulated shaft positions, mechanical coordinate (deg). */
static const float stop_deg=0.0f;  /* Pan hard stop. */
#define PAN_BASE 1500.0f           /* Pan encoder count at coordinate 0 (sign -1). */
#define TILT_BASE 700.0f
static uint16_t raw_of(unsigned axis,float deg) {
    float raw=axis?PAN_BASE-deg*4096.0f/360.0f:TILT_BASE+deg*4096.0f/360.0f;
    return (uint16_t)(((int32_t)lroundf(raw))&4095);
}
uint32_t HAL_GetTick(void) { return us/1000; }
uint32_t board_us(void) { return us; }
uint32_t board_lock(void) { uint32_t previous=mask;mask=1;return previous; }
void board_unlock(uint32_t m) { mask=m; }
bool board_arm_pressed(void) { return arm; }
bool board_stop_pressed(void) { return stop; }
void board_kill(void) {}
void board_release_stop(void) {}
void board_trip(motor_fault_t f) { (void)f; }
void board_inputs(motor_input_t *in,uint16_t adc[2]) {
    adc[0]=32768;adc[1]=0;in->driver_good=in->adc_valid=in->current_zero_ready=true;in->adc_ms=HAL_GetTick();
}
void pcb_control_read_axis(unsigned axis,control_input_t *in) {
    in->now_us=in->encoder_us=in->current_us=us;in->now_ms=us/1000;in->current_sequence=us/50+1;
    in->encoder_raw=raw_of(axis,coord[axis]);in->encoder_status=0x33;
    in->encoder_transport=in->current_valid=in->zero_ready=in->analog_valid=in->temperature_valid=true;
    in->bus_v=24;in->temperature_c=25;
}
void pcb_control_read(control_input_t *in) { pcb_control_read_axis(0,in); }
void board_current_pwm(const float duty[3]) { (void)duty; }
void board_apply(const motor_output_t *out) { (void)out; }
void board_adc_tick(void) {}
void board_service(void) {}
void board_watchdog_feed(void) {}
void board_leds(bool a,bool b,bool c,uint32_t n) { (void)a;(void)b;(void)c;(void)n; }
void network_poll(void) {}
const char *network_status(void) { return "TEST"; }
/* Scripted flash: real record format and scan, memory instead of the H7 sector. */
static uint8_t flash[64*CAL_RECORD_BYTES];static size_t flash_next;static unsigned flash_writes;
void cal_flash_init(cal_scan_t *scan) { cal_scan(flash,sizeof(flash),scan);flash_next=scan->next_offset; }
bool cal_flash_write(const uint8_t r[CAL_RECORD_BYTES]) {
    assert(flash_next+CAL_RECORD_BYTES<=sizeof(flash));
    memcpy(flash+flash_next,r,CAL_RECORD_BYTES);flash_next+=CAL_RECORD_BYTES;++flash_writes;return true;
}
void cal_flash_service(void) {}
cal_flash_state_t cal_flash_state(void) { return CAL_FLASH_IDLE; }

static void tick(void) {
    us+=1000;dual_foreground_ms=HAL_GetTick();app_control_tick();
    position_control_t *pan=&dual[1].servo;  /* Pan plant while homing: speed follows voltage. */
    if (pan->state==SERVO_HOMING) coord[1]=fmaxf(stop_deg,coord[1]+40.0f*pan->vq_target_v*0.001f);
}
static void foreground(void) { app_foreground(); }
static void boot(float tilt_deg,float pan_deg) {
    coord[0]=tilt_deg;coord[1]=pan_deg;arm=stop=false;us+=1000000;app_init();
    for (unsigned k=0;k<5;++k) tick();
}
static void store(bool tilt,bool pan_zero,uint16_t pan_zero_raw) {
    memset(flash,0xFF,sizeof(flash));flash_writes=0;
    cal_data_t d={0};d.sequence=41;
    d.axis[0]=(cal_axis_t){tilt,false,1,0,2.0f};
    d.axis[1]=(cal_axis_t){true,pan_zero,1,pan_zero_raw,1.0f};
    uint8_t r[CAL_RECORD_BYTES];cal_pack(&d,r);memcpy(flash,r,sizeof(r));
}
static cal_scan_t read_flash(void) { cal_scan_t s;cal_scan(flash,sizeof(flash),&s);return s; }

static void test_restore_at_boot(void) {
    store(true,true,(uint16_t)PAN_BASE);boot(30.0f,120.0f);
    position_control_t *t=&dual[0].servo,*p=&dual[1].servo;
    assert(t->state==SERVO_READY && t->commutation_calibrated && !t->zero_set && cal_restored[0]==CAL_RESTORE_ELECTRICAL);
    assert(p->state==SERVO_READY && p->zero_set && fabsf(p->position_deg-120.0f)<0.2f && cal_restored[1]==CAL_RESTORE_FULL);
    assert(!cal_dirty);foreground();assert(flash_writes==0);  /* Restoring never rewrites flash. */
    char text[640];app_select_axis(1);app_status_text(text,sizeof(text));
    assert(strstr(text,"stored=2 restored=2 save_pending=0 flash=0 home_supported=1"));
    app_select_axis(0);app_status_text(text,sizeof(text));assert(strstr(text,"stored=1 restored=1") && strstr(text,"home_supported=0"));
    assert(g_system.state==SYS_IDLE);
    /* Empty flash: nothing restored, axes stay OFF until calibrated. */
    memset(flash,0xFF,sizeof(flash));boot(30.0f,120.0f);
    assert(dual[0].servo.state==SERVO_OFF && dual[1].servo.state==SERVO_OFF && !cal_dirty);
    puts("PASS boot restore: pan electrical+zero, tilt electrical only, no flash rewrite");
}

static void test_save_on_change_only_when_idle(void) {
    memset(flash,0xFF,sizeof(flash));flash_writes=0;boot(30.0f,120.0f);
    position_control_t *p=&dual[1].servo;  /* As if CALIBRATE then ZERO just completed. */
    p->commutation_calibrated=true;p->direction=-1;p->electrical_offset=3.0f;position_stop(p);
    p->zero_turns=p->raw_turns;p->zero_set=true;tick();assert(cal_dirty);
    dual[0].servo.state=SERVO_RUNNING;foreground();assert(flash_writes==0 && cal_dirty); /* Busy: wait. */
    dual[0].servo.state=SERVO_OFF;foreground();assert(flash_writes==1 && !cal_dirty);
    cal_scan_t s=read_flash();
    assert(s.found && s.latest.axis[1].electrical && s.latest.axis[1].zero && s.latest.axis[1].direction==-1);
    assert(s.latest.axis[1].zero_raw==raw_of(1,120.0f) && !s.latest.axis[0].electrical);
    foreground();tick();foreground();assert(flash_writes==1);   /* No change, no write. */
    boot(30.0f,120.0f);                                          /* Power cycle: comes back READY at 0. */
    assert(dual[1].servo.state==SERVO_READY && dual[1].servo.zero_set && fabsf(dual[1].servo.position_deg)<0.2f);
    puts("PASS save after calibration/zero, deferred while any axis is active, restored after power cycle");
}

static void test_forget(void) {
    store(true,true,(uint16_t)PAN_BASE);boot(30.0f,120.0f);
    app_select_axis(1);assert(app_position(SOURCE_ETHERNET,150000));
    assert(!app_forget(SOURCE_ETHERNET));                        /* Refused with a queued target. */
    app_stop();tick();assert(app_forget(SOURCE_ETHERNET));
    assert(dual[0].servo.state==SERVO_OFF && dual[1].servo.state==SERVO_OFF && !dual[1].servo.zero_set);
    tick();foreground();cal_scan_t s=read_flash();
    assert(s.found && !s.latest.axis[0].electrical && !s.latest.axis[1].electrical);
    boot(30.0f,120.0f);assert(dual[1].servo.state==SERVO_OFF);   /* Nothing restored any more. */
    puts("PASS FORGET erases stored and RAM calibration, refused while a target is queued");
}

static void home_pan(bool keep_lease) {
    app_select_axis(1);assert(app_home(SOURCE_ETHERNET));
    assert(!app_position(SOURCE_ETHERNET,150000));               /* No targets while homing is queued. */
    app_select_axis(0);assert(!app_calibrate(SOURCE_ETHERNET));
    app_select_axis(1);
    for (unsigned k=0;k<15000 && (dual[1].home || dual[1].homing || dual[1].servo.state==SERVO_READY);++k) {
        if (keep_lease && k%100==0) (void)app_heartbeat(SOURCE_ETHERNET);
        if (k==100) { arm=false; }
        if (k==150) arm=true;
        if (k==200) arm=false;
        tick();
        if (k>200 && !dual[1].home && !dual[1].homing) break;
    }
}
static void test_home(void) {
    store(true,true,(uint16_t)PAN_BASE);boot(30.0f,120.0f);
    home_pan(true);
    position_control_t *p=&dual[1].servo;
    assert(p->state==SERVO_READY && p->zero_set && fabsf(coord[1]-stop_deg)<0.01f && fabsf(p->position_deg)<0.2f);
    assert(dual[0].servo.state==SERVO_READY);                    /* Tilt never moved. */
    tick();foreground();assert(flash_writes==0);                 /* Same zero as stored: nothing to save. */
    puts("PASS HOME: queued over the network, started by S1, stall at the stop, matches stored zero");

    /* Stored zero 10 degrees away from the real stop: homed zero is refused. */
    store(true,true,(uint16_t)(PAN_BASE-10.0f*4096/360));boot(30.0f,120.0f);
    home_pan(true);
    assert(dual[1].servo.state==SERVO_FAULT && dual[1].servo.fault==SERVO_CALIBRATION);
    tick();assert(g_system.state==SYS_FAULT);
    puts("PASS homed zero far from the stored zero faults instead of being trusted");

    /* No electrical calibration stored: zero is new, so it is saved. */
    store(true,false,0);boot(30.0f,120.0f);flash_writes=0;
    home_pan(true);assert(dual[1].servo.state==SERVO_READY && dual[1].servo.zero_set);
    tick();foreground();assert(flash_writes==1 && read_flash().latest.axis[1].zero_raw==raw_of(1,0.0f));

    /* Queued but S1 never pressed: the request lapses with the lease. */
    store(true,true,(uint16_t)PAN_BASE);boot(30.0f,120.0f);
    app_select_axis(1);assert(app_home(SOURCE_ETHERNET));
    for (unsigned k=0;k<700;++k) tick();
    assert(!dual[1].home && !dual[1].valid && dual[1].servo.state==SERVO_READY);
    app_select_axis(0);assert(!app_home(SOURCE_ETHERNET));       /* Tilt has no configured stop. */
    puts("PASS homed zero saved when new, unpressed HOME lapses, tilt HOME refused");
}

int main(void) {
    us=1000;
    test_restore_at_boot();test_save_on_change_only_when_idle();test_forget();test_home();
    return 0;
}
