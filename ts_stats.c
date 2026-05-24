/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: ts_stats.c                                                                  */
/* -------------------------------------------------------------------------------------------------- */

#include "ts_stats.h"

#define TS_PID_NULL 0x1FFF

void longmynd_ts_packet_stats_update(const uint8_t *buffer, uint32_t length, uint32_t *pid_counts, longmynd_ts_packet_stats_t *stats)
{
    if (buffer == 0 || stats == 0) {
        return;
    }

    for (uint32_t offset = 0; offset + LONGMYND_TS_PACKET_SIZE <= length; offset += LONGMYND_TS_PACKET_SIZE) {
        uint32_t pid;

        if (buffer[offset] != LONGMYND_TS_HEADER_SYNC) {
            continue;
        }

        pid = ((uint32_t)(buffer[offset + 1] & 0x1F) << 8) | (uint32_t)buffer[offset + 2];

        stats->total++;
        if (pid == TS_PID_NULL) {
            stats->nulls++;
        }
        if (pid_counts != 0 && pid < LONGMYND_TS_MAX_PID) {
            pid_counts[pid]++;
        }
    }
}

uint32_t longmynd_ts_bitrate_bps(uint32_t packet_count, uint32_t delta_ms)
{
    if (delta_ms == 0) {
        return 0;
    }

    return (uint32_t)(((uint64_t)packet_count * LONGMYND_TS_PACKET_SIZE * 8 * 1000) / delta_ms);
}
