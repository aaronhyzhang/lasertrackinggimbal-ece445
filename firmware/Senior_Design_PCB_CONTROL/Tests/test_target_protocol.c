#include "demo_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool parse(const char *text,net_command_t *c) { return protocol_parse(text,strlen(text),c); }

int main(void) {
    net_command_t c;
    assert(parse("G1 7 3 TARGET 1",&c) && c.operation==NET_TARGET && c.session==7 && c.sequence==3 && c.args[0]==1);
    assert(parse("G1 7 4 TARGET 0",&c) && c.operation==NET_TARGET && c.args[0]==0);
    static const char *const bad[]={"G1 7 5 TARGET","G1 7 5 TARGET 2","G1 7 5 TARGET -1","G1 7 5 TARGET 1 1",
        "G1 7 5 TARGET x","G1 7 5 TARGET  1","G1 7 5 target 1","G1 0 5 TARGET 1","G1 7 5 TARGET 4294967297"};
    for (unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) assert(!parse(bad[i],&c));
    /* Existing commands are unchanged. */
    assert(parse("G1 7 6 POSITION 90000",&c) && c.operation==NET_POSITION && c.millidegrees==90000);
    assert(parse("G1 7 7 HEARTBEAT",&c) && c.operation==NET_HEARTBEAT);
    assert(parse("G1 7 8 SET 3 110000",&c) && c.operation==NET_SET && c.args[0]==3 && c.args[1]==110000);
    puts("target_protocol: all tests passed");
    return 0;
}
