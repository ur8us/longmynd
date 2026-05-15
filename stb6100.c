/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stb6100.c                                                                   */
/*    - STB6100 tuner support for older Eardatek MiniTiouner NIMs                                     */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Portions are based on the Linux STB6100 frontend driver:
    Copyright (C) Manu Abraham and ST Microelectronics, SPDX-License-Identifier: GPL-2.0-or-later.
*/

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "errors.h"
#include "nim.h"
#include "stb6100.h"

#define STB6100_LD          0x00
#define STB6100_LD_LOCK     0x01
#define STB6100_VCO         0x01
#define STB6100_NI          0x02
#define STB6100_NF_LSB      0x03
#define STB6100_K           0x04
#define STB6100_G           0x05
#define STB6100_F           0x06
#define STB6100_DLB         0x07
#define STB6100_TEST1       0x08
#define STB6100_FCCK        0x09
#define STB6100_LPEN        0x0a
#define STB6100_TEST3       0x0b

#define STB6100_VCO_OSCH        0x80
#define STB6100_VCO_OCK         0x60
#define STB6100_VCO_ODIV_SHIFT  4
#define STB6100_VCO_OSM         0x0f
#define STB6100_K_PSD2          0x04
#define STB6100_K_PSD2_SHIFT    2
#define STB6100_K_NF_MSB        0x03
#define STB6100_G_G             0x0f
#define STB6100_G_GCT           0xe0
#define STB6100_FCCK_FCCK       0x40

typedef struct {
    uint32_t low;
    uint32_t high;
    uint8_t reg;
} stb6100_lkup_t;

static const stb6100_lkup_t stb6100_lkup[] = {
    {       0,  950000, 0x0a },
    {  950000, 1000000, 0x0a },
    { 1000000, 1075000, 0x0c },
    { 1075000, 1200000, 0x00 },
    { 1200000, 1300000, 0x01 },
    { 1300000, 1370000, 0x02 },
    { 1370000, 1470000, 0x04 },
    { 1470000, 1530000, 0x05 },
    { 1530000, 1650000, 0x06 },
    { 1650000, 1800000, 0x08 },
    { 1800000, 1950000, 0x0a },
    { 1950000, 2150000, 0x0c },
    { 2150000, 9999999, 0x0c },
    {       0,       0, 0x00 }
};

static uint8_t stb6100_write_reg(uint8_t reg, uint8_t val) {
    return nim_write_tuner(reg, val);
}

static uint8_t stb6100_read_reg(uint8_t reg, uint8_t *val) {
    return nim_read_tuner_addr(reg, val);
}

uint8_t stb6100_read_lock(uint8_t *locked) {
    uint8_t err;
    uint8_t val = 0;

    err = stb6100_read_reg(STB6100_LD, &val);
    *locked = (val & STB6100_LD_LOCK) ? 1 : 0;

    return err;
}

uint8_t stb6100_set_bandwidth(uint32_t bandwidth) {
    uint8_t err=ERROR_NONE;
    uint32_t tmp;

    bandwidth /= 2; /* zero-IF bandwidth */

    if (bandwidth >= 36000000) {
        tmp = 31;
    } else if (bandwidth <= 5000000) {
        tmp = 0;
    } else {
        tmp = (bandwidth + 500000) / 1000000 - 5;
    }

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_FCCK, 0x0d | STB6100_FCCK_FCCK);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_F, 0xc0 | (uint8_t)tmp);
    usleep(5 * 1000);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_FCCK, 0x0d);
    usleep(10 * 1000);

    return err;
}

uint8_t stb6100_set_freq(uint32_t freq, uint32_t sr) {
    uint8_t err=ERROR_NONE;
    uint8_t g;
    uint8_t psd2;
    uint8_t odiv;
    uint32_t fvco;
    uint32_t nint;
    uint32_t nfrac;
    const stb6100_lkup_t *ptr;

    printf("Flow: STB6100 set freq\n");

    if (freq < 950000 || freq > 2150000) {
        printf("ERROR: STB6100 frequency must be 950000-2150000 KHz for Eardatek\n");
        return ERROR_ARGS_INPUT;
    }

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_FCCK, 0x4d | STB6100_FCCK_FCCK);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_LPEN, 0xeb);

    if (freq <= 1075000) {
        odiv = 1;
    } else {
        odiv = 0;
    }

    for (ptr = stb6100_lkup; ptr->high != 0; ptr++) {
        if (freq >= ptr->low && freq < ptr->high) {
            break;
        }
    }
    if (ptr->high == 0) {
        printf("ERROR: STB6100 frequency lookup failed\n");
        return ERROR_ARGS_INPUT;
    }

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_VCO,
        (uint8_t)((0xe0 | (odiv << STB6100_VCO_ODIV_SHIFT)) & ~STB6100_VCO_OSM) | ptr->reg);

    if ((freq > 1075000) && (freq <= 1325000)) {
        psd2 = 0;
    } else {
        psd2 = 1;
    }

    fvco = freq << (1 + odiv);
    nint = fvco / (STB6100_TUNER_XTAL << psd2);
    nfrac = (uint32_t)((((uint64_t)(fvco - (nint * (STB6100_TUNER_XTAL << psd2)))) << (9 - psd2))
        + (STB6100_TUNER_XTAL / 2)) / STB6100_TUNER_XTAL;

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_NI, (uint8_t)nint);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_NF_LSB, (uint8_t)nfrac);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_K,
        (uint8_t)(((0x38 & ~STB6100_K_PSD2) | (psd2 << STB6100_K_PSD2_SHIFT)) |
        ((nfrac >> 8) & STB6100_K_NF_MSB)));

    if (sr >= 15000) {
        g = 9;
    } else if (sr >= 5000) {
        g = 11;
    } else {
        g = 14;
    }

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_G,
        (uint8_t)(((0x10 & ~STB6100_G_G) | g) & ~STB6100_G_GCT) | 0x20);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_DLB, 0xcc);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_TEST1, 0x8f);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_TEST3, 0xde);
    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_LPEN, 0xfb);
    usleep(2 * 1000);

    if (err==ERROR_NONE) {
        uint8_t vco = (uint8_t)((0xe0 | (odiv << STB6100_VCO_ODIV_SHIFT)) & ~STB6100_VCO_OSM) | ptr->reg;
        vco &= ~STB6100_VCO_OCK;
        err = stb6100_write_reg(STB6100_VCO, vco);
        usleep(10 * 1000);
        vco &= ~STB6100_VCO_OSCH;
        vco |= STB6100_VCO_OCK;
        err = stb6100_write_reg(STB6100_VCO, vco);
    }

    if (err==ERROR_NONE) err = stb6100_write_reg(STB6100_FCCK, 0x0d);
    usleep(10 * 1000);

    if (err!=ERROR_NONE) printf("ERROR: STB6100 set freq\n");

    return err;
}

uint8_t stb6100_init(uint32_t freq, uint32_t sr) {
    uint8_t err=ERROR_NONE;
    uint32_t bandwidth;
    uint8_t locked = 0;

    printf("Flow: STB6100 init\n");

    bandwidth = (sr * 135 / 100) * 1000;
    if (bandwidth < 5000000) {
        bandwidth = 5000000;
    }

    if (err==ERROR_NONE) err = stb6100_set_bandwidth(bandwidth);
    if (err==ERROR_NONE) err = stb6100_set_freq(freq, sr);
    if (err==ERROR_NONE) err = stb6100_read_lock(&locked);

    if (err==ERROR_NONE) {
        printf("      Status: STB6100 lock=%s\n", locked ? "yes" : "no");
    }
    if (err!=ERROR_NONE) printf("ERROR: STB6100 init\n");

    return err;
}
