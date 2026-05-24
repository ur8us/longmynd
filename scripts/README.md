# QO-100 WB Helper Scripts

These scripts use the BATC QO-100 wideband spectrum monitor to find active
DATV signals, then optionally tune LongMynd and capture PNG video frames.

## Find Active Stations

```sh
scripts/find_qo100_stations.sh
```

The finder connects to the BATC WB FFT websocket at
`https://eshail.batc.org.uk/wb/`, samples the spectrum for a few seconds, and
prints currently visible signals with:

- downlink frequency in kHz
- LongMynd IF in kHz for the default `9360000 kHz` LO
- uplink frequency in kHz
- estimated symbol rate
- approximate FFT width and strength
- relative level to the beacon when the beacon is visible

Machine-readable JSON:

```sh
scripts/find_qo100_stations.sh --json
```

Useful options:

```sh
scripts/find_qo100_stations.sh --seconds 10
scripts/find_qo100_stations.sh --threshold 16000
scripts/find_qo100_stations.sh --no-beacon
scripts/find_qo100_stations.sh --lo-khz 9360000
```

The frequency mapping and symbol-rate buckets follow the BATC WB monitor page:
the FFT spans `10490.5 MHz` to `10499.5 MHz`.

## Capture Screenshots

```sh
scripts/capture_qo100_screens.sh
```

The capture script repeatedly:

1. scans the BATC WB FFT feed for active DATV signals,
2. tunes LongMynd to the next detected downlink and symbol rate,
3. waits for DVB-S/S2 lock,
4. asks `ffmpeg` to save one PNG video frame,
5. moves to the next detected station,
6. repeats until interrupted with `Ctrl-C`.

Screenshots and run summaries are written under the ignored directory:

```text
scripts/screenshots/
```

To group PNG files into per-callsign folders, use:

```sh
scripts/capture_qo100_screens.sh --group-by-callsign
```

With that option, the default output root changes to:

```text
scripts/screenshots-by-callsign/
```

PNG files go under subfolders such as:

```text
scripts/screenshots-by-callsign/A71A/
scripts/screenshots-by-callsign/2E0ILY/
```

Run summaries and LongMynd logs stay at the selected output root.

Filenames use:

```text
YYYYMMDD-HHMMSS-CALLSIGN-SAMPLERATE.png
```

For example:

```text
20260524-135912-A71A-1500ks.png
```

If LongMynd cannot decode a service name, the filename uses the downlink, such
as `DL10492765`.

By default the script refuses to run if another `longmynd` process already
exists. To let it stop the existing process first:

```sh
scripts/capture_qo100_screens.sh --stop-existing
```

Run for a fixed time instead of until interrupt:

```sh
scripts/capture_qo100_screens.sh --max-run-seconds 900
```

Run the grouped capture mode for 15 minutes:

```sh
scripts/capture_qo100_screens.sh --group-by-callsign --max-run-seconds 900
```

Skip the beacon:

```sh
scripts/capture_qo100_screens.sh --no-beacon
```

Use different dwell/capture timing:

```sh
scripts/capture_qo100_screens.sh --scan-seconds 8 --lock-wait 18 --capture-timeout 35
```

The script retries screenshot capture up to three times and, when Pillow is
installed, rejects near-flat grey decoder frames. Disable that check with:

```sh
scripts/capture_qo100_screens.sh --min-detail-stddev 0
```

## Requirements

- built `./longmynd`
- EARDA/Eardatek tuner connected and usable with `-N earda`
- `ffmpeg` in `PATH`
- network access to `eshail.batc.org.uk`

The script uses `-L auto` by default with this workspace's Low SR LongMynd
changes. Use `--low-sr off` or `--low-sr on` to override it.
