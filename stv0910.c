/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: stv0910.c                                                                   */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - the demodulator support routines (STV0910)                                                    */
/* Copyright 2019 Heather Lomond                                                                      */
/* -------------------------------------------------------------------------------------------------- */
/*
    This file is part of longmynd.

    Longmynd is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Longmynd is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with longmynd.  If not, see <https://www.gnu.org/licenses/>.
*/

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- INCLUDES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "stv0910.h"
#include "stv0910_regs.h"
#include "stv0910_utils.h"
#include "nim.h"
#include "errors.h"
#include "stv0910_regs_init.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

typedef struct {
    int32_t value;
    uint32_t reg_value;
} stv0910_snr_lookup_t;

static const stv0910_snr_lookup_t stv0910_s2_snr_lookup[] = {
    { -30, 13950 }, { -25, 13580 }, { -20, 13150 }, { -15, 12760 },
    { -10, 12345 }, {  -5, 11900 }, {   0, 11520 }, {   5, 11080 },
    {  10, 10630 }, {  15, 10210 }, {  20,  9790 }, {  25,  9390 },
    {  30,  8970 }, {  35,  8575 }, {  40,  8180 }, {  45,  7800 },
    {  50,  7430 }, {  55,  7080 }, {  60,  6720 }, {  65,  6320 },
    {  70,  6060 }, {  75,  5760 }, {  80,  5480 }, {  85,  5200 },
    {  90,  4930 }, {  95,  4680 }, { 100,  4425 }, { 105,  4210 },
    { 110,  3980 }, { 115,  3765 }, { 120,  3570 }, { 125,  3315 },
    { 130,  3140 }, { 135,  2980 }, { 140,  2820 }, { 145,  2670 },
    { 150,  2535 }, { 160,  2270 }, { 170,  2035 }, { 180,  1825 },
    { 190,  1650 }, { 200,  1485 }, { 210,  1340 }, { 220,  1212 },
    { 230,  1100 }, { 240,  1000 }, { 250,   910 }, { 260,   836 },
    { 270,   772 }, { 280,   718 }, { 290,   671 }, { 300,   635 },
    { 310,   602 }, { 320,   575 }, { 330,   550 }, { 350,   517 },
    { 400,   480 }, { 450,   466 }, { 500,   464 }, { 510,   463 },
};

static const stv0910_snr_lookup_t stv0910_s1_snr_lookup[] = {
    {   0,  9242 }, {   5,  9105 }, {  10,  8950 }, {  15,  8780 },
    {  20,  8566 }, {  25,  8366 }, {  30,  8146 }, {  35,  7908 },
    {  40,  7666 }, {  45,  7405 }, {  50,  7136 }, {  55,  6861 },
    {  60,  6576 }, {  65,  6330 }, {  70,  6048 }, {  75,  5768 },
    {  80,  5492 }, {  85,  5224 }, {  90,  4959 }, {  95,  4709 },
    { 100,  4467 }, { 105,  4236 }, { 110,  4013 }, { 115,  3800 },
    { 120,  3598 }, { 125,  3406 }, { 130,  3225 }, { 135,  3052 },
    { 140,  2889 }, { 145,  2733 }, { 150,  2587 }, { 160,  2318 },
    { 170,  2077 }, { 180,  1862 }, { 190,  1670 }, { 200,  1499 },
    { 210,  1347 }, { 220,  1213 }, { 230,  1095 }, { 240,   992 },
    { 250,   900 }, { 260,   826 }, { 270,   758 }, { 280,   702 },
    { 290,   653 }, { 300,   613 }, { 310,   579 }, { 320,   550 },
    { 330,   526 }, { 350,   490 }, { 400,   445 }, { 450,   430 },
    { 500,   426 }, { 510,   425 },
};

static int32_t stv0910_lookup_snr_x10(const stv0910_snr_lookup_t *table, uint32_t table_len, uint32_t reg_value) {
    uint32_t min = 0;
    uint32_t max = table_len - 1;

    if (reg_value >= table[0].reg_value) {
        return table[0].value;
    }
    if (reg_value <= table[max].reg_value) {
        return table[max].value;
    }

    while ((max - min) > 1) {
        uint32_t mid = (max + min) / 2;
        if ((table[min].reg_value >= reg_value) && (reg_value >= table[mid].reg_value)) {
            max = mid;
        } else {
            min = mid;
        }
    }

    int32_t reg_diff = (int32_t)table[max].reg_value - (int32_t)table[min].reg_value;
    int32_t value = table[min].value;
    if (reg_diff != 0) {
        value += ((int32_t)(reg_value - table[min].reg_value) * (table[max].value - table[min].value)) / reg_diff;
    }
    return value;
}

static uint32_t stv0910_dvbs2_nbch(uint32_t modcod, bool short_frame) {
    static const uint32_t nbch[][2] = {
        {    0,     0 },
        {16200,  3240 },
        {21600,  5400 },
        {25920,  6480 },
        {32400,  7200 },
        {38880,  9720 },
        {43200, 10800 },
        {48600, 11880 },
        {51840, 12600 },
        {54000, 13320 },
        {57600, 14400 },
        {58320, 16000 },
        {43200,  9720 },
        {48600, 10800 },
        {51840, 11880 },
        {54000, 13320 },
        {57600, 14400 },
        {58320, 16000 },
        {43200, 10800 },
        {48600, 11880 },
        {51840, 12600 },
        {54000, 13320 },
        {57600, 14400 },
        {58320, 16000 },
        {48600, 11880 },
        {51840, 12600 },
        {54000, 13320 },
        {57600, 14400 },
        {58320, 16000 },
    };

    if (modcod < (sizeof(nbch) / sizeof(nbch[0]))) {
        return nbch[modcod][short_frame ? 1 : 0];
    }
    return 64800;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_car_freq(uint8_t demod, int32_t *cf) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the current carrier frequency and return it (in Hz)                                          */
/*    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                */
/* car_freq: signed place to store the answer                                                         */
/*   return: error state                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t val_h, val_m, val_l;
    double car_offset_freq;

    /* first off we read in the carrier offset as a signed number */
                           err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ?
                                            RSTV0910_P2_CFR2 : RSTV0910_P1_CFR2, &val_h); /* high byte*/
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? 
                                            RSTV0910_P2_CFR1 : RSTV0910_P1_CFR1, &val_m); /* mid */
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? 
                                            RSTV0910_P2_CFR0 : RSTV0910_P1_CFR0, &val_l); /* low */
    /* since this is a 24 bit signed value, we need to build it as a 24 bit value, shift it up to the top
       to get a 32 bit signed value, then convert it to a double */
    car_offset_freq=(double)(int32_t)((((uint32_t)val_h<<16) + ((uint32_t)val_m<< 8) + ((uint32_t)val_l )) << 8);
    /* carrier offset freq (MHz)= mclk (MHz) * CFR/2^24. But we have the extra 256 in there from the sign shift */
    /* so in Hz we need: */
    car_offset_freq=135000000*car_offset_freq/256.0/256.0/256.0/256.0;

    *cf=(int32_t)car_offset_freq;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read carrier frequency\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_constellation(uint8_t demod, uint8_t *i, uint8_t *q) {
/* -------------------------------------------------------------------------------------------------- */
/* reads an I,Q pair from the constellation monitor registers                                         */
/*     i,q: places to store the results                                                               */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;

                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_ISYMB : RSTV0910_P1_ISYMB, i);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_QSYMB : RSTV0910_P1_QSYMB, q);

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read constellation\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_sr(uint8_t demod, uint32_t *found_sr) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the currently detected symbol rate                                                           */
/* found_sr: place to store the result                                                                */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    double sr;
    uint8_t val_h, val_mu, val_ml, val_l;
    uint8_t err;

                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFR3 : RSTV0910_P1_SFR3, &val_h);  /* high byte */
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFR2 : RSTV0910_P1_SFR2, &val_mu); /* mid upper */
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFR1 : RSTV0910_P1_SFR1, &val_ml); /* mid lower */
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFR0 : RSTV0910_P1_SFR0, &val_l);  /* low byte */
    sr=((uint32_t)val_h  << 24) +
       ((uint32_t)val_mu << 16) +
       ((uint32_t)val_ml <<  8) +
       ((uint32_t)val_l       );
    /* sr (MHz) = ckadc (MHz) * SFR/2^32. So in Symbols per Second we need */
    sr=135000000*sr/256.0/256.0/256.0/256.0;
    *found_sr=(uint32_t)sr;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read symbol rate\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_puncture_rate(uint8_t demod, uint8_t *rate) {
/* -------------------------------------------------------------------------------------------------- */
/* reads teh detected viterbi punctuation rate                                                        */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*   rate: place to store the result                                                                   */
/*         The single byta, n, represents a rate=n/n+1                                                 */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t val;

    err=stv0910_read_reg_field(demod==STV0910_DEMOD_TOP ? FSTV0910_P2_VIT_CURPUN : FSTV0910_P1_VIT_CURPUN, &val);
    switch (val) {
      case STV0910_PUNCTURE_1_2: *rate=1; break;
      case STV0910_PUNCTURE_2_3: *rate=2; break;
      case STV0910_PUNCTURE_3_4: *rate=3; break;
      case STV0910_PUNCTURE_5_6: *rate=5; break;
      case STV0910_PUNCTURE_6_7: *rate=6; break;
      case STV0910_PUNCTURE_7_8: *rate=7; break;
      default: err=ERROR_VITERBI_PUNCTURE_RATE; break;
    }

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read puncture rate\n");

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_agc1_gain(uint8_t demod, uint16_t *agc) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the AGC1 Gain registers in the Demodulator and returns the results                           */
/*  demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                  */
/* agc: place to store the results                                                                    */
/* return: error state                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t agc_low, agc_high;

                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_AGCIQIN0 : RSTV0910_P1_AGCIQIN0, &agc_low);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_AGCIQIN1 : RSTV0910_P1_AGCIQIN1, &agc_high);
    if (err==ERROR_NONE) *agc = ((uint16_t)agc_high << 8) | (uint16_t)agc_low;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read agc1 gain\n");

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_agc2_gain(uint8_t demod, uint16_t *agc) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the AGC2 Gain registers in the Demodulator and returns the results                           */
/*  demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                  */
/* agc: place to store the results                                                                    */
/* return: error state                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t agc_low, agc_high;

                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_AGC2I0 : RSTV0910_P1_AGC2I0, &agc_low);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_AGC2I1 : RSTV0910_P1_AGC2I1, &agc_high);
    if (err==ERROR_NONE) *agc = ((uint16_t)agc_high << 8) | (uint16_t)agc_low;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read agc2 gain\n");

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_power(uint8_t demod, uint8_t *power_i, uint8_t *power_q) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the power registers in the Demodulator and returns the results                               */
/*  demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/* power_i, power_q: places to store the results                                                      */
/* return: error state                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;

    /*power=1/4.ADC */
                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_POWERI : RSTV0910_P1_POWERI, power_i);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_POWERQ : RSTV0910_P1_POWERQ, power_q);

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read power\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_err_rate(uint8_t demod, uint32_t *vit_errs) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the viterbi error rate registers                                                             */
/*    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                */
/* vit_errs: place to store the result                                                                */
/*   return: error state                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t val;

    err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_VERROR : RSTV0910_P1_VERROR, &val);
    /* 0=perfect, 0xff=6.23 %errors (errs/4096) */
    /* note there is a problem in the datasheet here as it says 255/2048=6.23% */
    /* to report an integer we will report in 100 * the percentage, so 623=6.23% */
    /* also want to round up to the nearest integer just to be pedantic */
    *vit_errs=((((uint32_t)val)*100000/4096)+5)/10;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read viterbi error rate\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_ber(uint8_t demod, bool dvbs2, uint32_t *ber) {
/* -------------------------------------------------------------------------------------------------- */
/* reads the number of bytes processed by the FEC, the number of error bits and then calculates BER   */
/*    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                */
/*    dvbs2: true when reading DVB-S2 pre-BCH errors, false for legacy DVB-S FBER                     */
/*      ber: place to store the result                                                                */
/*   return: error state                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t high, mid_u, mid_m, mid_l, low;
    double cpt;
    double errs;

    if (dvbs2) {
        uint8_t ctrl;
        uint32_t modcod;
        bool short_frame;
        bool pilots;
        uint32_t nbch;
        uint32_t scale;
        uint64_t denominator;

                             err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_ERRCNT12 : RSTV0910_P1_ERRCNT12, &high);
        if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_ERRCNT11 : RSTV0910_P1_ERRCNT11, &mid_m);
        if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_ERRCNT10 : RSTV0910_P1_ERRCNT10, &low);
        if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_ERRCTRL1 : RSTV0910_P1_ERRCTRL1, &ctrl);
        if (err==ERROR_NONE) err=stv0910_read_modcod_and_type(demod, &modcod, &short_frame, &pilots);

        if (err==ERROR_NONE) {
            errs = (double)((((uint32_t)high & 0x7f) << 16) | ((uint32_t)mid_m << 8) | (uint32_t)low);
            nbch = stv0910_dvbs2_nbch(modcod, short_frame);
            scale = ctrl & 0x07;
            denominator = (uint64_t)nbch << (scale * 2);
            if (denominator == 0) {
                *ber = 0;
            } else {
                *ber=(uint32_t)((10000.0 * errs / (double)denominator) + 0.5);
            }
        }

        if (err!=ERROR_NONE) printf("ERROR: STV0910 read DVB-S2 BER\n");
        return err;
    }

    /* first we trigger a buffer transfer and read the byte counter 40 bits */
                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERCPT4 : RSTV0910_P1_FBERCPT4, &high);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERCPT3 : RSTV0910_P1_FBERCPT3, &mid_u);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERCPT2 : RSTV0910_P1_FBERCPT2, &mid_m);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERCPT1 : RSTV0910_P1_FBERCPT1, &mid_l);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERCPT0 : RSTV0910_P1_FBERCPT0, &low);
    cpt=(double)high*256.0*256.0*256.0*256.0 + (double)mid_u*256.0*256.0*256.0 + (double)mid_m*256.0*256.0 +
        (double)mid_l*256.0 + (double)low;

    /* we have already triggered the register buffer transfer, so now we we read the bit error from them */
                         err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERERR2 : RSTV0910_P1_FBERERR2, &high);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERERR1 : RSTV0910_P1_FBERERR1, &mid_m);
    if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_FBERERR0 : RSTV0910_P1_FBERERR0, &low);
    errs=(double)high*256.0*256.0 + (double)mid_m*256.0 + (double)low;

    *ber=(uint32_t)(10000.0*errs/(cpt*8.0));

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read BER\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_mer(uint8_t demod, bool dvbs2, uint32_t *mer) {
/* -------------------------------------------------------------------------------------------------- */
/*    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                */
/*    dvbs2: true when reading DVB-S2 C/N, false for DVB-S                                             */
/*      mer: place to store the result                                                                */
/*   return: error state                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t high, low;
    uint32_t reg_value;
    int32_t snr_x10;
    const stv0910_snr_lookup_t *lookup;
    uint32_t lookup_len;

    if (dvbs2) {
                             err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_NNOSPLHT1 : RSTV0910_P1_NNOSPLHT1, &high);
        if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_NNOSPLHT0 : RSTV0910_P1_NNOSPLHT0, &low);
        lookup = stv0910_s2_snr_lookup;
        lookup_len = sizeof(stv0910_s2_snr_lookup) / sizeof(stv0910_s2_snr_lookup[0]);
    } else {
                             err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_NNOSDATAT1 : RSTV0910_P1_NNOSDATAT1, &high);
        if (err==ERROR_NONE) err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_NNOSDATAT0 : RSTV0910_P1_NNOSDATAT0, &low);
        lookup = stv0910_s1_snr_lookup;
        lookup_len = sizeof(stv0910_s1_snr_lookup) / sizeof(stv0910_s1_snr_lookup[0]);
    }

    if (err==ERROR_NONE) {
        reg_value = ((uint32_t)high << 8) | low;
        snr_x10 = stv0910_lookup_snr_x10(lookup, lookup_len, reg_value);
        *mer = snr_x10 < 0 ? 0 : (uint32_t)snr_x10;
    }

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read SNR/MER\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_errors_bch_uncorrected(uint8_t demod, bool *errors_bch_uncorrected) {
/* -------------------------------------------------------------------------------------------------- */
/*                    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read*/
/*   errors_bch_uncorrected: place to store the result                                                */
/*                   return: error state                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t result;

    /* This parameter appears to be total, not for an individual demodulator */
    (void)demod;

    err=stv0910_read_reg_field(FSTV0910_ERRORFLAG, &result);

    if(result != 0) {
        *errors_bch_uncorrected = true;
    }
    else {
        *errors_bch_uncorrected = false;
    }

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read BCH Errors Uncorrected\n");

    return err;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_errors_bch_count(uint8_t demod, uint32_t *errors_bch_count) {
/* -------------------------------------------------------------------------------------------------- */
/*              demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read      */
/*   errors_bch_count: place to store the result                                                      */
/*             return: error state                                                                    */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t result;

    /* This parameter appears to be total, not for an individual demodulator */
    (void)demod;

    err=stv0910_read_reg_field(FSTV0910_BCH_ERRORS_COUNTER, &result);

    *errors_bch_count = (uint32_t)result;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read BCH Errors Count\n");

    return err;
}



/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_errors_ldpc_count(uint8_t demod, uint32_t *errors_ldpc_count) {
/* -------------------------------------------------------------------------------------------------- */
/*               demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read      */
/*   errors_ldpc_count: place to store the result                                                      */
/*              return: error state                                                                    */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t high, low;

    /* This parameter appears to be total, not for an individual demodulator */
    (void)demod;

                         err=stv0910_read_reg_field(FSTV0910_LDPC_ERRORS1, &high);
    if (err==ERROR_NONE) err=stv0910_read_reg_field(FSTV0910_LDPC_ERRORS0, &low);

    *errors_ldpc_count = (uint32_t)high << 8 | (uint32_t)low;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read LDPC Errors Count\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_modcod_and_type(uint8_t demod, uint32_t *modcod, bool *short_frame, bool *pilots) {
/* -------------------------------------------------------------------------------------------------- */
/*   Note that MODCODs are different in DVBS and DVBS2. Also short_frame and pilots only valid for S2 */
/*    demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                */
/*   modcod: place to store the result                                                                */
/*   return: error state                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;
    uint8_t regval;
    
    err=stv0910_read_reg(demod==STV0910_DEMOD_TOP ? RSTV0910_P2_DMDMODCOD : RSTV0910_P1_DMDMODCOD, &regval);

    *modcod = (regval & 0x7c) >> 2;
    *short_frame = (regval & 0x02) >> 1;
    *pilots = regval & 0x01;

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read MODCOD\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_setup_clocks() {
/* -------------------------------------------------------------------------------------------------- */
/* sequence is:                                                                                       */
/*   DIRCLK=0 (the hw clock selection pin)                                                            */
/*   RESETB (the hw reset pin) transits from low to high at least 3ms after power stabilises          */
/*   disable demodulators (done in register initialisation)                                           */
/*   standby=1 (done in register initialisation)                                                      */
/*   set NCOURSE etc. (PLL regs, also done in reg init)                                               */
/*   STANDBY=0 (turn on PLL)                                                                          */
/*   SYNCTRL:BYPASSPLLCORE=0  (turn on clocks)                                                        */
/*   wait for lock bit to go high                                                                     */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint32_t ndiv;
    uint32_t odf;
    uint32_t idf;
    uint32_t f_phi;
    uint32_t f_xtal;
    uint8_t cp;
    uint8_t lock=0;
    uint16_t timeout=0;

    printf("Flow: STV0910 set MCLK\n");

    /* 800MHz < Fvco < 1800MHz                              */
    /* Fvco = (ExtClk * 2 * NDIV) / IDF                     */
    /* (400 * IDF) / ExtClk < NDIV < (900 * IDF) / ExtClk   */

    /* ODF forced to 4 otherwise desynchronization of digital and analog clock which result */
    /* in a bad calculated symbolrate */
    odf=4;
    /* IDF forced to 1 : Optimal value */
    idf=1;
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_ODF, odf);
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_IDF, idf);

    f_xtal=NIM_TUNER_XTAL/1000; /* in MHz */
    f_phi=135000000/1000000;
    ndiv=(f_phi * odf * idf) / f_xtal;
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_N_DIV, ndiv);

    /* Set CP according to NDIV */
    cp=7;
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_CP, cp);

    /* turn on all the clocks */
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_STANDBY, 0);

    /* derive clocks from PLL */
    if (err==ERROR_NONE) err=stv0910_write_reg_field(FSTV0910_BYPASSPLLCORE, 0);

    /* wait for PLL to lock */
    do {
        timeout++;
        if (timeout==STV0910_PLL_LOCK_TIMEOUT) {
             err=ERROR_DEMOD_PLL_TIMEOUT;
             printf("ERROR: STV0910 pll lock timeout\n");
        }
        if (err==ERROR_NONE) stv0910_read_reg_field(FSTV0910_PLLLOCK, &lock);
    } while ((err==ERROR_NONE) && (lock==0));

    if (err!=ERROR_NONE) printf("ERROR: STV0910 set MCLK\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_setup_equalisers(uint8_t demod) {
/* -------------------------------------------------------------------------------------------------- */
/* 2 parts DFE, FFE                                                                                   */
/*     DFE: update speed is in EQUALCFG.PX_MU_EQUALDFE.                                               */
/*       turn it on with EQUAL_ON and freeze with PX_MU_EQUALDFE=0                                    */
/*     FFE: update speed is in FFECFGPX_MU_EQUALFFE.                                                  */
/*       turn it on with EQUALFFE_ON and freeze with PX_MU_EQUALFFE=0                                 */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */

    printf("Flow: Setup equlaizers %i\n", demod);

    return ERROR_NONE;
}


/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_setup_carrier_loop(uint8_t demod) {
/* -------------------------------------------------------------------------------------------------- */
/* 3 stages:                                                                                          */
/*   course:                                                                                          */
/*     CARFREQ sets speed and precision                                                               */
/*     limitsd are in CFRUP and CFRLOW                                                                */
/*     step size in CFRINC                                                                            */
/*   fine:                                                                                            */
/*     CARFREQ_BETA_FREQ defines time constant                                                        */
/*     once course is done, phase tracking loop is used, ACLC and BCLC define loop parameters         */
/*     if DVBS not resolved, attempts to reolve DVB-S2 headers as defined in CARHDR.K_FREQ_HDR        */
/*   tracking:                                                                                        */
/*     seperate alpha a beta for DVBS (ACLC and BCLC) and DVB-S2 (Alpha in CLC2S2Q and                */
/*     beta in ACLC2S28)                                                                              */
/*   lock detect:                                                                                     */
/*     DVBS LDI has accumulator. compared to threshold (LDT, LDT2) and the results are                */
/*     in DSTATUS.CAR_LOCK                                                                            */
/*     when lock bit is set, freq detector is disabled amd starts phase tracking.                     */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error code                                                                                */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err;

    printf("Flow: Setup carrier loop %i\n", demod);

    /* start at 0 offset */
                         err=stv0910_write_reg((demod==STV0910_DEMOD_TOP ? RSTV0910_P2_CFRINIT0 : RSTV0910_P1_CFRINIT0), 0);
    if (err==ERROR_NONE) err=stv0910_write_reg((demod==STV0910_DEMOD_TOP ? RSTV0910_P2_CFRINIT1 : RSTV0910_P1_CFRINIT1), 0);
 
    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_setup_timing_loop(uint8_t demod, uint32_t sr) {
/* -------------------------------------------------------------------------------------------------- */
/* coarse aquisition                                                                                  */
/* put in coarse mode in TMGCFG2 (regs init)                                                          */
/*  put in auto mode TMGCFG3  (regs init)                                                             */
/*  set SFRUPRATIO, SFTLOWRATIO (regs init)                                                           */
/*  set so that when boundary reached scan is inverted (regs init)                                    */
/*  set to keep looking indefinitely (regs init)                                                      */
/*  DVBS alpha and beta are used                                                                      */
/* observe where it is at with KREFTMG2                                                               */
/* fine aquisition                                                                                    */
/*  coarse search defines the fine scanning range automtically                                        */
/*  go to fine mode TMGCFG2                                                                           */
/*  SFRSTEP.SFR_SCANSTEP defines fineness of scan                                                     */
/* tracking                                                                                           */
/*  it now loads loop parameters                                                                      */
/*  seperate alphas and betas for DVBS and SVB-S2:                                                    */
/*     DVBS  TMGALPHA_EXP TIMGBETA_EXP in RTC                                                         */
/*     iDVB-S2 TMGALPHAS2_EXP and TMGBETA2_EXP in RTCS2                                               */
/*  when lock achieved timing offset is in TMGREG                                                     */
/*  timing offset can be cancelled out (ie TMGREG set to zero and SFR  adjusted accordingly           */
/*                                                                                                    */
/*  lock indicator is DSTATUS maximised when locked.                                                  */
/*  need to optimise lock thresholds to optimise lock stability                                       */
/*  lock indicator is filtered with time constant: TMGCFG.TMGLOCK_BETA                                */
/*  this is compared to 2 thresholds TMGTHRISE_TMGLOCK_THRISE and TMGTHFALL.TMGLOCK_THFALL in order   */
/*   to issue TMGCLOCK_QUALITY[1:0] which is a 2 bit lock indicator with hysterisis in DSTATUS.       */
/*  lock is when TMGLOCK>TMGLOCK_THRISE (in TMGLOCK_QUALITY)                                          */
/*  loss of lock is when TMGLOCK < TTMGLOCK_THFALL in TMGLOCK_QUALITY                                 */
/*                                                                                                    */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint16_t sr_reg; 

    printf("Flow: Setup timing loop %i\n", demod);

    /* SR (MHz) = ckadc (135MHz) * SFRINIT / 2^16 */
    /* we have sr in KHz, ckadc in MHz) */
    sr_reg=(uint16_t)((((uint32_t)sr) << 16) / 135 / 1000);

    if (err==ERROR_NONE) err=stv0910_write_reg((demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFRINIT1 : RSTV0910_P1_SFRINIT1),
                                                                           (uint8_t)(sr_reg >> 8)     );
    if (err==ERROR_NONE) err=stv0910_write_reg((demod==STV0910_DEMOD_TOP ? RSTV0910_P2_SFRINIT0 : RSTV0910_P1_SFRINIT0),
                                                                           (uint8_t)(sr_reg & 0xFF)   );

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_setup_ts(uint8_t demod) {
/* -------------------------------------------------------------------------------------------------- */
/* format with or without sync and header bytes TSINSDELH                                             */ 
/*   output rate manual or auto adjust                                                                */ 
/*   control with TSCFX                                                                               */ 
/*   serial or paralled TSCFGH.PxTSFIFO_SERIAL (serial is on D7) 2 control bits                       */ 
/*   configure bus to low impedance (high Z on reset) OUTCFG                                          */ 
/*   DPN (data valid/parity negated) is high when FEC is outputting data                              */ 
/*      low when redundant data is out[ut eq parity data or rate regulation stuffing bits)            */ 
/*   Data is regulated by CLKOUT and DPN: either data valid or envelope.                              */ 
/*     data valid uses continuous clock and select valid data using DPN                               */ 
/*     envelope: DPN still indicates valid data and then punctured clock for rate regulation          */ 
/*     TSCFGH.TSFIFO_DVBCI=1 for data and 0 for envelope.                                             */ 
/*   CLKOUT polarity bit XOR, OUTCFG2.TS/2_CLKOUT_XOR=0 valid rising (=1 for falling).                */ 
/*   TSFIFOMANSPEED controlls data rate (padding). 0x11 manual, 0b00 fully auto. speed is TSSPEE      */
/*     if need square clock, TSCFGH.TSFIFO_DUTY50.                                                    */ 
/*   parallel mode is ST back end. CLKOUT held (TSCFGH.TSINFO_DBCI) for unknown data section          */ 
/*     or DVB-CI: DRN is help (CLKOUTnCFG.CLKOUT_XOR) for unknown data section                        */ 
/*   in both STRUT is high for first byte of packet                                                   */ 
/*   rate compensation is TSCFGH.TSFIFO_DVBCI                                                         */ 
/*                                                                                                    */ 
/*   All of this is set in the register init.                                                         */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    printf("Flow: Setup ts %i\n", demod);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_start_scan(uint8_t demod) {
/* -------------------------------------------------------------------------------------------------- */
/* demodulator search sequence is:                                                                    */
/*   setup the timing loop                                                                            */
/*   setup the carrier loop                                                                           */
/*   set initial carrier offset CFRINIT for best guess carrier search                                 */
/*   set initial symbol ratea SFRINIT for best guess blind search                                     */
/*   set manual mode for CFRINC to be used - make it small                                            */
/*   write DMDISTATE to AER = 1 - blind search with best guess                                        */
/*   auto mode for symbol rate will be +/25% SFRUPRATIO and SFRLOWRATIO define this number.           */
/*   SFRUP1:AUTO_GUP=1 for auto (and SFRLOW1:AUTO_GLOW=1)                                             */
/*   cold start carrier and sr unknown but use best guess (0x01)                                      */
/*                                                                                                    */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0910 start scan\n");

    if (err==ERROR_NONE) err=stv0910_write_reg((demod==STV0910_DEMOD_TOP ? RSTV0910_P2_DMDISTATE : RSTV0910_P1_DMDISTATE),
                                                                                   STV0910_SCAN_BLIND_BEST_GUESS);

    if (err!=ERROR_NONE) printf("ERROR: STV0910 start scan\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_read_scan_state(uint8_t demod, uint8_t *state) {
/* -------------------------------------------------------------------------------------------------- */
/* simply reads out the demodulator states for the given demodulator                                  */
/*   demod: STV0910_DEMOD_TOP | STV0910_DEMOD_BOTTOM: which demodulator is being read                 */
/*  return: error state                                                                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (err==ERROR_NONE) err=stv0910_read_reg_field((demod==STV0910_DEMOD_TOP ? 
                                  FSTV0910_P2_HEADER_MODE : FSTV0910_P1_HEADER_MODE), state);

    if (err!=ERROR_NONE) printf("ERROR: STV0910 read scan state\n");

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_init_regs() {
/* -------------------------------------------------------------------------------------------------- */
/* reads all the initial values for all the demodulator registers and sets them up                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t val1;
    uint8_t val2;
    uint8_t err;
    uint16_t i=0;

    printf("Flow: stv0910 init regs\n");

    /* first we check on the IDs */
    err=nim_read_demod(0xf100, &val1);
    if (err==ERROR_NONE) err=nim_read_demod(0xf101, &val2);
    printf("      Status: STV0910 MID = 0x%.2x, DID = 0x%.2x\n", val1, val2);
    if ((val1!=0x51) || (val2!=0x20)) {
        printf("ERROR: read the wrong stv0910 MID/DID");
        return ERROR_DEMOD_INIT;
    }

    /* next we initialise all the registers in the list */
    do {
        if (err==ERROR_NONE) err=stv0910_write_reg(STV0910DefVal[i].reg, STV0910DefVal[i].val);
    }        
    while (STV0910DefVal[i++].reg!=RSTV0910_TSTTSRS);

    /* finally (from ST example code) reset the LDPC decoder */
    if (err==ERROR_NONE) err=stv0910_write_reg(RSTV0910_TSTRES0, 0x80);
    if (err==ERROR_NONE) err=stv0910_write_reg(RSTV0910_TSTRES0, 0x00);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t stv0910_init(uint32_t sr1, uint32_t sr2) {
/* -------------------------------------------------------------------------------------------------- */
/* demodulator search sequence is:                                                                    */
/*   setup the carrier loop                                                                           */
/*     set initial carrier offset CFRINIT for best guess carrier search                               */
/*     set manual mode for CFRINC to be used - make it small                                          */
/*   setup the timing loop                                                                            */
/*     set initial symbol rate SFRINIT                                                                */
/*     auto mode for symbol rate will be +/25% SFRUPRATIO and SFRLOWRATIO define this number.         */
/*     SFRUP1:AUTO_GUP=1 for auto (and SFRLOW1:AUTO_GLOW=1)                                           */
/*   write DMDISTATE to AER = 1 - blind search with best guess (SR and carrier unknown)               */
/* FLYWHEEL_CPT: when 0xf DVB-S2 is locked in DMDFLYW (also int stus bits                             */
/*   sr_top   : the symbol rate to initialise the top demodulator to (0=disable)                      */
/*   sr_bottom: the symbol rate to initialise the bottom demodulator to (0=disable)                   */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    printf("Flow: STV0910 init\n");

    /* first we stop the demodulators in case they are already running */
    if (err==ERROR_NONE) err=stv0910_write_reg(RSTV0910_P1_DMDISTATE, 0x1c);
    if (err==ERROR_NONE) err=stv0910_write_reg(RSTV0910_P2_DMDISTATE, 0x1c);

    /* do the non demodulator specific stuff */
    if (err==ERROR_NONE) err=stv0910_init_regs();
    if (err==ERROR_NONE) err=stv0910_setup_clocks();

    /* now we do the inits for each specific demodulator */
    if (sr1!=0) {
        if (err==ERROR_NONE) err=stv0910_setup_equalisers(STV0910_DEMOD_TOP);
        if (err==ERROR_NONE) err=stv0910_setup_carrier_loop(STV0910_DEMOD_TOP);
        if (err==ERROR_NONE) err=stv0910_setup_timing_loop(STV0910_DEMOD_TOP, sr1);
    }

    if (sr2!=0) {
        if (err==ERROR_NONE) err=stv0910_setup_equalisers(STV0910_DEMOD_BOTTOM);
        if (err==ERROR_NONE) err=stv0910_setup_carrier_loop(STV0910_DEMOD_BOTTOM);
        if (err==ERROR_NONE) err=stv0910_setup_timing_loop(STV0910_DEMOD_BOTTOM, sr2);
    }

    if (err!=ERROR_NONE) printf("ERROR: STV0910 init\n");

    return err;
}
