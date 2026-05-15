# Agent Notes

## Project

This repository is LongMynd, a MiniTiouner DVB-S/S2 receiver program.
The local fork adds support for EARDA/Eardatek `EDS-4B47FF1B+` tuner
modules using an STV0903 demodulator and STB6100 tuner.

## Build

Use the project `Makefile`:

```sh
make
```

Use `make clean && make` after changing headers that are not tracked by
the simple dependency rules.

## EARDA/Eardatek NIM

The EARDA path is selected with:

```sh
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 1131500 1500
```

`-N eardatek` is kept as an alias. The known working module is labelled
`EDS-4B47FF1B+` and has an STB6100 tuner. The STV0903 demodulator uses a
27 MHz reference in this hardware.

For the QO-100 beacon, expected status values include:

```text
$1,4    DVB-S2 lock
$18,8   DVB-S2 QPSK 4/5
```

View the UDP transport stream with:

```sh
vlc udp://@:10000
```

If video stutters while audio continues, inspect status port `10001` for
LDPC/BCH errors. Clean reception should avoid persistent `$23,1` BCH
uncorrected indications.
