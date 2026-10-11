#ifndef DEMO_LWIP_CC_H
#define DEMO_LWIP_CC_H
#include <stdint.h>
#include <stdlib.h>
#define BYTE_ORDER LITTLE_ENDIAN
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x
void Error_Handler(void);
#define LWIP_PLATFORM_ASSERT(x) do { (void)(x); Error_Handler(); } while (0)
#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#endif
