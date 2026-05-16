# QO-100 DATV Sweep Analysis

- Tested entries: 44
- LongMynd lock-state rows: 5
- Valid received rows: 2
- Weak lock rows ignored: 3
- Deduplicated candidate identities: 2
- Valid means DVB-S/DVB-S2 lock, nonzero status-12 C/N, no `$23,1` BCH uncorrected status, and observed carrier close to the requested IF.

## Deduplicated Candidate Rows

Rows are grouped by service, provider, MODCOD, and observed symbol rate; the row with the closest carrier offset is kept.

| seq | mode | ch | downlink_khz | if_khz | sr_ks | clean | valid | best_mer_db | carrier_offset_khz | bch_unc | service | provider | modcod | observed_carrier_khz |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Beacon | beacon | 10491500 | 1131500 | 1500 | True | True | 8.8 | -9 | 0 | A71A | QARS | QPSK 4/5 | 1131491 |
| 41 | Narrow | 13 | 10498750 | 1138750 | 333 | True | True | 5.8 | -10 | 0 | CS5CEP - AMRAD | SDR Television v1.0.14 | QPSK 2/3 | 1138740 |

## Lock-State Rows

| seq | mode | ch | downlink_khz | if_khz | sr_ks | clean | valid | best_mer_db | carrier_offset_khz | bch_unc | service | provider | modcod | observed_carrier_khz |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | Beacon | beacon | 10491500 | 1131500 | 1500 | True | True | 8.8 | -9 | 0 | A71A | QARS | QPSK 4/5 | 1131491 |
| 2 | Wide | 1 | 10493250 | 1133250 | 1500 | True | False | 8.6 | 1757 | 0 | A71A | QARS | QPSK 4/5 | 1135007 |
| 39 | Narrow | 12 | 10498250 | 1138250 | 333 | True | False | 6.8 | -511 | 0 | CS5CEP - AMRAD | SDR Television v1.0.14 | QPSK 2/3 | 1137739 |
| 41 | Narrow | 13 | 10498750 | 1138750 | 333 | True | True | 5.8 | -10 | 0 | CS5CEP - AMRAD | SDR Television v1.0.14 | QPSK 2/3 | 1138740 |
| 43 | Narrow | 14 | 10499250 | 1139250 | 333 | True | False | 6.2 | 490 | 0 | CS5CEP - AMRAD | SDR Television v1.0.14 | QPSK 2/3 | 1139740 |
