#ifndef CAL_STORE_H
#define CAL_STORE_H
/* Persistent calibration record (design doc 2.3.4): electrical commutation and
 * mechanical zero per axis, CRC-checked. HAL-independent; cal_flash.c moves the
 * 64-byte records to and from flash.
 *
 * Electrical calibration (direction, offset) is tied to the encoder reading, so it
 * stays valid across power cycles unless the magnet/encoder is remounted. The
 * mechanical zero is stored as the AS5600 count at 0 degrees. A single-turn
 * encoder can only recover it when exactly one whole-turn choice puts the shaft
 * inside the axis travel window: always true for pan (about 340 degrees of travel
 * between hard stops), not for an axis that can turn freely. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "position_control.h"

#define CAL_RECORD_BYTES 64u
#define CAL_RECORD_MAGIC 0x4C414347u /* "GCAL" little-endian */
#define CAL_RECORD_VERSION 1u
/* Restore the mechanical zero only where travel is known to be under one turn. */
#ifndef CAL_RESTORE_ZERO_TILT
#define CAL_RESTORE_ZERO_TILT 0 /* Tilt mechanical travel not yet confirmed. */
#endif
#ifndef CAL_RESTORE_ZERO_PAN
#define CAL_RESTORE_ZERO_PAN 1  /* Hard stops: about 339.8 degrees of travel. */
#endif
#define CAL_ZERO_MARGIN_DEG 2.0f   /* Allowed reading outside the travel window. */
#define CAL_HOME_TOLERANCE_DEG 3.0f /* Homed zero must match a stored zero this closely. */

typedef struct {
    bool electrical,zero;
    int8_t direction;          /* Commutation direction, +1/-1. */
    uint16_t zero_raw;         /* AS5600 count at mechanical 0 degrees. */
    float electrical_offset;   /* Radians, [0, 2*pi). */
} cal_axis_t;
typedef struct {
    uint32_t sequence;         /* Increments on every saved record. */
    cal_axis_t axis[2];
} cal_data_t;
typedef struct {
    bool found;                /* A valid record for this firmware configuration exists. */
    bool full;                 /* No erased slot left: next write needs a sector erase. */
    size_t next_offset;        /* First erased slot (valid when !full). */
    cal_data_t latest;         /* Highest-sequence valid record. */
} cal_scan_t;
typedef enum { CAL_RESTORE_NONE,CAL_RESTORE_ELECTRICAL,CAL_RESTORE_FULL } cal_restore_t;

uint32_t cal_crc32(const void *data,size_t length);
uint32_t cal_config_id(void); /* Changes when pole pairs, encoder signs or travel change. */
void cal_pack(const cal_data_t *data,uint8_t out[CAL_RECORD_BYTES]);
bool cal_unpack(const uint8_t in[CAL_RECORD_BYTES],cal_data_t *data);
void cal_scan(const uint8_t *sector,size_t size,cal_scan_t *scan);
cal_axis_t cal_capture(const position_control_t *s);
bool cal_axis_same(const cal_axis_t *a,const cal_axis_t *b);
float cal_zero_delta_deg(uint16_t a,uint16_t b); /* Signed shortest difference a-b. */
/* Boot restore into an OFF, uncalibrated servo whose encoder has a valid first sample.
 * Never moves anything; the axis becomes READY. */
cal_restore_t cal_restore(position_control_t *s,const cal_axis_t *a);
#endif
