/* -------------------------------------------------------------------------------------------------- */
/* The LongMynd receiver: nim.c                                                                       */
/*    - an implementation of the Serit NIM controlling software for the MiniTiouner Hardware          */
/*    - handlers for the nim module itself                                                            */
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
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "nim.h"
#include "ftdi.h"
#include "errors.h"

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- GLOBALS ------------------------------------------------------------------------ */
/* -------------------------------------------------------------------------------------------------- */

/* The tuner and LNAs are accessed by turning on the I2C bus repeater in the demodulator 
   this reduces the noise on the tuner I2C lines as they are inactive when the repeater
   is turned off. We need to keep track of this when we access the NIM  */
bool repeater_on;
static uint8_t nim_demod_addr = NIM_DEMOD_ADDR_SERIT;
static uint8_t nim_tuner_addr = NIM_TUNER_ADDR;

/* -------------------------------------------------------------------------------------------------- */
/* ----------------- ROUTINES ----------------------------------------------------------------------- */
/* -------------------------------------------------------------------------------------------------- */

/* -------------------------------------------------------------------------------------------------- */
void nim_set_demod_addr(uint8_t addr) {
/* -------------------------------------------------------------------------------------------------- */
/* sets the active demodulator i2c write address                                                      */
/* -------------------------------------------------------------------------------------------------- */
    nim_demod_addr = addr;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_get_demod_addr(void) {
/* -------------------------------------------------------------------------------------------------- */
/* gets the active demodulator i2c write address                                                      */
/* -------------------------------------------------------------------------------------------------- */
    return nim_demod_addr;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_probe_demod_addrs(const uint8_t *addrs, uint8_t count) {
/* -------------------------------------------------------------------------------------------------- */
/* probes possible demodulator addresses and selects the first that responds to the MID register      */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NIM_INIT;
    uint8_t val;
    uint8_t i;
    int addr;
    uint8_t ack_count=0;

    for (i=0; i<count; i++) {
        printf("Flow: NIM probe demod I2C address 0x%.2x\n", addrs[i]);
        if (ftdi_i2c_read_reg16(addrs[i], 0xf100, &val)==ERROR_NONE) {
            nim_demod_addr = addrs[i];
            printf("      Status: Demod responded at 0x%.2x, MID=0x%.2x\n", nim_demod_addr, val);
            err = ERROR_NONE;
            break;
        }
    }

    if (err!=ERROR_NONE) {
        printf("ERROR: no demodulator responded on configured Eardatek I2C addresses\n");
        printf("Flow: NIM I2C bus scan\n");
        for (addr=0x02; addr<0xff; addr+=2) {
            if (ftdi_i2c_probe_addr((uint8_t)addr)==ERROR_NONE) {
                printf("      Status: I2C ACK at 8-bit write address 0x%.2x\n", addr);
                ack_count++;
            }
        }
        if (ack_count==0) {
            printf("ERROR: no I2C devices acknowledged on the NIM bus\n");
        }
    }

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
void nim_set_tuner_addr(uint8_t addr) {
/* -------------------------------------------------------------------------------------------------- */
/* sets the active tuner i2c write address                                                            */
/* -------------------------------------------------------------------------------------------------- */
    nim_tuner_addr = addr;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_get_tuner_addr(void) {
/* -------------------------------------------------------------------------------------------------- */
/* gets the active tuner i2c write address                                                            */
/* -------------------------------------------------------------------------------------------------- */
    return nim_tuner_addr;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_probe_tuner_addrs(const uint8_t *addrs, uint8_t count) {
/* -------------------------------------------------------------------------------------------------- */
/* probes possible tuner addresses through the demodulator i2c repeater                               */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint8_t val;
    uint8_t i;
    int addr;
    uint8_t ack_count=0;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }

    for (i=0; err==ERROR_NONE && i<count; i++) {
        printf("Flow: NIM probe tuner I2C address 0x%.2x\n", addrs[i]);
        if (ftdi_i2c_read_addr(addrs[i], &val)==ERROR_NONE) {
            nim_tuner_addr = addrs[i];
            printf("      Status: Tuner responded at 0x%.2x, reg0=0x%.2x\n", nim_tuner_addr, val);
            return ERROR_NONE;
        }
    }

    if (err==ERROR_NONE) {
        printf("ERROR: no tuner responded on configured STB6100 I2C addresses\n");
        printf("Flow: NIM tuner I2C bus scan\n");
        for (addr=0x02; addr<0xff; addr+=2) {
            if (ftdi_i2c_probe_addr((uint8_t)addr)==ERROR_NONE) {
                printf("      Status: tuner-side I2C ACK at 8-bit write address 0x%.2x\n", addr);
                ack_count++;
            }
        }
        if (ack_count==0) {
            printf("ERROR: no I2C devices acknowledged on the tuner-side bus\n");
        }
    }

    if (err!=ERROR_NONE) {
        return err;
    }

    return ERROR_NIM_INIT;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_demod(uint16_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads a demodulator register and takes care of the i2c bus repeater                                */
/*    reg: which demod register to read                                                               */
/*    val: where to put the result                                                                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    /* if we are not using the tuner or lna any more then we can turn off
       the repeater to reduce noise 
       this is bit 7 of the Px_I2CRPT register. Other bits define I2C speed etc. */
    if (repeater_on) {
        repeater_on=false;
        err=nim_write_demod(0xf12a,0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg16(nim_demod_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: demod read 0x%.4x\n",reg);

    /* note we don't turn the repeater off as there might be other r/w to tuner/LNAs */

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_demod(uint16_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to a demodulator register and takes care of the i2c bus repeater                            */
/*    reg: which demod register to write to                                                           */
/*    val: what to write to it                                                                        */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (repeater_on) {
        repeater_on=false;
        err=nim_write_demod(0xf12a,0x38);
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg16(nim_demod_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: demod write 0x%.4x, 0x%.2x\n",reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_lna(uint8_t lna_addr, uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from the specified lna taking care of the i2c bus repeater                                   */
/*  lna_addr: i2c address of the lna to access                                                        */
/*       reg: which lna register to read                                                              */
/*       val: where to put the result                                                                 */
/*    return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg8(lna_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: lna read 0x%.2x, 0x%.2x\n",lna_addr,reg);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_lna(uint8_t lna_addr, uint8_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to the specified lna taking care of the i2c bus repeater                                    */
/*  lna_addr: i2c address of the lna to access                                                        */
/*       reg: which lna register to write to                                                          */
/*       val: what to write to it                                                                     */
/*    return: error code                                                                              */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg8(lna_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: lna write 0x%.2x, 0x%.2x,0x%.2x\n",lna_addr,reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_tuner_addr(uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from a tuner whose register number is encoded in the i2c address                             */
/*    reg: which tuner register to read from                                                          */
/*    val: where to put the result                                                                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_addr(nim_tuner_addr + (reg << 1), val);
    if (err!=ERROR_NONE) printf("ERROR: tuner address-read 0x%.2x\n",reg);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_read_tuner(uint8_t reg, uint8_t *val) {
/* -------------------------------------------------------------------------------------------------- */
/* reads from the stv0910 (tuner) taking care of the i2c bus repeater                                 */
/*    reg: which tuner register to read from                                                          */
/*    val: where to put the result                                                                    */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_read_reg8(nim_tuner_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: tuner read 0x%.2x\n",reg);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_write_tuner(uint8_t reg, uint8_t val) {
/* -------------------------------------------------------------------------------------------------- */
/* writes to the stv0910 (tuner) taking care of the i2c bus repeater                                  */
/*    reg: which tuner register to write to                                                           */
/*    val: what to write to it                                                                        */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;

    if (!repeater_on) {
        err=nim_write_demod(0xf12a,0xb8);
        repeater_on=true;
    }
    if (err==ERROR_NONE) err=ftdi_i2c_write_reg8(nim_tuner_addr,reg,val);
    if (err!=ERROR_NONE) printf("ERROR: tuner write %i,%i\n",reg,val);

    return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t nim_init() {
/* -------------------------------------------------------------------------------------------------- */
/* initialises the nim (at the highest level)                                                         */
/* return: error code                                                                                 */
/* -------------------------------------------------------------------------------------------------- */
    uint8_t err=ERROR_NONE;
    uint8_t val;

    printf("Flow: NIM init\n");

    repeater_on = false;

    /* check we can read and write a register */
    /* check to see if we can write and readback to a demod register */
    if (err==ERROR_NONE) err=nim_write_demod(0xf536,0xaa); /* random reg with alternating bits */
    if (err==ERROR_NONE) err=nim_read_demod(0xf536,&val);
    if (err==ERROR_NONE) {
        if (val!=0xaa) { /* check alternating bits ok */
            printf("ERROR: nim_init didn't r/w to the modulator\n");
            err=ERROR_NIM_INIT;
        }
    }

    /* we always want to start with the i2c repeater turned off */
    if (err!=ERROR_NONE) err=nim_write_demod(0xf12a,0x38);

    if (err!=ERROR_NONE) printf("ERROR: nim_init\n");

    return err;
}
