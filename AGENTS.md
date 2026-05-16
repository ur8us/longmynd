# Agent Notes

## Project

This repository is LongMynd, a MiniTiouner DVB-S/S2 receiver program.
The local fork adds support for EARDA/Eardatek `EDS-4B47FF1B+` tuner
modules using an STV0903 demodulator and STB6100 tuner.
It also carries the browser interface ported from the
`philcrump/longmynd` fork.

## Build

Use the project `Makefile`:

```sh
make
```

The web interface depends on the `web/libwebsockets` submodule, `json-c`,
and `libcap`; the default `make` target initializes and builds
libwebsockets when needed.

## EARDA/Eardatek NIM

The EARDA path is selected with:

```sh
./longmynd -N earda -i 127.0.0.1 10000 -I 127.0.0.1 10001 1131500 1500
```

Add `-W 8080` to serve the web interface at `http://localhost:8080/`.

`-N eardatek` is kept as an alias. The known working module is labelled
`EDS-4B47FF1B+` and has an STB6100 tuner. The STV0903 demodulator uses a
27 MHz reference in this hardware.

For the QO-100 beacon, expected status values include:

```text
$1,4    DVB-S2 lock
$18,8   DVB-S2 QPSK 4/5
```

Status ID `12` is historically called MER in LongMynd output, but this fork
now reports the demodulator C/N estimate there. On the EARDA/Eardatek
QO-100 beacon path, recent 30 second beacon checks reported roughly
`8.2` to `8.9 dB` with `$23,0` BCH uncorrected status.

View the UDP transport stream with:

```sh
vlc udp://@:10000
```

If video stutters while audio continues, inspect status port `10001` for
LDPC/BCH errors. Clean reception should avoid persistent `$23,1` BCH
uncorrected indications.

The latest filtered sweep report in this workspace is:

```text
QO-100-test/reports/qo100-1500-250-fixed-20260516-142646/
```

Its deduplicated valid rows are the QO-100 beacon at `1500 KS/s` and
`CS5CEP - AMRAD` at `333 KS/s`.
