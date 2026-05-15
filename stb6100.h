/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stb6100.h                                                                   */
/*    - STB6100 tuner support for older Eardatek MiniTiouner NIMs                                     */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Portions are based on the Linux STB6100 frontend driver:
    Copyright (C) Manu Abraham and ST Microelectronics, SPDX-License-Identifier: GPL-2.0-or-later.
*/

#ifndef STB6100_H
#define STB6100_H

#include <stdint.h>

#define STB6100_TUNER_XTAL 27000 /* in KHz */

uint8_t stb6100_init(uint32_t freq, uint32_t sr);
uint8_t stb6100_set_freq(uint32_t freq, uint32_t sr);
uint8_t stb6100_set_bandwidth(uint32_t bandwidth);
uint8_t stb6100_read_lock(uint8_t *locked);

#endif
