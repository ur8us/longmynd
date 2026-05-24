#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ts_stats.h"

#define ASSERT_EQ_U32(actual, expected) \
    do { \
        uint32_t actual_value = (actual); \
        uint32_t expected_value = (expected); \
        if (actual_value != expected_value) { \
            fprintf(stderr, "%s:%d: expected %u, got %u\n", __FILE__, __LINE__, expected_value, actual_value); \
            return 1; \
        } \
    } while (0)

static void make_packet(uint8_t *packet, uint16_t pid)
{
    memset(packet, 0x47, LONGMYND_TS_PACKET_SIZE);
    packet[0] = LONGMYND_TS_HEADER_SYNC;
    packet[1] = 0x40 | ((pid >> 8) & 0x1F);
    packet[2] = pid & 0xFF;
    packet[3] = 0x10;
}

int main(void)
{
    uint8_t stream[LONGMYND_TS_PACKET_SIZE * 3];
    uint32_t pid_counts[LONGMYND_TS_MAX_PID];
    longmynd_ts_packet_stats_t stats = {0, 0};

    memset(pid_counts, 0, sizeof(pid_counts));
    make_packet(&stream[0], 0x0100);
    make_packet(&stream[LONGMYND_TS_PACKET_SIZE], 0x1FFF);
    make_packet(&stream[LONGMYND_TS_PACKET_SIZE * 2], 0x0101);

    longmynd_ts_packet_stats_update(stream, sizeof(stream), pid_counts, &stats);

    ASSERT_EQ_U32(stats.total, 3);
    ASSERT_EQ_U32(stats.nulls, 1);
    ASSERT_EQ_U32(pid_counts[0x0100], 1);
    ASSERT_EQ_U32(pid_counts[0x0101], 1);
    ASSERT_EQ_U32(pid_counts[0x1FFF], 1);
    ASSERT_EQ_U32(longmynd_ts_bitrate_bps(294, 1000), 442176);
    ASSERT_EQ_U32(longmynd_ts_bitrate_bps(0, 0), 0);

    stats = (longmynd_ts_packet_stats_t){0, 0};
    longmynd_ts_packet_stats_update(stream, sizeof(stream) - 1, NULL, &stats);
    ASSERT_EQ_U32(stats.total, 2);
    ASSERT_EQ_U32(stats.nulls, 1);

    stream[LONGMYND_TS_PACKET_SIZE] = 0x00;
    stats = (longmynd_ts_packet_stats_t){0, 0};
    memset(pid_counts, 0, sizeof(pid_counts));
    longmynd_ts_packet_stats_update(stream, sizeof(stream), pid_counts, &stats);
    ASSERT_EQ_U32(stats.total, 2);
    ASSERT_EQ_U32(stats.nulls, 0);
    ASSERT_EQ_U32(pid_counts[0x0100], 1);
    ASSERT_EQ_U32(pid_counts[0x0101], 1);
    ASSERT_EQ_U32(pid_counts[0x1FFF], 0);

    return 0;
}
