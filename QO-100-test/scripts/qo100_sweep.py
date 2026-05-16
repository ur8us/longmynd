#!/usr/bin/env python3
"""Sweep documented QO-100 DATV spot frequencies with LongMynd.

The script launches LongMynd once per downlink/symbol-rate plan entry, listens
to the LongMynd UDP status stream, and writes raw and summarized reception
reports. It deliberately does not consume or record the transport stream.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import select
import signal
import socket
import subprocess
import sys
import time
from dataclasses import asdict, dataclass
from datetime import datetime
from decimal import Decimal
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


DEFAULT_LO_KHZ = 9_360_000
DEFAULT_SECONDS = 15
DEFAULT_TS_HOST = "127.0.0.1"
DEFAULT_TS_PORT = 10000
DEFAULT_STATUS_HOST = "127.0.0.1"
DEFAULT_STATUS_PORT = 10001
DEFAULT_NIM = "earda"


STATUS_NAMES = {
    1: "state",
    2: "lna_gain",
    3: "puncture_rate",
    4: "power_i",
    5: "power_q",
    6: "carrier_frequency_khz",
    7: "constellation_i",
    8: "constellation_q",
    9: "symbol_rate_sps",
    10: "viterbi_error_rate",
    11: "ber",
    12: "mer_x10",
    13: "service_name",
    14: "service_provider_name",
    15: "ts_null_percentage",
    16: "es_pid",
    17: "es_type",
    18: "modcod",
    19: "short_frame",
    20: "pilots",
    21: "ldpc_error_count",
    22: "bch_error_count",
    23: "bch_uncorrected",
    24: "lnb_supply",
    25: "lnb_polarisation_h",
    26: "agc1_gain",
    27: "agc2_gain",
}

STATE_NAMES = {
    0: "initialising",
    1: "searching",
    2: "found_headers",
    3: "locked_dvb_s",
    4: "locked_dvb_s2",
}

DVBS_MODCOD = {
    0: "QPSK 1/2",
    1: "QPSK 2/3",
    2: "QPSK 3/4",
    3: "QPSK 5/6",
    4: "QPSK 6/7",
    5: "QPSK 7/8",
}

DVBS2_MODCOD = {
    0: "DummyPL",
    1: "QPSK 1/4",
    2: "QPSK 1/3",
    3: "QPSK 2/5",
    4: "QPSK 1/2",
    5: "QPSK 3/5",
    6: "QPSK 2/3",
    7: "QPSK 3/4",
    8: "QPSK 4/5",
    9: "QPSK 5/6",
    10: "QPSK 8/9",
    11: "QPSK 9/10",
    12: "8PSK 3/5",
    13: "8PSK 2/3",
    14: "8PSK 3/4",
    15: "8PSK 5/6",
    16: "8PSK 8/9",
    17: "8PSK 9/10",
    18: "16APSK 2/3",
    19: "16APSK 3/4",
    20: "16APSK 4/5",
    21: "16APSK 5/6",
    22: "16APSK 8/9",
    23: "16APSK 9/10",
    24: "32APSK 3/4",
    25: "32APSK 4/5",
    26: "32APSK 5/6",
    27: "32APSK 8/9",
    28: "32APSK 9/10",
}


@dataclass(frozen=True)
class PlanEntry:
    sequence: int
    mode: str
    channel: str
    downlink_khz: int
    uplink_khz: int
    symbol_rate_ks: int
    note: str

    def with_if(self, lo_khz: int) -> Dict[str, object]:
        data = asdict(self)
        data["lo_khz"] = lo_khz
        data["if_khz"] = self.downlink_khz - lo_khz
        return data


@dataclass
class StatusSample:
    timestamp_monotonic: float
    status_id: int
    value: str
    raw: str


def mhz_to_khz(value: str) -> int:
    return int(Decimal(value) * Decimal(1000))


def build_plan() -> List[PlanEntry]:
    entries: List[PlanEntry] = []

    def add(mode: str, channel: object, downlink_mhz: str, uplink_mhz: str, symbol_rates: Iterable[int], note: str) -> None:
        for symbol_rate_ks in symbol_rates:
            entries.append(
                PlanEntry(
                    sequence=len(entries) + 1,
                    mode=mode,
                    channel=str(channel),
                    downlink_khz=mhz_to_khz(downlink_mhz),
                    uplink_khz=mhz_to_khz(uplink_mhz),
                    symbol_rate_ks=symbol_rate_ks,
                    note=note,
                )
            )

    add("Beacon", "beacon", "10491.5", "2402.0", [1500], "Beacon DVB-S2 FEC 4/5")

    for channel, downlink_mhz, uplink_mhz in [
        (1, "10493.25", "2403.75"),
        (2, "10494.75", "2405.25"),
        (3, "10496.25", "2406.75"),
    ]:
        add("Wide", channel, downlink_mhz, uplink_mhz, [1500, 1000], "Wide DATV 1000/1500 KS")

    narrow_downlinks = [
        "10492.75",
        "10493.25",
        "10493.75",
        "10494.25",
        "10494.75",
        "10495.25",
        "10495.75",
        "10496.25",
        "10496.75",
        "10497.25",
        "10497.75",
        "10498.25",
        "10498.75",
        "10499.25",
    ]
    narrow_uplinks = [
        "2403.25",
        "2403.75",
        "2404.25",
        "2404.75",
        "2405.25",
        "2405.75",
        "2406.25",
        "2406.75",
        "2407.25",
        "2407.75",
        "2408.25",
        "2408.75",
        "2409.25",
        "2409.75",
    ]
    for channel, (downlink_mhz, uplink_mhz) in enumerate(zip(narrow_downlinks, narrow_uplinks), start=1):
        rates = [500, 333, 250] if channel <= 9 else [333, 250]
        add("Narrow", channel, downlink_mhz, uplink_mhz, rates, "Narrow DATV")

    for channel in range(1, 28):
        downlink_khz = mhz_to_khz("10492.75") + (channel - 1) * 250
        uplink_khz = mhz_to_khz("2403.25") + (channel - 1) * 250
        add(
            "Very Narrow",
            channel,
            f"{Decimal(downlink_khz) / Decimal(1000)}",
            f"{Decimal(uplink_khz) / Decimal(1000)}",
            [125, 66, 33],
            "Very narrow DATV",
        )

    return entries


def safe_name(entry: PlanEntry, lo_khz: int) -> str:
    return (
        f"{entry.sequence:03d}_"
        f"{entry.mode.lower().replace(' ', '-')}_"
        f"ch{entry.channel}_"
        f"dl{entry.downlink_khz}_"
        f"if{entry.downlink_khz - lo_khz}_"
        f"sr{entry.symbol_rate_ks}"
    ).replace("/", "-")


def parse_status_line(raw: str, timestamp_monotonic: float) -> Optional[StatusSample]:
    line = raw.strip()
    if not line.startswith("$") or "," not in line:
        return None
    sid_text, value = line[1:].split(",", 1)
    try:
        status_id = int(sid_text)
    except ValueError:
        return None
    return StatusSample(timestamp_monotonic, status_id, value.strip(), line)


def int_values(samples: Sequence[StatusSample], status_id: int) -> List[int]:
    values: List[int] = []
    for sample in samples:
        if sample.status_id != status_id:
            continue
        try:
            values.append(int(sample.value))
        except ValueError:
            continue
    return values


def last_string(samples: Sequence[StatusSample], status_id: int) -> str:
    for sample in reversed(samples):
        if sample.status_id == status_id and sample.value:
            return sample.value
    return ""


def last_int(samples: Sequence[StatusSample], status_id: int) -> Optional[int]:
    values = int_values(samples, status_id)
    return values[-1] if values else None


def best_mer_db(samples: Sequence[StatusSample]) -> Optional[float]:
    values = int_values(samples, 12)
    if not values:
        return None
    return max(values) / 10.0


def last_mer_db(samples: Sequence[StatusSample]) -> Optional[float]:
    values = int_values(samples, 12)
    if not values:
        return None
    return values[-1] / 10.0


def parse_pid_pairs(samples: Sequence[StatusSample]) -> List[str]:
    pairs: List[str] = []
    pending_pid: Optional[int] = None
    for sample in samples:
        if sample.status_id == 16:
            try:
                pending_pid = int(sample.value)
            except ValueError:
                pending_pid = None
        elif sample.status_id == 17 and pending_pid is not None:
            try:
                es_type = int(sample.value)
                pair = f"{pending_pid}:{es_type}"
                if pair not in pairs:
                    pairs.append(pair)
            except ValueError:
                pass
            pending_pid = None
    return pairs


def decode_modcod(samples: Sequence[StatusSample]) -> Tuple[str, str]:
    modcod = last_int(samples, 18)
    if modcod is None:
        return "", ""
    states = int_values(samples, 1)
    last_state = states[-1] if states else None
    if last_state == 3:
        return str(modcod), DVBS_MODCOD.get(modcod, f"unknown DVB-S {modcod}")
    return str(modcod), DVBS2_MODCOD.get(modcod, f"unknown DVB-S2 {modcod}")


def summarize(entry: PlanEntry, lo_khz: int, duration_s: float, samples: Sequence[StatusSample], returncode: Optional[int], raw_file: Path, console_file: Path) -> Dict[str, object]:
    states = int_values(samples, 1)
    lock_states = [state for state in states if state in (3, 4)]
    lock_state = lock_states[-1] if lock_states else (states[-1] if states else None)
    modcod_value, modcod_label = decode_modcod(samples)
    best_mer = best_mer_db(samples)
    last_mer = last_mer_db(samples)
    bch_uncorrected = last_int(samples, 23)
    observed_carrier = last_int(samples, 6)
    carrier_offset_khz = None if observed_carrier is None else observed_carrier - (entry.downlink_khz - lo_khz)
    carrier_window_khz = max(25, int(round(entry.symbol_rate_ks * 0.25)))
    carrier_close = carrier_offset_khz is not None and abs(carrier_offset_khz) <= carrier_window_khz
    quality_ok = bch_uncorrected == 0 and best_mer is not None and best_mer > 0.0
    clean_received = bool(lock_states) and bch_uncorrected == 0
    valid_received = bool(lock_states) and quality_ok and carrier_close

    if valid_received:
        reception_note = "locked_valid"
    elif lock_states:
        weak_reasons = []
        if not quality_ok:
            weak_reasons.append("bad_quality")
        if not carrier_close:
            weak_reasons.append("far_carrier")
        reception_note = "locked_weak_ignored_" + "_".join(weak_reasons)
    else:
        reception_note = "no_lock"

    summary: Dict[str, object] = entry.with_if(lo_khz)
    summary.update(
        {
            "duration_s": duration_s,
            "received": bool(lock_states),
            "clean_received": clean_received,
            "valid_received": valid_received,
            "reception_note": reception_note,
            "lock_state": "" if lock_state is None else lock_state,
            "lock_state_name": "" if lock_state is None else STATE_NAMES.get(lock_state, f"unknown_{lock_state}"),
            "best_mer_db": "" if best_mer is None else f"{best_mer:.1f}",
            "last_mer_db": "" if last_mer is None else f"{last_mer:.1f}",
            "snr_db_estimate": "" if best_mer is None else f"{best_mer:.1f}",
            "modcod": modcod_value,
            "modcod_label": modcod_label,
            "service_name": last_string(samples, 13),
            "service_provider_name": last_string(samples, 14),
            "ts_null_percentage": "" if last_int(samples, 15) is None else last_int(samples, 15),
            "ldpc_error_count": "" if last_int(samples, 21) is None else last_int(samples, 21),
            "bch_error_count": "" if last_int(samples, 22) is None else last_int(samples, 22),
            "bch_uncorrected": "" if bch_uncorrected is None else bch_uncorrected,
            "observed_carrier_khz": "" if observed_carrier is None else observed_carrier,
            "carrier_offset_khz": "" if carrier_offset_khz is None else carrier_offset_khz,
            "carrier_window_khz": carrier_window_khz,
            "carrier_close": carrier_close,
            "observed_symbol_rate_sps": "" if last_int(samples, 9) is None else last_int(samples, 9),
            "agc1_gain": "" if last_int(samples, 26) is None else last_int(samples, 26),
            "agc2_gain": "" if last_int(samples, 27) is None else last_int(samples, 27),
            "elementary_streams": " ".join(parse_pid_pairs(samples)),
            "status_count": len(samples),
            "returncode": "" if returncode is None else returncode,
            "raw_status_file": str(raw_file),
            "console_file": str(console_file),
        }
    )
    return summary


def listen_status(sock: socket.socket, duration_s: float, process: subprocess.Popen[str]) -> List[StatusSample]:
    samples: List[StatusSample] = []
    deadline = time.monotonic() + duration_s
    while time.monotonic() < deadline:
        remaining = max(0.0, deadline - time.monotonic())
        readable, _, _ = select.select([sock], [], [], min(0.25, remaining))
        if readable:
            packet, _addr = sock.recvfrom(4096)
            timestamp_monotonic = time.monotonic()
            text = packet.decode("utf-8", errors="replace")
            for raw in text.splitlines():
                sample = parse_status_line(raw, timestamp_monotonic)
                if sample is not None:
                    samples.append(sample)
        if process.poll() is not None:
            break
    return samples


def stop_process(process: subprocess.Popen[str], grace_s: float = 3.0) -> Optional[int]:
    if process.poll() is not None:
        return process.returncode
    process.send_signal(signal.SIGTERM)
    try:
        return process.wait(timeout=grace_s)
    except subprocess.TimeoutExpired:
        process.kill()
        return process.wait(timeout=grace_s)


def run_entry(entry: PlanEntry, args: argparse.Namespace, run_dir: Path, raw_dir: Path, console_dir: Path) -> Dict[str, object]:
    if_khz = entry.downlink_khz - args.lo_khz
    label = safe_name(entry, args.lo_khz)
    raw_file = raw_dir / f"{label}.status"
    console_file = console_dir / f"{label}.log"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.status_host, args.status_port))

    cmd = [
        args.longmynd,
        "-N",
        args.nim,
        "-i",
        args.ts_host,
        str(args.ts_port),
        "-I",
        args.status_host,
        str(args.status_port),
        str(if_khz),
        str(entry.symbol_rate_ks),
    ]

    with console_file.open("w", encoding="utf-8") as console:
        console.write("$ " + " ".join(cmd) + "\n\n")
        console.flush()
        process = subprocess.Popen(
            cmd,
            stdout=console,
            stderr=subprocess.STDOUT,
            text=True,
            cwd=args.repo_root,
        )
        samples = listen_status(sock, args.seconds, process)
        returncode = stop_process(process)

    sock.close()

    with raw_file.open("w", encoding="utf-8") as raw:
        for sample in samples:
            raw.write(f"{sample.timestamp_monotonic:.3f} {sample.raw}\n")

    summary = summarize(entry, args.lo_khz, args.seconds, samples, returncode, raw_file, console_file)
    summary["command"] = " ".join(cmd)
    return summary


def write_csv(path: Path, rows: Sequence[Dict[str, object]]) -> None:
    if not rows:
        path.write_text("", encoding="utf-8")
        return
    fieldnames = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)


def write_markdown(path: Path, rows: Sequence[Dict[str, object]]) -> None:
    headers = [
        "seq",
        "mode",
        "ch",
        "downlink_khz",
        "if_khz",
        "sr_ks",
        "received",
        "clean",
        "valid",
        "state",
        "best_mer_db",
        "carrier_offset_khz",
        "bch_unc",
        "modcod",
        "service",
        "provider",
        "pids",
        "note",
    ]
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# QO-100 DATV Sweep Summary\n\n")
        handle.write("| " + " | ".join(headers) + " |\n")
        handle.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
        for row in rows:
            values = [
                row["sequence"],
                row["mode"],
                row["channel"],
                row["downlink_khz"],
                row["if_khz"],
                row["symbol_rate_ks"],
                row["received"],
                row.get("clean_received", ""),
                row.get("valid_received", ""),
                row["lock_state_name"],
                row["best_mer_db"],
                row.get("carrier_offset_khz", ""),
                row["bch_uncorrected"],
                row["modcod_label"],
                row["service_name"],
                row["service_provider_name"],
                row["elementary_streams"],
                row.get("reception_note", ""),
            ]
            handle.write("| " + " | ".join(str(value).replace("|", "\\|") for value in values) + " |\n")


def row_is_clean(row: Dict[str, object]) -> bool:
    if "clean_received" in row:
        return str(row["clean_received"]) == "True"
    return str(row.get("received", "")) == "True" and str(row.get("bch_uncorrected", "")) == "0"


def row_is_valid(row: Dict[str, object]) -> bool:
    if "valid_received" in row:
        return str(row["valid_received"]) == "True"
    return row_is_clean(row)


def row_abs_carrier_offset(row: Dict[str, object]) -> int:
    try:
        return abs(int(str(row.get("carrier_offset_khz", ""))))
    except ValueError:
        return 1_000_000_000


def deduplicated_candidates(rows: Sequence[Dict[str, object]]) -> List[Dict[str, object]]:
    grouped: Dict[Tuple[str, str, str, str, str], Dict[str, object]] = {}
    for row in rows:
        if str(row.get("received", "")) != "True":
            continue
        service = str(row.get("service_name", ""))
        provider = str(row.get("service_provider_name", ""))
        if not service and not provider:
            continue
        key = (
            service,
            provider,
            str(row.get("symbol_rate_ks", "")),
            str(row.get("modcod_label", "")),
            str(row.get("observed_symbol_rate_sps", "")),
        )
        current = grouped.get(key)
        if current is None or row_abs_carrier_offset(row) < row_abs_carrier_offset(current):
            grouped[key] = row
    return sorted(grouped.values(), key=lambda row: int(str(row["sequence"])))


def write_rows_table(handle, headers: Sequence[str], rows: Sequence[Dict[str, object]]) -> None:
    handle.write("| " + " | ".join(headers) + " |\n")
    handle.write("| " + " | ".join(["---"] * len(headers)) + " |\n")
    for row in rows:
        values = [
            row["sequence"],
            row["mode"],
            row["channel"],
            row["downlink_khz"],
            row["if_khz"],
            row["symbol_rate_ks"],
            row_is_clean(row),
            row_is_valid(row),
            row["best_mer_db"],
            row.get("carrier_offset_khz", ""),
            row["bch_uncorrected"],
            row["service_name"],
            row["service_provider_name"],
            row["modcod_label"],
            row["observed_carrier_khz"],
        ]
        handle.write("| " + " | ".join(str(value).replace("|", "\\|") for value in values) + " |\n")


def write_analysis(path: Path, rows: Sequence[Dict[str, object]]) -> None:
    locked_rows = [row for row in rows if str(row.get("received", "")) == "True"]
    valid_rows = [row for row in locked_rows if row_is_valid(row)]
    weak_rows = [row for row in locked_rows if not row_is_valid(row)]
    candidates = deduplicated_candidates(rows)
    with path.open("w", encoding="utf-8") as handle:
        handle.write("# QO-100 DATV Sweep Analysis\n\n")
        handle.write(f"- Tested entries: {len(rows)}\n")
        handle.write(f"- LongMynd lock-state rows: {len(locked_rows)}\n")
        handle.write(f"- Valid received rows: {len(valid_rows)}\n")
        handle.write(f"- Weak lock rows ignored: {len(weak_rows)}\n")
        handle.write(f"- Deduplicated candidate identities: {len(candidates)}\n")
        handle.write("- Valid means DVB-S/DVB-S2 lock, nonzero status-12 C/N, no `$23,1` BCH uncorrected status, and observed carrier close to the requested IF.\n\n")

        if not locked_rows:
            handle.write("No rows reported a DVB-S or DVB-S2 lock.\n")
            return

        if candidates:
            handle.write("## Deduplicated Candidate Rows\n\n")
            handle.write("Rows are grouped by service, provider, MODCOD, and observed symbol rate; the row with the closest carrier offset is kept.\n\n")
            headers = [
                "seq",
                "mode",
                "ch",
                "downlink_khz",
                "if_khz",
                "sr_ks",
                "clean",
                "valid",
                "best_mer_db",
                "carrier_offset_khz",
                "bch_unc",
                "service",
                "provider",
                "modcod",
                "observed_carrier_khz",
            ]
            write_rows_table(handle, headers, candidates)
            handle.write("\n")

        handle.write("## Lock-State Rows\n\n")
        headers = [
            "seq",
            "mode",
            "ch",
            "downlink_khz",
            "if_khz",
            "sr_ks",
            "clean",
            "valid",
            "best_mer_db",
            "carrier_offset_khz",
            "bch_unc",
            "service",
            "provider",
            "modcod",
            "observed_carrier_khz",
        ]
        write_rows_table(handle, headers, locked_rows)


def select_plan(plan: Sequence[PlanEntry], args: argparse.Namespace) -> List[PlanEntry]:
    selected = list(plan)
    if args.mode:
        wanted = {mode.strip().lower() for mode in args.mode.split(",") if mode.strip()}
        selected = [entry for entry in selected if entry.mode.lower() in wanted]
    if args.min_symbol_rate is not None:
        selected = [entry for entry in selected if entry.symbol_rate_ks >= args.min_symbol_rate]
    if args.max_symbol_rate is not None:
        selected = [entry for entry in selected if entry.symbol_rate_ks <= args.max_symbol_rate]
    if args.start_index > 1:
        selected = [entry for entry in selected if entry.sequence >= args.start_index]
    if args.limit is not None:
        selected = selected[: args.limit]
    return selected


def print_plan(plan: Sequence[PlanEntry], lo_khz: int) -> None:
    writer = csv.DictWriter(
        sys.stdout,
        fieldnames=["sequence", "mode", "channel", "downlink_khz", "if_khz", "symbol_rate_ks", "note"],
    )
    writer.writeheader()
    for entry in plan:
        writer.writerow(
            {
                "sequence": entry.sequence,
                "mode": entry.mode,
                "channel": entry.channel,
                "downlink_khz": entry.downlink_khz,
                "if_khz": entry.downlink_khz - lo_khz,
                "symbol_rate_ks": entry.symbol_rate_ks,
                "note": entry.note,
            }
        )


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=str(repo_root), help="repository root")
    parser.add_argument("--longmynd", default=str(repo_root / "longmynd"), help="path to longmynd binary")
    parser.add_argument("--nim", default=DEFAULT_NIM, help="LongMynd -N value")
    parser.add_argument("--lo-khz", type=int, default=DEFAULT_LO_KHZ, help="QO-100 receive LO in kHz")
    parser.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help="test duration per plan entry")
    parser.add_argument("--ts-host", default=DEFAULT_TS_HOST, help="LongMynd TS UDP destination host")
    parser.add_argument("--ts-port", type=int, default=DEFAULT_TS_PORT, help="LongMynd TS UDP destination port")
    parser.add_argument("--status-host", default=DEFAULT_STATUS_HOST, help="LongMynd status UDP destination/listen host")
    parser.add_argument("--status-port", type=int, default=DEFAULT_STATUS_PORT, help="LongMynd status UDP destination/listen port")
    parser.add_argument("--out", default=str(repo_root / "QO-100-test" / "reports"), help="report root directory")
    parser.add_argument("--run-id", default=datetime.now().strftime("%Y%m%d-%H%M%S"), help="report run id")
    parser.add_argument("--plan-only", action="store_true", help="print selected plan as CSV and exit")
    parser.add_argument("--write-plan", help="write selected plan CSV to this path")
    parser.add_argument("--limit", type=int, help="limit number of selected plan entries")
    parser.add_argument("--start-index", type=int, default=1, help="start from this plan sequence number")
    parser.add_argument("--mode", help="comma-separated mode filter, e.g. Beacon,Wide,Narrow")
    parser.add_argument("--min-symbol-rate", type=int, help="minimum symbol rate in KS/s")
    parser.add_argument("--max-symbol-rate", type=int, help="maximum symbol rate in KS/s")
    args = parser.parse_args(argv)
    args.repo_root = str(Path(args.repo_root).resolve())
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    plan = select_plan(build_plan(), args)

    if args.write_plan:
        plan_path = Path(args.write_plan)
        plan_path.parent.mkdir(parents=True, exist_ok=True)
        with plan_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(
                handle,
                fieldnames=["sequence", "mode", "channel", "downlink_khz", "if_khz", "symbol_rate_ks", "note"],
            )
            writer.writeheader()
            for entry in plan:
                writer.writerow(
                    {
                        "sequence": entry.sequence,
                        "mode": entry.mode,
                        "channel": entry.channel,
                        "downlink_khz": entry.downlink_khz,
                        "if_khz": entry.downlink_khz - args.lo_khz,
                        "symbol_rate_ks": entry.symbol_rate_ks,
                        "note": entry.note,
                    }
                )

    if args.plan_only:
        print_plan(plan, args.lo_khz)
        return 0

    report_root = Path(args.out)
    run_dir = report_root / args.run_id
    raw_dir = run_dir / "raw-status"
    console_dir = run_dir / "console"
    raw_dir.mkdir(parents=True, exist_ok=True)
    console_dir.mkdir(parents=True, exist_ok=True)

    rows: List[Dict[str, object]] = []
    jsonl_path = run_dir / "results.jsonl"
    total_selected = len(plan)
    with jsonl_path.open("w", encoding="utf-8") as jsonl:
        for selected_index, entry in enumerate(plan, start=1):
            print(
                f"[{selected_index:03d}/{total_selected:03d}] seq={entry.sequence:03d} "
                f"{entry.mode} ch{entry.channel} DL={entry.downlink_khz} kHz "
                f"IF={entry.downlink_khz - args.lo_khz} kHz SR={entry.symbol_rate_ks} KS/s",
                flush=True,
            )
            row = run_entry(entry, args, run_dir, raw_dir, console_dir)
            rows.append(row)
            jsonl.write(json.dumps(row, sort_keys=True) + "\n")
            jsonl.flush()
            print(
                f"  received={row['received']} state={row['lock_state_name']} "
                f"MER={row['best_mer_db']} service={row['service_name']}",
                flush=True,
            )

    write_csv(run_dir / "summary.csv", rows)
    write_markdown(run_dir / "summary.md", rows)
    write_analysis(run_dir / "analysis.md", rows)
    print(f"Reports written to {run_dir}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
