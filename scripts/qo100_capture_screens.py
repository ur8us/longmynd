#!/usr/bin/env python3
"""Tune LongMynd through active QO-100 stations and save video screenshots."""

from __future__ import annotations

import argparse
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Sequence

import qo100_wb_find


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCREENSHOT_DIR = Path(__file__).resolve().parent / "screenshots"
DEFAULT_SCREENSHOT_BY_CALLSIGN_DIR = Path(__file__).resolve().parent / "screenshots-by-callsign"
DEFAULT_LO_KHZ = 9_360_000
DEFAULT_TS_HOST = "127.0.0.1"
DEFAULT_TS_PORT = 10000
DEFAULT_STATUS_HOST = "127.0.0.1"
DEFAULT_STATUS_PORT = 10001

STATUS_NAMES = {
    1: "state",
    6: "carrier_frequency_khz",
    9: "symbol_rate_sps",
    11: "ber",
    12: "cn_x10",
    13: "service_name",
    14: "service_provider_name",
    18: "modcod",
    21: "ldpc_error_count",
    22: "bch_error_count",
    23: "bch_uncorrected",
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


def parse_status_line(line: str) -> Optional[tuple[int, str]]:
    text = line.strip()
    if not text.startswith("$") or "," not in text:
        return None
    status_id, value = text[1:].split(",", 1)
    try:
        return int(status_id), value.strip()
    except ValueError:
        return None


def update_status(status: Dict[int, str], packet: bytes) -> None:
    text = packet.decode("utf-8", errors="replace")
    for line in text.splitlines():
        parsed = parse_status_line(line)
        if parsed is not None:
            status[parsed[0]] = parsed[1]


def status_int(status: Dict[int, str], status_id: int) -> Optional[int]:
    try:
        return int(status[status_id])
    except (KeyError, ValueError):
        return None


def status_summary(status: Dict[int, str]) -> dict:
    modcod = status_int(status, 18)
    cn_x10 = status_int(status, 12)
    state = status_int(status, 1)
    return {
        "state": state,
        "service_name": status.get(13, ""),
        "service_provider_name": status.get(14, ""),
        "cn_db": None if cn_x10 is None else round(cn_x10 / 10.0, 1),
        "ber": status_int(status, 11),
        "modcod": None if modcod is None else DVBS2_MODCOD.get(modcod, str(modcod)),
        "ldpc_error_count": status_int(status, 21),
        "bch_error_count": status_int(status, 22),
        "bch_uncorrected": status_int(status, 23),
        "observed_carrier_khz": status_int(status, 6),
        "observed_symbol_rate_sps": status_int(status, 9),
    }


def wait_for_status(sock: socket.socket, process: subprocess.Popen[str], seconds: float) -> Dict[int, str]:
    status: Dict[int, str] = {}
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if process.poll() is not None:
            break
        remaining = max(0.0, deadline - time.monotonic())
        readable, _, _ = select.select([sock], [], [], min(0.25, remaining))
        if readable:
            packet, _addr = sock.recvfrom(8192)
            update_status(status, packet)
        state = status_int(status, 1)
        if state in (3, 4) and (status.get(13) or time.monotonic() > deadline - 2.0):
            break
    return status


def stop_process(process: subprocess.Popen[str], grace_s: float = 2.0) -> None:
    if process.poll() is not None:
        return
    process.send_signal(signal.SIGTERM)
    try:
        process.wait(timeout=grace_s)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=grace_s)


def safe_token(value: str, fallback: str) -> str:
    value = value.strip() or fallback
    value = re.sub(r"[^A-Za-z0-9_.+-]+", "_", value)
    value = value.strip("._-")
    return value[:48] or fallback


def screenshot_output_path(args: argparse.Namespace, callsign: str, symbol_rate_ks: int) -> Path:
    filename = f"{datetime.now().strftime('%Y%m%d-%H%M%S')}-{callsign}-{symbol_rate_ks}ks.png"
    if args.group_by_callsign:
        return args.screenshot_dir / callsign / filename
    return args.screenshot_dir / filename


def existing_longmynd_pids() -> List[int]:
    try:
        result = subprocess.run(["pgrep", "-x", "longmynd"], text=True, capture_output=True, check=False)
    except FileNotFoundError:
        return []
    pids = []
    for line in result.stdout.splitlines():
        try:
            pids.append(int(line.strip()))
        except ValueError:
            pass
    return [pid for pid in pids if pid != os.getpid()]


def stop_existing_longmynd() -> None:
    pids = existing_longmynd_pids()
    for pid in pids:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        if not existing_longmynd_pids():
            return
        time.sleep(0.1)
    for pid in existing_longmynd_pids():
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass


def png_has_detail(output: Path, min_stddev: float) -> bool:
    if min_stddev <= 0:
        return True
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return True
    try:
        with Image.open(output) as image:
            small = image.convert("RGB").resize((160, 90))
            stat = ImageStat.Stat(small)
    except Exception:
        return False
    return max(stat.stddev) >= min_stddev


def ffmpeg_screenshot(
    ts_host: str,
    ts_port: int,
    output: Path,
    timeout_s: float,
    attempts: int,
    retry_sleep: float,
    min_detail_stddev: float,
) -> bool:
    url = f"udp://{ts_host}:{ts_port}?overrun_nonfatal=1&fifo_size=50000000&timeout={int(timeout_s * 1000000)}"
    cmd = [
        "ffmpeg",
        "-hide_banner",
        "-loglevel",
        "error",
        "-y",
        "-probesize",
        "1000000",
        "-analyzeduration",
        "3000000",
        "-i",
        url,
        "-frames:v",
        "1",
        str(output),
    ]
    for attempt in range(1, max(1, attempts) + 1):
        output.parent.mkdir(parents=True, exist_ok=True)
        if output.exists():
            output.unlink()
        try:
            subprocess.run(
                cmd,
                check=True,
                timeout=timeout_s + 5.0,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        except (subprocess.CalledProcessError, subprocess.TimeoutExpired, FileNotFoundError):
            pass
        if output.exists() and output.stat().st_size > 0 and png_has_detail(output, min_detail_stddev):
            return True
        if output.exists():
            output.unlink()
        if attempt < attempts:
            time.sleep(retry_sleep)
    return False


def cleanup_empty_group_dir(args: argparse.Namespace, output: Path) -> None:
    if not args.group_by_callsign:
        return
    try:
        output.parent.rmdir()
    except OSError:
        pass


def tune_and_capture(station: dict, args: argparse.Namespace, cycle_started: datetime) -> dict:
    downlink_khz = int(station["downlink_khz"])
    symbol_rate_ks = int(station["symbol_rate_ks"])
    if_khz = downlink_khz - args.lo_khz
    args.screenshot_dir.mkdir(parents=True, exist_ok=True)
    log_dir = args.screenshot_dir / "logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    log_file = log_dir / f"{cycle_started.strftime('%Y%m%d-%H%M%S')}-dl{downlink_khz}-sr{symbol_rate_ks}.log"

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.status_host, args.status_port))

    cmd = [
        str(args.longmynd),
        "-N",
        args.nim,
        "-i",
        args.ts_host,
        str(args.ts_port),
        "-I",
        args.status_host,
        str(args.status_port),
    ]
    if args.low_sr:
        cmd += ["-L", args.low_sr]
    cmd += [str(if_khz), str(symbol_rate_ks)]

    with log_file.open("w", encoding="utf-8") as log_handle:
        log_handle.write("$ " + " ".join(cmd) + "\n\n")
        log_handle.flush()
        process = subprocess.Popen(
            cmd,
            cwd=args.repo_root,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            text=True,
        )
        try:
            status = wait_for_status(sock, process, args.lock_wait)
            summary = status_summary(status)
            locked = summary["state"] in (3, 4)
            service = safe_token(summary["service_name"], f"DL{downlink_khz}")
            output = screenshot_output_path(args, service, symbol_rate_ks)
            captured = False
            if locked:
                captured = ffmpeg_screenshot(
                    args.ts_host,
                    args.ts_port,
                    output,
                    args.capture_timeout,
                    args.capture_attempts,
                    args.capture_retry_sleep,
                    args.min_detail_stddev,
                )
            if not captured and output.exists():
                output.unlink()
            if not captured:
                cleanup_empty_group_dir(args, output)
            result = {
                "downlink_khz": downlink_khz,
                "if_khz": if_khz,
                "symbol_rate_ks": symbol_rate_ks,
                "locked": locked,
                "captured": captured,
                "screenshot": str(output) if captured else "",
                "log_file": str(log_file),
                **summary,
            }
            print(json.dumps(result, sort_keys=True), flush=True)
            return result
        finally:
            stop_process(process)
            sock.close()


def discover(args: argparse.Namespace) -> List[dict]:
    finder_args = argparse.Namespace(
        seconds=args.scan_seconds,
        host=args.fft_host,
        path=args.fft_path,
        protocol=args.fft_protocol,
        threshold=args.threshold,
        noise_level=args.noise_level,
        min_seen=args.min_seen,
        lo_khz=args.lo_khz,
        timeout=args.socket_timeout,
        include_beacon=args.include_beacon,
    )
    report = qo100_wb_find.discover_stations(finder_args)
    stations = report["stations"]
    if args.no_overpower:
        stations = [station for station in stations if not station.get("overpower")]
    if args.max_stations is not None:
        stations = stations[: args.max_stations]
    print(
        f"scan frames={report['frames']} stations={len(stations)} "
        f"threshold={report['threshold']}",
        flush=True,
    )
    return stations


def run_loop(args: argparse.Namespace) -> int:
    if args.stop_existing:
        stop_existing_longmynd()
    else:
        pids = existing_longmynd_pids()
        if pids:
            print(
                "ERROR: longmynd is already running. Stop it first or use --stop-existing.",
                file=sys.stderr,
            )
            return 2

    started = time.monotonic()
    cycle = 0
    results: List[dict] = []
    try:
        while True:
            if args.max_run_seconds is not None and time.monotonic() - started >= args.max_run_seconds:
                break
            cycle += 1
            print(f"cycle={cycle} discovering stations", flush=True)
            stations = discover(args)
            if not stations:
                time.sleep(args.empty_sleep)
                continue
            for station in stations:
                if args.max_run_seconds is not None and time.monotonic() - started >= args.max_run_seconds:
                    break
                print(
                    f"tune downlink={station['downlink_khz']} "
                    f"sr={station['symbol_rate_ks']}ks",
                    flush=True,
                )
                results.append(tune_and_capture(station, args, datetime.now()))
                time.sleep(args.between_stations)
    except KeyboardInterrupt:
        print("interrupted", flush=True)
    finally:
        summary_file = args.screenshot_dir / f"{datetime.now().strftime('%Y%m%d-%H%M%S')}-summary.json"
        args.screenshot_dir.mkdir(parents=True, exist_ok=True)
        summary_file.write_text(json.dumps(results, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        print(f"summary={summary_file}", flush=True)
    return 0


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--longmynd", type=Path, default=REPO_ROOT / "longmynd")
    parser.add_argument("--nim", default="earda")
    parser.add_argument("--lo-khz", type=int, default=DEFAULT_LO_KHZ)
    parser.add_argument("--low-sr", default="auto", choices=["auto", "on", "off", ""], help="LongMynd -L setting")
    parser.add_argument("--ts-host", default=DEFAULT_TS_HOST)
    parser.add_argument("--ts-port", type=int, default=DEFAULT_TS_PORT)
    parser.add_argument("--status-host", default=DEFAULT_STATUS_HOST)
    parser.add_argument("--status-port", type=int, default=DEFAULT_STATUS_PORT)
    parser.add_argument("--screenshot-dir", type=Path, help="screenshot output root")
    parser.add_argument("--group-by-callsign", action="store_true", help="write PNGs under screenshots-by-callsign/<callsign>/ by default")
    parser.add_argument("--scan-seconds", type=float, default=6.0)
    parser.add_argument("--lock-wait", type=float, default=14.0)
    parser.add_argument("--capture-timeout", type=float, default=25.0)
    parser.add_argument("--capture-attempts", type=int, default=3)
    parser.add_argument("--capture-retry-sleep", type=float, default=1.5)
    parser.add_argument("--min-detail-stddev", type=float, default=5.0, help="reject near-flat PNGs when Pillow is installed; use 0 to disable")
    parser.add_argument("--between-stations", type=float, default=1.0)
    parser.add_argument("--empty-sleep", type=float, default=10.0)
    parser.add_argument("--max-run-seconds", type=float, help="stop after this many seconds; default runs until Ctrl-C")
    parser.add_argument("--max-stations", type=int, help="limit stations per scan cycle")
    parser.add_argument("--stop-existing", action="store_true", help="terminate existing longmynd before starting")
    parser.add_argument("--include-beacon", action="store_true", default=True)
    parser.add_argument("--no-beacon", dest="include_beacon", action="store_false")
    parser.add_argument("--no-overpower", action="store_true", help="skip BATC overpower-marked stations")
    parser.add_argument("--fft-host", default=qo100_wb_find.FFT_HOST)
    parser.add_argument("--fft-path", default=qo100_wb_find.FFT_PATH)
    parser.add_argument("--fft-protocol", default=qo100_wb_find.DEFAULT_PROTOCOL, choices=["fft", "fft_fast"])
    parser.add_argument("--threshold", type=int, default=qo100_wb_find.DEFAULT_THRESHOLD)
    parser.add_argument("--noise-level", type=int, default=qo100_wb_find.DEFAULT_NOISE_LEVEL)
    parser.add_argument("--min-seen", type=int, default=qo100_wb_find.DEFAULT_MIN_SEEN)
    parser.add_argument("--socket-timeout", type=float, default=5.0)
    args = parser.parse_args(argv)
    args.repo_root = args.repo_root.resolve()
    args.longmynd = args.longmynd.resolve()
    if args.screenshot_dir is None:
        args.screenshot_dir = DEFAULT_SCREENSHOT_BY_CALLSIGN_DIR if args.group_by_callsign else DEFAULT_SCREENSHOT_DIR
    args.screenshot_dir = args.screenshot_dir.resolve()
    return args


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    if not args.longmynd.exists():
        print(f"ERROR: longmynd binary not found: {args.longmynd}", file=sys.stderr)
        return 1
    return run_loop(args)


if __name__ == "__main__":
    raise SystemExit(main())
