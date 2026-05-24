#!/usr/bin/env python3
"""Find current QO-100 DATV signals from the BATC WB FFT websocket."""

from __future__ import annotations

import argparse
import base64
import csv
import json
import os
import socket
import ssl
import struct
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable, List, Optional, Sequence


FFT_HOST = "eshail.batc.org.uk"
FFT_PATH = "/wb/fft"
FFT_SOURCE_URL = "https://eshail.batc.org.uk/wb/"
DEFAULT_PROTOCOL = "fft_fast"
DEFAULT_SECONDS = 6.0
DEFAULT_THRESHOLD = 13500
DEFAULT_NOISE_LEVEL = 11000
DEFAULT_MIN_SEEN = 10
DEFAULT_LO_KHZ = 9_360_000

START_FREQ_MHZ = 490.5
SPAN_MHZ = 9.0
DOWNLINK_BASE_MHZ = 10_000.0
UPLINK_BASE_MHZ = 1_910.5
SCALE_DB = 3276.8


@dataclass
class SignalDetection:
    frame_index: int
    downlink_khz: int
    uplink_khz: int
    symbol_rate_ks: int
    raw_width_khz: float
    strength: float
    start_bin: int
    end_bin: int
    bin_count: int


class U16WebSocket:
    def __init__(self, host: str, path: str, protocol: str, timeout: float) -> None:
        self.host = host
        self.path = path
        self.protocol = protocol
        self.timeout = timeout
        self.sock: Optional[ssl.SSLSocket] = None

    def __enter__(self) -> "U16WebSocket":
        self.connect()
        return self

    def __exit__(self, _exc_type, _exc, _tb) -> None:
        self.close()

    def connect(self) -> None:
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        request = (
            f"GET {self.path} HTTP/1.1\r\n"
            f"Host: {self.host}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            f"Sec-WebSocket-Protocol: {self.protocol}\r\n"
            f"Origin: https://{self.host}\r\n"
            "\r\n"
        )
        raw = socket.create_connection((self.host, 443), timeout=self.timeout)
        raw.settimeout(self.timeout)
        self.sock = ssl.create_default_context().wrap_socket(raw, server_hostname=self.host)
        self.sock.sendall(request.encode("ascii"))
        response = b""
        while b"\r\n\r\n" not in response:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise RuntimeError("websocket handshake closed before response headers")
            response += chunk
        headers = response.split(b"\r\n\r\n", 1)[0].decode("iso-8859-1", errors="replace")
        if " 101 " not in headers.splitlines()[0]:
            raise RuntimeError(f"websocket handshake failed: {headers.splitlines()[0]}")

    def close(self) -> None:
        if self.sock is not None:
            try:
                self.sock.close()
            finally:
                self.sock = None

    def recv_u16_frame(self) -> Optional[List[int]]:
        if self.sock is None:
            raise RuntimeError("websocket is not connected")
        header = self._recv_exact(2)
        if not header:
            return None
        opcode = header[0] & 0x0F
        length = header[1] & 0x7F
        masked = bool(header[1] & 0x80)
        if length == 126:
            length = struct.unpack("!H", self._recv_exact(2))[0]
        elif length == 127:
            length = struct.unpack("!Q", self._recv_exact(8))[0]
        mask = self._recv_exact(4) if masked else b""
        payload = self._recv_exact(length)
        if masked:
            payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        if opcode == 8:
            return None
        if opcode != 2 or len(payload) < 2:
            return []
        count = len(payload) // 2
        return list(struct.unpack("<" + "H" * count, payload[: count * 2]))

    def _recv_exact(self, size: int) -> bytes:
        assert self.sock is not None
        data = b""
        while len(data) < size:
            chunk = self.sock.recv(size - len(data))
            if not chunk:
                raise RuntimeError("websocket closed while reading frame")
            data += chunk
        return data


def align_symbol_rate(width_mhz: float) -> float:
    if width_mhz < 0.022:
        return 0.0
    if width_mhz < 0.060:
        return 0.035
    if width_mhz < 0.086:
        return 0.066
    if width_mhz < 0.185:
        return 0.125
    if width_mhz < 0.277:
        return 0.250
    if width_mhz < 0.388:
        return 0.333
    if width_mhz < 0.700:
        return 0.500
    if width_mhz < 1.2:
        return 1.000
    if width_mhz < 1.6:
        return 1.500
    if width_mhz < 2.2:
        return 2.000
    return round(width_mhz * 5.0) / 5.0


def rounded_display_downlink_khz(downlink_khz: int, symbol_rate_ks: int) -> int:
    step_khz = 25 if symbol_rate_ks >= 700 else 12.5
    return int(round(round(downlink_khz / step_khz) * step_khz))


def canonical_frequencies(downlink_khz: int, uplink_khz: int, symbol_rate_ks: int) -> tuple[int, int]:
    if 10_491_000 <= downlink_khz <= 10_492_000 and symbol_rate_ks >= 1000:
        return 10_491_500, 2_402_000
    return downlink_khz, uplink_khz


def detect_signals(
    fft_data: Sequence[int],
    frame_index: int,
    threshold: int,
    noise_level: int,
) -> List[SignalDetection]:
    detections: List[SignalDetection] = []
    in_signal = False
    start_signal = 0
    length = len(fft_data)

    def finish(end_signal: int) -> None:
        nonlocal start_signal
        if end_signal <= start_signal + 1:
            return
        inner_start = int(start_signal + 0.3 * (end_signal - start_signal))
        inner_end = int(start_signal + 0.7 * (end_signal - start_signal))
        if inner_end <= inner_start:
            return
        strength = sum(fft_data[inner_start:inner_end]) / float(inner_end - inner_start)
        top_start = start_signal
        top_end = end_signal
        top_limit = 0.75 * (strength - noise_level)
        while top_start < top_end and (fft_data[top_start] - noise_level) < top_limit:
            top_start += 1
        while top_end > top_start and (fft_data[top_end - 1] - noise_level) < top_limit:
            top_end -= 1
        if top_end <= top_start:
            top_start = start_signal
            top_end = end_signal

        width_mhz = (top_end - top_start) * (SPAN_MHZ / length)
        symbol_rate_mhz = align_symbol_rate(width_mhz)
        if symbol_rate_mhz <= 0.0:
            return
        mid_signal = top_start + ((top_end - top_start) / 2.0)
        offset_mhz = START_FREQ_MHZ + (((mid_signal + 1.0) / length) * SPAN_MHZ)
        downlink_mhz = DOWNLINK_BASE_MHZ + offset_mhz
        uplink_mhz = UPLINK_BASE_MHZ + offset_mhz
        symbol_rate_ks = int(round(1000.0 * symbol_rate_mhz))
        downlink_khz = rounded_display_downlink_khz(int(round(downlink_mhz * 1000.0)), symbol_rate_ks)
        uplink_khz = int(round(uplink_mhz * 1000.0))
        downlink_khz, uplink_khz = canonical_frequencies(downlink_khz, uplink_khz, symbol_rate_ks)
        detections.append(
            SignalDetection(
                frame_index=frame_index,
                downlink_khz=downlink_khz,
                uplink_khz=uplink_khz,
                symbol_rate_ks=symbol_rate_ks,
                raw_width_khz=width_mhz * 1000.0,
                strength=strength,
                start_bin=top_start,
                end_bin=top_end,
                bin_count=length,
            )
        )

    for index in range(2, length):
        average = (fft_data[index] + fft_data[index - 1] + fft_data[index - 2]) / 3.0
        if not in_signal and average > threshold:
            in_signal = True
            start_signal = index
        elif in_signal and average < threshold:
            in_signal = False
            finish(index)

    if in_signal:
        finish(length)
    return detections


def station_band(downlink_khz: int) -> str:
    if downlink_khz < 10_492_000:
        return "beacon"
    if downlink_khz < 10_497_000:
        return "wide/narrow"
    return "narrow"


def cluster_key(detection: SignalDetection) -> tuple[int, int]:
    window = 25 if detection.symbol_rate_ks >= 700 else 12
    return (int(round(detection.downlink_khz / window) * window), detection.symbol_rate_ks)


def summarize_detections(
    detections: Iterable[SignalDetection],
    frames_seen: int,
    min_seen: int,
    lo_khz: int,
    include_beacon: bool,
) -> List[dict]:
    clusters: dict[tuple[int, int], List[SignalDetection]] = {}
    for detection in detections:
        if not include_beacon and detection.downlink_khz < 10_492_000:
            continue
        clusters.setdefault(cluster_key(detection), []).append(detection)

    beacon_strengths = [
        item.strength
        for group in clusters.values()
        for item in group
        if item.downlink_khz < 10_492_000 and item.symbol_rate_ks >= 1000
    ]
    beacon_strength = sum(beacon_strengths) / len(beacon_strengths) if beacon_strengths else None

    stations: List[dict] = []
    for group in clusters.values():
        seen_frames = len({item.frame_index for item in group})
        if seen_frames < min_seen:
            continue
        avg_downlink = sum(item.downlink_khz for item in group) / len(group)
        avg_uplink = sum(item.uplink_khz for item in group) / len(group)
        avg_width = sum(item.raw_width_khz for item in group) / len(group)
        avg_strength = sum(item.strength for item in group) / len(group)
        symbol_rate_ks = int(round(sum(item.symbol_rate_ks for item in group) / len(group)))
        downlink_khz = rounded_display_downlink_khz(int(round(avg_downlink)), symbol_rate_ks)
        uplink_khz = int(round(avg_uplink))
        downlink_khz, uplink_khz = canonical_frequencies(downlink_khz, uplink_khz, symbol_rate_ks)
        relative_db = None
        overpower = False
        if beacon_strength is not None:
            relative_db = (avg_strength - beacon_strength) / SCALE_DB
            overpower = symbol_rate_ks >= 1000 and avg_strength > (beacon_strength - (0.75 * SCALE_DB))
        stations.append(
            {
                "downlink_khz": downlink_khz,
                "if_khz": downlink_khz - lo_khz,
                "uplink_khz": uplink_khz,
                "symbol_rate_ks": symbol_rate_ks,
                "raw_width_khz": round(avg_width, 1),
                "strength": round(avg_strength, 1),
                "relative_db_to_beacon": None if relative_db is None else round(relative_db, 1),
                "seen_frames": seen_frames,
                "total_frames": frames_seen,
                "band": station_band(downlink_khz),
                "overpower": overpower,
                "source": "BATC WB FFT",
            }
        )
    return sorted(stations, key=lambda item: (item["downlink_khz"], item["symbol_rate_ks"]))


def discover_stations(args: argparse.Namespace) -> dict:
    deadline = time.monotonic() + args.seconds
    detections: List[SignalDetection] = []
    frames_seen = 0
    with U16WebSocket(args.host, args.path, args.protocol, args.timeout) as websocket:
        while time.monotonic() < deadline:
            frame = websocket.recv_u16_frame()
            if frame is None:
                break
            if not frame:
                continue
            frames_seen += 1
            detections.extend(detect_signals(frame, frames_seen, args.threshold, args.noise_level))

    stations = summarize_detections(
        detections,
        frames_seen=frames_seen,
        min_seen=args.min_seen,
        lo_khz=args.lo_khz,
        include_beacon=args.include_beacon,
    )
    return {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "source_url": FFT_SOURCE_URL,
        "host": args.host,
        "path": args.path,
        "protocol": args.protocol,
        "seconds": args.seconds,
        "threshold": args.threshold,
        "noise_level": args.noise_level,
        "frames": frames_seen,
        "stations": stations,
    }


def print_table(report: dict) -> None:
    stations = report["stations"]
    if not stations:
        print("No active DATV signals detected.")
        return
    headers = [
        "downlink_khz",
        "if_khz",
        "uplink_khz",
        "sr_ks",
        "width_khz",
        "strength",
        "dBb",
        "seen",
        "band",
        "note",
    ]
    print(" | ".join(headers))
    print(" | ".join(["---"] * len(headers)))
    for station in stations:
        note = "over-power" if station["overpower"] else ""
        rel = station["relative_db_to_beacon"]
        print(
            " | ".join(
                [
                    str(station["downlink_khz"]),
                    str(station["if_khz"]),
                    str(station["uplink_khz"]),
                    str(station["symbol_rate_ks"]),
                    str(station["raw_width_khz"]),
                    str(station["strength"]),
                    "" if rel is None else f"{rel:.1f}",
                    f"{station['seen_frames']}/{station['total_frames']}",
                    station["band"],
                    note,
                ]
            )
        )


def write_csv(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = [
        "downlink_khz",
        "if_khz",
        "uplink_khz",
        "symbol_rate_ks",
        "raw_width_khz",
        "strength",
        "relative_db_to_beacon",
        "seen_frames",
        "total_frames",
        "band",
        "overpower",
        "source",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(report["stations"])


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seconds", type=float, default=DEFAULT_SECONDS, help="FFT collection time")
    parser.add_argument("--host", default=FFT_HOST, help="WB FFT websocket host")
    parser.add_argument("--path", default=FFT_PATH, help="WB FFT websocket path")
    parser.add_argument("--protocol", default=DEFAULT_PROTOCOL, choices=["fft", "fft_fast"], help="websocket subprotocol")
    parser.add_argument("--threshold", type=int, default=DEFAULT_THRESHOLD, help="signal threshold in FFT units")
    parser.add_argument("--noise-level", type=int, default=DEFAULT_NOISE_LEVEL, help="noise level in FFT units")
    parser.add_argument("--min-seen", type=int, default=DEFAULT_MIN_SEEN, help="minimum frames in which a station must appear")
    parser.add_argument("--lo-khz", type=int, default=DEFAULT_LO_KHZ, help="receive LO used to compute LongMynd IF")
    parser.add_argument("--timeout", type=float, default=5.0, help="socket timeout")
    parser.add_argument("--json", action="store_true", help="write machine-readable JSON to stdout")
    parser.add_argument("--csv", type=Path, help="also write station rows as CSV")
    parser.add_argument("--no-beacon", dest="include_beacon", action="store_false", help="exclude the QO-100 beacon")
    parser.set_defaults(include_beacon=True)
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        report = discover_stations(args)
    except Exception as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if args.csv:
        write_csv(args.csv, report)
    if args.json:
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        print()
    else:
        print_table(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
