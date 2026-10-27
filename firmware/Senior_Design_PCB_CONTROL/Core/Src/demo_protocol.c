#include "demo_protocol.h"
#include <string.h>
bool protocol_u32(const char *s,uint32_t *out) {
    uint32_t value=0;
    if (!*s) return false;
    for (;*s;++s) {
        if (*s<'0'||*s>'9') return false;
        uint32_t d=(uint32_t)(*s-'0');
        if (value>(UINT32_MAX-d)/10u) return false;
        value=value*10u+d;
    }
    *out=value; return true;
}
bool protocol_newer(uint32_t value,uint32_t previous) {
    uint32_t delta=value-previous; return delta!=0 && delta<0x80000000u;
}
bool protocol_parse(const void *bytes,size_t length,net_command_t *out) {
    if (length==0 || length>=80u) return false;
    char data[80]; memcpy(data,bytes,length); data[length]=0;
    for (size_t i=0;i<length;++i) if (data[i]<32 || data[i]>126) return false;
    char *token[5]={0}; unsigned count=0;
    char *p=data;
    while (*p && count<5u) {
        if (*p==' ') return false;
        token[count++]=p;
        while (*p && *p!=' ') ++p;
        if (*p) { *p++=0; if (!*p) return false; }
    }
    if (*p || count<4 || strcmp(token[0],"G1") ||
        !protocol_u32(token[1],&out->session) || out->session==0 ||
        !protocol_u32(token[2],&out->sequence)) return false;
    out->millidegrees=0;
    if (!strcmp(token[3],"POSITION")) {
        out->operation=NET_POSITION;
        return count==5 && protocol_u32(token[4],&out->millidegrees) && out->millidegrees<=360000u;
    }
    if (count!=4) return false;
    if (!strcmp(token[3],"HEARTBEAT")) out->operation=NET_HEARTBEAT;
    else if (!strcmp(token[3],"STATUS")) out->operation=NET_STATUS;
    else if (!strcmp(token[3],"DISABLE")) out->operation=NET_DISABLE;
    else return false;
    return true;
}
