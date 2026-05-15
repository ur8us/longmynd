/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stv0903.c                                                                   */
/*    - STV0903 demodulator support for older Eardatek MiniTiouner NIMs                               */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Portions are based on the Linux STV0900/0903 frontend driver:
    Copyright (C) Manu Abraham and ST Microelectronics, SPDX-License-Identifier: GPL-2.0-or-later.
*/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "errors.h"
#include "stv0903.h"
#include "stv0910.h"
#include "stv0910_regs.h"
#include "stv0910_utils.h"

#define RSTV0903_P1_TNRCFG      0xf4e0
#define RSTV0903_P1_PDELCTRL1   0xf550
#define RSTV0903_P1_F22TX       0xf1a9
#define RSTV0903_P1_F22RX       0xf1aa

#define STV0903_STOP_CLKPKDT1   0x20
#define STV0903_STOP_CLKFEC     0x10
#define STV0903_STOP_CLKADCI1   0x02
#define STV0903_STOP_CLKSAMP1   0x08
#define STV0903_STOP_CLKVIT1    0x02
#define STV0903_STOP_CLKTS      0x01
#define STV0903_ADC1_PON        0x02
#define STV0903_DISEQC1_PON     0x20
#define STV0903_STANDBY         0x80
#define STV0903_BYPASSPLLCORE   0x40
#define STV0903_SELX1RATIO      0x20
#define STV0903_M_DIV           0xff
#define STV0903_TSFIFO_SERIAL   0x40
#define STV0903_TSFIFO_DVBCI    0x80
#define STV0903_RST_HWARE       0x01
#define STV0903_FRESFEC         0x10
#define STV0903_ALGOSWRST       0x01
#define STV0903_DVBS2_COLD_SEARCH 0x81

static uint32_t stv0903_mclk_hz = STV0903_MCLK;
static uint32_t stv0903_sr_khz = 0;

typedef struct {
    uint16_t reg;
    uint8_t val;
} regval_t;

static const regval_t stv0903_initval[] = {
    { 0xf11c, 0x00 },
    { 0xf152, 0x11 },
    { 0xf1c2, 0x48 },
    { 0xf1c3, 0x14 },
    { 0xf1e0, 0x27 },
    { 0xf1e1, 0x21 },
    { 0xf1a0, 0x22 },
    { 0xf1a9, 0xc0 },
    { 0xf1aa, 0xc0 },
    { 0xf1a1, 0x00 },
    { 0xf414, 0xf9 },
    { 0xf410, 0x08 },
    { 0xf41e, 0xc4 },
    { 0xf43d, 0xed },
    { 0xf4e1, 0x82 },
    { 0xf43f, 0xd0 },
    { 0xf440, 0xb8 },
    { 0xf450, 0xd2 },
    { 0xf453, 0x20 },
    { 0xf454, 0x00 },
    { 0xf455, 0xf0 },
    { 0xf456, 0x70 },
    { 0xf574, 0x20 },
    { 0xf5a0, 0x88 },
    { 0xf5a2, 0x3a },
    { 0xf5a8, 0x00 },
    { 0xf5b2, 0x10 },
    { 0xf598, 0x35 },
    { 0xf59c, 0xc1 },
    { 0xf441, 0xf8 },
    { 0xf401, 0x1c },
    { 0xf417, 0x20 },
    { 0xf420, 0x70 },
    { 0xf421, 0x88 },
    { 0xf42c, 0x5b },
    { 0xf42d, 0x38 },
    { 0xf438, 0xe4 },
    { 0xf439, 0x1a },
    { 0xf43a, 0x09 },
    { 0xf43e, 0x08 },
    { 0xf458, 0xc1 },
    { 0xf459, 0x58 },
    { 0xf45a, 0x01 },
    { 0xf490, 0x26 },
    { 0xf49c, 0x86 },
    { 0xf49d, 0x86 },
    { 0xf500, 0x77 },
    { 0xf501, 0x85 },
    { 0xf502, 0x77 },
    { 0xf415, 0x3b },
    { 0xf4b0, 0xff },
    { 0xf4b1, 0xff },
    { 0xf4b2, 0xff },
    { 0xf4b3, 0xff },
    { 0xf4b4, 0xff },
    { 0xf4b5, 0xff },
    { 0xf4b6, 0xff },
    { 0xf4b7, 0xcc },
    { 0xf4b8, 0xcc },
    { 0xf4b9, 0xcc },
    { 0xf4ba, 0xcc },
    { 0xf4bb, 0xcc },
    { 0xf4bc, 0xcc },
    { 0xf4bd, 0xcc },
    { 0xf4be, 0xcc },
    { 0xf4bf, 0xcf },
    { 0xfa86, 0x1c },
    { 0xfa03, 0x37 },
    { 0xfa04, 0x29 },
    { 0xfa05, 0x37 },
    { 0xfa06, 0x33 },
    { 0xfa07, 0x31 },
    { 0xfa08, 0x2f },
    { 0xfa09, 0x39 },
    { 0xfa0a, 0x3a },
    { 0xfa0b, 0x29 },
    { 0xfa0c, 0x37 },
    { 0xfa0d, 0x33 },
    { 0xfa0e, 0x2f },
    { 0xfa0f, 0x39 },
    { 0xfa10, 0x3a },
    { 0xfa3f, 0x04 },
    { 0xfa43, 0x0c },
    { 0xfa44, 0x0f },
    { 0xfa45, 0x11 },
    { 0xfa46, 0x14 },
    { 0xfa47, 0x17 },
    { 0xfa48, 0x19 },
    { 0xfa49, 0x20 },
    { 0xfa4a, 0x21 },
    { 0xfa4b, 0x0d },
    { 0xfa4c, 0x0f },
    { 0xfa4d, 0x13 },
    { 0xfa4e, 0x1a },
    { 0xfa4f, 0x1f },
    { 0xfa50, 0x21 },
    { 0xf600, 0x20 },
    { 0xf533, 0x01 },
    { 0xf53c, 0x2f },
};

static const regval_t stv0903_cut20_val[] = {
    { 0xf41e, 0xe8 },
    { 0xf41f, 0x10 },
    { 0xf43d, 0x38 },
    { 0xf43e, 0x20 },
    { 0xf458, 0x5a },
    { 0xf500, 0x06 },
    { 0xf501, 0x00 },
    { 0xf502, 0x04 },
    { 0xf401, 0x0c },
    { 0xfa43, 0x21 },
    { 0xfa44, 0x21 },
    { 0xfa45, 0x20 },
    { 0xfa46, 0x1f },
    { 0xfa47, 0x1e },
    { 0xfa48, 0x1e },
    { 0xfa49, 0x1d },
    { 0xfa4a, 0x1b },
    { 0xfa4b, 0x20 },
    { 0xfa4c, 0x20 },
    { 0xfa4d, 0x20 },
    { 0xfa4e, 0x20 },
    { 0xfa4f, 0x20 },
    { 0xfa50, 0x21 },
};

static uint8_t stv0903_update_reg(uint16_t reg, uint8_t mask, uint8_t val) {
    uint8_t err;
    uint8_t old_val = 0;

    err = stv0910_read_reg(reg, &old_val);
    if (err==ERROR_NONE) err = stv0910_write_reg(reg, (old_val & ~mask) | (val & mask));

    return err;
}

static uint8_t stv0903_set_mclk(void) {
    uint8_t err=ERROR_NONE;
    uint8_t reg=0;
    uint8_t ratio;
    uint8_t div;
    uint32_t mclk;

    printf("Flow: STV0903 set MCLK\n");

    if (err==ERROR_NONE) err = stv0910_read_reg(RSTV0910_SYNTCTRL, &reg);
    ratio = (reg & STV0903_SELX1RATIO) ? 4 : 6;
    div = (uint8_t)(((ratio * STV0903_MCLK) / STV0903_DEMOD_XTAL) - 1);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_NCOARSE, STV0903_M_DIV, div);

    mclk = ((uint32_t)div + 1) * STV0903_DEMOD_XTAL / ratio;
    stv0903_mclk_hz = mclk;
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0903_P1_F22TX, (uint8_t)(mclk / 704000));
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0903_P1_F22RX, (uint8_t)(mclk / 704000));

    if (err!=ERROR_NONE) printf("ERROR: STV0903 set MCLK\n");

    return err;
}

static uint16_t stv0903_srate_reg(uint32_t sr_khz, uint32_t percent) {
    uint64_t srate_hz = (uint64_t)sr_khz * 1000U * percent / 100U;

    return (uint16_t)((srate_hz * 65536U) / stv0903_mclk_hz);
}

static uint8_t stv0903_setup_low_sr_scan(uint32_t sr_khz) {
    uint8_t err=ERROR_NONE;
    uint16_t sym;

    if (sr_khz <= 5000) {
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARCFG, 0x44);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRUP1, 0x0f);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRUP0, 0xff);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRLOW1, 0xf0);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRLOW0, 0x00);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_RTCS2, 0x68);
    } else {
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARCFG, 0xc4);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_RTCS2, 0x44);
    }

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDT0M, 0x20);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_TMGCFG, 0xd2);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CORRELMANT, sr_khz < 2000 ? 0x63 : 0x70);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_AGC2REF, 0x38);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_KREFTMG, 0x5a);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_TMGCFG2, 0xc1);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRINIT1, 0x00);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CFRINIT0, 0x00);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_EQUALCFG, 0x41);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_FFECFG, 0x41);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRSTEP, 0x00);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_TMGTHRISE, 0xe0);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_TMGTHFALL, 0xc0);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDCFG2, 0x3b);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_RTC, 0x88);

    if (sr_khz < 2000) {
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARFREQ, 0x39);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARHDR, 0x40);
    } else if (sr_khz < 10000) {
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARFREQ, 0x4c);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARHDR, 0x20);
    } else {
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARFREQ, 0x4b);
        if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CARHDR, 0x20);
    }

    sym = stv0903_srate_reg(sr_khz, 105);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRUP1, (uint8_t)((sym >> 8) & 0x7f));
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRUP0, (uint8_t)(sym & 0xff));
    sym = stv0903_srate_reg(sr_khz, 95);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRLOW1, (uint8_t)((sym >> 8) & 0x7f));
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRLOW0, (uint8_t)(sym & 0xff));
    sym = stv0903_srate_reg(sr_khz, 100);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRINIT1, (uint8_t)(sym >> 8));
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_SFRINIT0, (uint8_t)(sym & 0xff));

    return err;
}

static uint8_t stv0903_init_regs(void) {
    uint8_t err=ERROR_NONE;
    uint8_t val=0;
    uint16_t i;

    printf("Flow: STV0903 init regs\n");

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDISTATE, 0x5c);
    usleep(5 * 1000);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0903_P1_TNRCFG, 0x6c);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_I2CRPT, 0x38);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_NCOARSE, 0x13);
    usleep(5 * 1000);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_I2CCFG, 0x08);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_SYNTCTRL, 0x22);
    usleep(5 * 1000);

    for (i=0; err==ERROR_NONE && i<(sizeof(stv0903_initval)/sizeof(stv0903_initval[0])); i++) {
        err = stv0910_write_reg(stv0903_initval[i].reg, stv0903_initval[i].val);
    }

    if (err==ERROR_NONE) err = stv0910_read_reg(RSTV0910_MID, &val);
    if (err==ERROR_NONE) {
        printf("      Status: STV0903 MID = 0x%.2x\n", val);
        if (val == 0x51) {
            printf("ERROR: STV0910/Serit demod detected while Eardatek mode was selected\n");
            err = ERROR_DEMOD_INIT;
        } else if (val < 0x20) {
            printf("ERROR: unsupported STV0903 cut 0x%.2x\n", val);
            err = ERROR_DEMOD_INIT;
        }
    }

    for (i=0; err==ERROR_NONE && i<(sizeof(stv0903_cut20_val)/sizeof(stv0903_cut20_val[0])); i++) {
        err = stv0910_write_reg(stv0903_cut20_val[i].reg, stv0903_cut20_val[i].val);
    }

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_TSTRES0, 0x80);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_TSTRES0, 0x00);

    return err;
}

static uint8_t stv0903_wakeup(void) {
    uint8_t err=ERROR_NONE;

    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_SYNTCTRL, STV0903_STANDBY, 0x00);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_TSTTNR1, STV0903_ADC1_PON, STV0903_ADC1_PON);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_TSTTNR2, STV0903_DISEQC1_PON, STV0903_DISEQC1_PON);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_STOPCLK1,
        STV0903_STOP_CLKPKDT1 | STV0903_STOP_CLKADCI1 | STV0903_STOP_CLKFEC, 0x00);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_STOPCLK2,
        STV0903_STOP_CLKSAMP1 | STV0903_STOP_CLKVIT1 | STV0903_STOP_CLKTS, 0x00);

    return err;
}

static uint8_t stv0903_ldpc_single(void) {
    uint8_t err=ERROR_NONE;
    uint8_t reg=0;
    uint8_t i;
    static const uint8_t modcod_single[16] = {
        0xff, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f
    };

    for (i=0; err==ERROR_NONE && i<16; i++) {
        err = stv0910_write_reg((uint16_t)(RSTV0910_P1_MODCODLST0 + i), 0xff);
    }
    for (i=0; err==ERROR_NONE && i<16; i++) {
        err = stv0910_write_reg((uint16_t)(RSTV0910_P1_MODCODLST0 + i), modcod_single[i]);
    }

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_GENCFG, 0x04);
    if (err==ERROR_NONE) err = stv0910_read_reg(RSTV0910_TSTRES0, &reg);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_TSTRES0, reg | STV0903_FRESFEC);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_TSTRES0, reg & ~STV0903_FRESFEC);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0903_P1_PDELCTRL1, STV0903_ALGOSWRST, STV0903_ALGOSWRST);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0903_P1_PDELCTRL1, STV0903_ALGOSWRST, 0x00);

    return err;
}

static uint8_t stv0903_setup_s2_search(void) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 setup DVB-S2 search\n");

    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_STOPCLK2,
        STV0903_STOP_CLKVIT1, STV0903_STOP_CLKVIT1);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_ACLC, 0x1a);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_BCLC, 0x09);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CAR2CFG, 0x26);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH12, 0xd0);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH23, 0x7d);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH34, 0x53);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH56, 0x2f);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH67, 0x24);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_VTH78, 0x1f);

    if (err!=ERROR_NONE) printf("ERROR: STV0903 setup DVB-S2 search\n");

    return err;
}

uint8_t stv0903_start_s2_scan(void) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 start DVB-S2 scan\n");

    if (err==ERROR_NONE) err = stv0903_setup_low_sr_scan(stv0903_sr_khz);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CORRELABS, stv0903_sr_khz > 5000 ? 0x9e : 0x82);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDCFGMD, STV0903_DVBS2_COLD_SEARCH);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDISTATE, 0x1f);
    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_DMDISTATE, STV0910_SCAN_BLIND_BEST_GUESS);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0903_P1_PDELCTRL1, STV0903_ALGOSWRST, STV0903_ALGOSWRST);

    if (err!=ERROR_NONE) printf("ERROR: STV0903 start DVB-S2 scan\n");

    return err;
}

uint8_t stv0903_s2_lock_setup(void) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 DVB-S2 lock setup\n");

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_P1_CORRELABS, 0x9e);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0903_P1_PDELCTRL1, STV0903_ALGOSWRST, 0x00);
    if (err==ERROR_NONE) err = stv0903_reset_ts();

    if (err!=ERROR_NONE) printf("ERROR: STV0903 DVB-S2 lock setup\n");

    return err;
}

static uint8_t stv0903_setup_ts(void) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 setup TS\n");

    if (err==ERROR_NONE) err = stv0910_write_reg(RSTV0910_TSGENERAL, 0x00);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_P1_TSCFGH,
        STV0903_TSFIFO_SERIAL | STV0903_TSFIFO_DVBCI, STV0903_TSFIFO_DVBCI);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_P1_TSCFGH, STV0903_RST_HWARE, STV0903_RST_HWARE);

    return err;
}

uint8_t stv0903_reset_ts(void) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 reset TS\n");

    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_P1_TSCFGH, STV0903_RST_HWARE, 0x00);
    usleep(3 * 1000);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_P1_TSCFGH, STV0903_RST_HWARE, STV0903_RST_HWARE);
    if (err==ERROR_NONE) err = stv0903_update_reg(RSTV0910_P1_TSCFGH, STV0903_RST_HWARE, 0x00);

    if (err!=ERROR_NONE) printf("ERROR: STV0903 reset TS\n");

    return err;
}

uint8_t stv0903_init(uint32_t sr) {
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0903 init\n");
    stv0903_sr_khz = sr;

    if (err==ERROR_NONE) err = stv0903_init_regs();
    if (err==ERROR_NONE) err = stv0903_set_mclk();
    if (err==ERROR_NONE) err = stv0903_wakeup();
    if (err==ERROR_NONE) err = stv0903_ldpc_single();
    if (err==ERROR_NONE) err = stv0903_setup_s2_search();
    if (err==ERROR_NONE) err = stv0910_setup_equalisers(STV0910_DEMOD_BOTTOM);
    if (err==ERROR_NONE) err = stv0910_setup_carrier_loop(STV0910_DEMOD_BOTTOM);
    if (err==ERROR_NONE) err = stv0910_setup_timing_loop(STV0910_DEMOD_BOTTOM, sr);
    if (err==ERROR_NONE) err = stv0903_setup_ts();

    if (err!=ERROR_NONE) printf("ERROR: STV0903 init\n");

    return err;
}
