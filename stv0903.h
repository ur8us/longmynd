/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stv0903.h                                                                   */
/*    - STV0903 demodulator support for older Eardatek MiniTiouner NIMs                               */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Portions are based on the Linux STV0900/0903 frontend driver:
    Copyright (C) Manu Abraham and ST Microelectronics, SPDX-License-Identifier: GPL-2.0-or-later.
*/

#ifndef STV0903_H
#define STV0903_H

#include <stdbool.h>
#include <stdint.h>

#define STV0903_DEMOD_XTAL 27000000U
#define STV0903_MCLK       135000000U

#define STV0903_LOW_SR_AUTO 0
#define STV0903_LOW_SR_ON   1
#define STV0903_LOW_SR_OFF  2

#define STV0903_LOW_SR_FORCE_ON_KS  180U
#define STV0903_LOW_SR_FORCE_OFF_KS 6500U

bool stv0903_low_sr_effective(uint8_t mode, uint32_t sr);
uint8_t stv0903_init(uint32_t sr, uint8_t low_sr_mode);
uint8_t stv0903_start_s2_scan(void);
uint8_t stv0903_s2_lock_setup(void);
uint8_t stv0903_reset_ts(void);

#endif
