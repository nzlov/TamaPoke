#!/usr/bin/env python3
"""Collect labelled QMI8658 traces from the standalone calibration firmware."""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path


TOKEN = re.compile(r"[A-Za-z0-9._-]+")


class ProtocolError(RuntimeError):
    pass


def read_line(port, deadline: float) -> str:
    while time.monotonic() < deadline:
        line = port.readline().decode(errors="replace").strip()
        if line:
            return line
    raise ProtocolError("timed out waiting for the board")


def command(port, request: str, timeout: float) -> list[str]:
    port.write((request + "\n").encode())
    deadline = time.monotonic() + timeout
    lines: list[str] = []
    while True:
        line = read_line(port, deadline)
        if line.startswith("ERR "):
            raise ProtocolError(line)
        if line == "DONE":
            return lines
        lines.append(line)


def wait_for_trace(port, timeout: float = 6.0) -> None:
    time.sleep(3.2)
    deadline = time.monotonic() + timeout
    while True:
        rows = command(port, "MOTION STATUS", 1.5)
        status = next((row for row in rows if row.startswith("MOTION_STATUS\t")), "")
        if status.split("\t")[1:2] == ["COMPLETE"]:
            return
        if time.monotonic() >= deadline:
            raise ProtocolError("motion trace did not complete")
        time.sleep(0.1)


def dump_trace(port) -> list[str]:
    offset = 0
    dump: list[str] = []
    while True:
        rows = command(port, f"MOTION DUMP {offset}", 4.0)
        chunks = [row.split("\t") for row in rows
                  if row.startswith("MOTION_CHUNK\t")]
        if len(chunks) != 1 or len(chunks[0]) != 3:
            raise ProtocolError("dump chunk is missing its continuation row")
        next_offset, total = int(chunks[0][1]), int(chunks[0][2])
        if next_offset < offset or next_offset > total:
            raise ProtocolError("dump chunk returned an invalid offset")
        dump.extend(row for row in rows if not row.startswith("MOTION_CHUNK\t"))
        if next_offset == total:
            return dump
        if next_offset == offset:
            raise ProtocolError("dump chunk made no progress")
        offset = next_offset


def parse_dump(lines: list[str], session: str, label: str, trial: int,
               expected: int) -> dict[str, int]:
    meta = [line.split("\t") for line in lines if line.startswith("MOTION_META\t")]
    config = [line.split("\t") for line in lines if line.startswith("MOTION_CONFIG\t")]
    samples = [line.split("\t") for line in lines if line.startswith("MOTION_SAMPLE\t")]
    end = [line.split("\t") for line in lines if line.startswith("MOTION_END\t")]
    if len(meta) != 1 or len(config) != 1 or len(end) != 1:
        raise ProtocolError("dump is missing a unique META, CONFIG, or END row")
    if len(meta[0]) != 11 or len(config[0]) != 6 or len(end[0]) != 5:
        raise ProtocolError("dump row has an unsupported schema")
    if meta[0][1] != "2":
        raise ProtocolError(f"unsupported motion schema {meta[0][1]!r}")
    if (meta[0][3], meta[0][4], int(meta[0][5]), int(meta[0][6])) != (
            session, label, trial, expected):
        raise ProtocolError("dump metadata does not match the requested trial")

    for sequence, row in enumerate(samples):
        if len(row) != 9 or int(row[1]) != sequence:
            raise ProtocolError("sample sequence is missing or out of order")
        int(row[2])
        for value in row[3:]:
            float(value)

    if int(meta[0][7]) not in (0, 1):
        raise ProtocolError("META row has an invalid calibration flag")
    for value in meta[0][8:]:
        int(value)
    int(config[0][1])
    float(config[0][2])
    int(config[0][3])
    float(config[0][4])
    int(config[0][5])

    count = int(end[0][1])
    dropped = int(end[0][2])
    int(end[0][3])
    int(end[0][4])
    if count != len(samples):
        raise ProtocolError("END row does not match the samples")

    long_gaps = 0
    for previous, current in zip(samples, samples[1:]):
        dt = (int(current[2]) - int(previous[2])) & 0xFFFFFFFF
        if dt > 60:
            long_gaps += 1
    return {
        "count": count,
        "dropped": dropped,
        "long_gaps": long_gaps,
    }


def valid_token(value: str, maximum: int) -> str:
    if len(value) > maximum or not TOKEN.fullmatch(value):
        raise argparse.ArgumentTypeError(
            f"use 1-{maximum} ASCII letters, digits, dot, underscore, or dash"
        )
    return value


def notify_move(label: str, trial: int) -> None:
    result = subprocess.run(
        [
            "notify-send",
            "--app-name=TamaPoke Motion Calibration",
            "--urgency=critical",
            "--expire-time=5000",
            "开始动作",
            f"{label} #{trial}：立即动作并保持终点",
        ],
        check=False,
    )
    if result.returncode != 0:
        raise ProtocolError("desktop notification failed")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="collect labelled three-second motion traces"
    )
    parser.add_argument("--port", required=True,
                        help="stable serial path, preferably /dev/serial/by-id/...")
    parser.add_argument("--out", required=True, type=Path,
                        help="TSV dataset outside the repository; trials are appended")
    parser.add_argument("--session", required=True,
                        type=lambda value: valid_token(value, 15))
    parser.add_argument("--label", required=True,
                        type=lambda value: valid_token(value, 23))
    parser.add_argument("--expected", required=True, choices=("throw", "reject"))
    parser.add_argument("--trials", type=int, default=1)
    parser.add_argument("--start-trial", type=int, default=1)
    parser.add_argument("--calibrate", action="store_true",
                        help="run the stationary QMI8658 calibration first")
    args = parser.parse_args()
    if args.trials < 1 or args.start_trial < 1 or args.start_trial + args.trials > 65536:
        parser.error("trial numbers must stay within 1..65535")

    try:
        import serial
    except ImportError:
        parser.error("pyserial is required (python3 -m pip install pyserial)")

    expected = 1 if args.expected == "throw" else 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=0.25) as port:
        time.sleep(1.5)
        port.reset_input_buffer()

        if args.calibrate:
            input("Place the board flat and still, then press Enter to calibrate: ")
            rows = command(port, "MOTION CAL", 8.0)
            calibration = next((row for row in rows if row.startswith("MOTION_CAL\t")), None)
            if calibration is None:
                raise ProtocolError("board did not return calibration gains")
            print(calibration.replace("\t", " "))

        for trial in range(args.start_trial, args.start_trial + args.trials):
            input(f"Trial {trial} ({args.label}): press Enter when ready: ")
            request = f"MOTION TRACE {args.session} {args.label} {trial} {expected}"
            ready = command(port, request, 3.0)
            if not any(row.startswith("MOTION_READY\t") for row in ready):
                raise ProtocolError("board did not arm the motion trace")
            # Let the detector's 200 ms selection-tap guard expire before the cue.
            time.sleep(0.25)
            notify_move(args.label, trial)
            print("Move now; keep hold of the board.", flush=True)
            wait_for_trace(port)
            dump = dump_trace(port)
            result = parse_dump(dump, args.session, args.label, trial, expected)

            with args.out.open("a", encoding="utf-8", newline="\n") as dataset:
                for row in dump:
                    if row.startswith(("MOTION_META\t", "MOTION_CONFIG\t",
                                       "MOTION_SAMPLE\t", "MOTION_END\t")):
                        dataset.write(row + "\n")
                dataset.flush()
            command(port, "MOTION CLEAR", 3.0)

            print(f"saved trial {trial}: {result['count']} samples, "
                  f"dropped={result['dropped']}, "
                  f"gaps>60ms={result['long_gaps']}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ProtocolError, ValueError) as error:
        raise SystemExit(f"motion capture failed: {error}")
