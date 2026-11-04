#include "commission.h"
#include "control_config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    position_control_t s;position_init(&s);
    s.state=SERVO_RUNNING;s.commutation_calibrated=s.zero_set=true;s.position_deg=180;
    control_input_t in={0};in.bus_v=24;in.encoder_status=0x33;in.encoder_transport=true;
    in.current_valid=in.zero_ready=in.analog_valid=in.driver_good=true;
    in.command_valid=in.temperature_valid=true;in.temperature_c=25;
    commission_init();
    assert(!commission_prepare(1,1000,10,1000,180000,30000));
    assert(!commission_prepare(0,40000,1000,1000,180000,30000));
    assert(commission_prepare(1,1000,200,2000,180000,30000));
    assert(!commission_input(&in,&s) && in.experiment_mode==1 && in.experiment_value==1);
    in.now_ms=100;assert(!commission_input(&in,&s) && in.experiment_value==-1);
    for(unsigned i=0;i<2100;++i) { in.now_us=i*1000;commission_capture(&in,&s); }
    unsigned char packet[728];size_t used=commission_read(0,packet,sizeof(packet));
    uint32_t header[6];memcpy(header,packet,24);
    assert(used==728 && header[0]==0x31525443 && header[3]==52 && header[4]==8 && header[5]==2100);
    in.now_ms=2000;assert(commission_input(&in,&s) && g_commission.complete);
    commission_abort();assert(!g_commission.authorized);
    assert(commission_prepare(1,1000,200,2000,180000,30000));
    s.position_deg=140;assert(!commission_input(&in,&s) && g_commission.aborted);
    if (!CONTROL_CURRENT_PI_ENABLED) {
        position_init(&s);s.config.run_volts=6;
        assert(fabsf(position_voltage_limit(&s,24)-6)<0.001f);
        assert(fabsf(position_voltage_limit(&s,18)-4.5f)<0.001f);
        s.state=SERVO_RUNNING;s.zero_set=s.unwrap_started=true;s.raw_turns=0.5f;
        s.trajectory_deg=s.position_deg=180;s.previous_raw=2048;
        in=(control_input_t){.now_us=1000,.now_ms=1,.encoder_us=1000,.current_us=1000,
            .encoder_raw=2048,.encoder_status=0x33,.encoder_transport=true,
            .driver_good=true,.zero_ready=true,.current_valid=true,.analog_valid=true,
            .temperature_valid=true,.temperature_c=25,.command_valid=true,.command_ms=1,
            .bus_v=18,.experiment_mode=2,.experiment_value=100};
        s.config.velocity_kp=10;s.config.velocity_ki=100;
        for(unsigned i=0;i<100;++i) {
            in.now_ms=in.command_ms=i+1;in.now_us=in.encoder_us=in.current_us=(i+1)*1000;
            position_tick(&s,&in,SERVO_NO_ACTION);
            assert(s.state==SERVO_RUNNING && s.voltage_saturated);
            assert(fabsf(s.velocity_integral)<0.0001f && fabsf(s.vq_target_v-4.5f)<0.001f);
        }
        in.experiment_value=-100;position_tick(&s,&in,SERVO_NO_ACTION);
        assert(s.vq_target_v<0); /* No winding up against unavailable voltage. */
    }
    puts("PASS local timing/waveform, trace wrap schema, travel abort and true-output anti-windup");
}
