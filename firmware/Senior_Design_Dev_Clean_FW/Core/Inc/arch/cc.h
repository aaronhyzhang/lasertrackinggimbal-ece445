#ifndef GIMBAL_LWIP_ARCH_CC_H
#define GIMBAL_LWIP_ARCH_CC_H
#include <stdint.h>
#include <stdlib.h>
#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif
/* Host tests use Darwin's existing byte-order names, plus lwip_ prefixed APIs. */
#if defined(__APPLE__)
#define LWIP_DONT_PROVIDE_BYTEORDER_FUNCTIONS 1
#endif
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_FIELD(x) x
void Error_Handler(void);
#define LWIP_PLATFORM_ASSERT(message) do { (void)(message); Error_Handler(); } while (0)
#define LWIP_PLATFORM_DIAG(message) do { } while (0)
#endif
