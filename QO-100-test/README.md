# QO-100 DATV Sweep Tests

This folder contains scripts for sweeping the documented QO-100 wideband
DATV spot frequencies with LongMynd and recording reception characteristics.

The frequency plan is based on the BATC QO-100 WB Bandplan and AMSAT-DL
QO-100 WB Bandplan V3. It includes:

- Beacon: `10491.500 MHz`, `1500 KS/s`, DVB-S2 FEC 4/5.
- Wide DATV: 3 spot frequencies with `1500` and `1000 KS/s`.
- Narrow DATV: 14 spot frequencies with documented `500`, `333`, and
  `250 KS/s` rates where applicable.
- Very narrow DATV: 27 spot frequencies with `125`, `66`, and `33 KS/s`.

The default LO is your QO-100 setup value:

```text
LO = 9360000 kHz
IF = downlink_khz - 9360000
```

## Run

From the repository root:

```sh
QO-100-test/scripts/run_15s_sweep.sh
```

For a 30 second dwell time per frequency/SR pair:

```sh
QO-100-test/scripts/run_15s_sweep.sh --seconds 30
```

To inspect the plan without running LongMynd:

```sh
QO-100-test/scripts/run_15s_sweep.sh --plan-only
```

To test only the first few entries:

```sh
QO-100-test/scripts/run_15s_sweep.sh --limit 5
```

## Reports

Each run creates a timestamped directory under `QO-100-test/reports/` with:

- `summary.csv`: machine-readable summary.
- `summary.md`: human-readable table.
- `results.jsonl`: one JSON object per tested frequency/SR pair.
- `raw-status/*.status`: raw LongMynd UDP status lines.
- `console/*.log`: LongMynd console output for each test.

The scripts do not record video. LongMynd is run with UDP TS output to
`127.0.0.1:10000`, but the sweep only listens to the UDP status port and
stores reception characteristics such as lock state, C/N estimate, MODCOD,
service name, provider name, PIDs, and error counters.

`received` means LongMynd reported a DVB-S or DVB-S2 lock during the dwell
period. `clean_received` is stricter and requires a lock without a
`$23,1` BCH uncorrected indication. `valid_received` is stricter again: it
also requires nonzero status-12 C/N and an observed carrier close to the
requested IF.
This filters weak duplicate locks, for example when the demodulator re-locks
the beacon while tuned to a nearby spot frequency. LongMynd status ID 12 is
historically named MER, but this fork now reports the demodulator C/N estimate
there, so the report's `snr_db_estimate` column is the best status-12 C/N value
seen during the dwell.

## Sources

- BATC QO-100 WB Bandplan:
  `https://wiki.batc.org.uk/QO-100_WB_Bandplan`
- AMSAT-DL QO-100 WB Bandplan V3:
  `https://amsat-dl.org/wp-content/uploads/2021/02/QO-100-WB-Bandplan-V3.pdf`
