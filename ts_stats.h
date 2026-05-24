/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: ts_stats.h                                                                  */
/* -------------------------------------------------------------------------------------------------- */

#ifndef TS_STATS_H
#define TS_STATS_H

#include <stdint.h>

#include "ts.h"

typedef struct {
    uint32_t total;
    uint32_t nulls;
} longmynd_ts_packet_stats_t;

void longmynd_ts_packet_stats_update(const uint8_t *buffer, uint32_t length, uint32_t *pid_counts, longmynd_ts_packet_stats_t *stats);
uint32_t longmynd_ts_bitrate_bps(uint32_t packet_count, uint32_t delta_ms);

#endif
