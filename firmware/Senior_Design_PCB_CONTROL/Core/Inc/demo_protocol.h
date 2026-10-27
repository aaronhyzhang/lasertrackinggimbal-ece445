#ifndef DEMO_PROTOCOL_H
#define DEMO_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
typedef enum { NET_POSITION, NET_HEARTBEAT, NET_STATUS, NET_DISABLE } net_operation_t;
typedef struct { net_operation_t operation; uint32_t session, sequence, millidegrees; } net_command_t;
bool protocol_u32(const char *s,uint32_t *out);
/* Strict ASCII: G1 session sequence POSITION mdeg|HEARTBEAT|STATUS|DISABLE */
bool protocol_parse(const void *bytes,size_t length,net_command_t *out);
bool protocol_newer(uint32_t value,uint32_t previous);
#endif
