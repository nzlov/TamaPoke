#!/usr/bin/env python3
"""Protocol checks for tools/motion_capture.py; no serial device required."""

import sys
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

from motion_capture import (  # noqa: E402
    ProtocolError,
    dump_trace,
    notify_move,
    parse_dump,
)


def check(ok: bool, message: str) -> None:
    print(f"{'PASS' if ok else 'FAIL'}  {message}")
    if not ok:
        raise SystemExit(1)


rows = [
    "MOTION_META\t2\tmotion-calibration-v1\tA\tthrow-normal\t7\t1\t1\t10\t11\t12",
    "MOTION_CONFIG\t4\t125.0\t1024\t112.1\t3",
    "MOTION_SAMPLE\t0\t1000\t0.0\t0.0\t1.0\t0.0\t0.0\t0.0",
    "MOTION_SAMPLE\t1\t1010\t0.1\t0.0\t1.1\t0.0\t120.0\t0.0",
    "MOTION_END\t2\t0\t900\t3900",
]
result = parse_dump(rows, "A", "throw-normal", 7, 1)
check(result == {"count": 2, "dropped": 0, "long_gaps": 0},
      "a complete labelled raw trace is accepted")

broken = rows.copy()
broken[3] = broken[3].replace("MOTION_SAMPLE\t1", "MOTION_SAMPLE\t2")
try:
    parse_dump(broken, "A", "throw-normal", 7, 1)
except ProtocolError:
    check(True, "a missing sample sequence is rejected")
else:
    check(False, "a missing sample sequence is rejected")

try:
    parse_dump(rows, "B", "throw-normal", 7, 1)
except ProtocolError:
    check(True, "metadata from another session is rejected")
else:
    check(False, "metadata from another session is rejected")


class FakePort:
    def __init__(self) -> None:
        self.pending: list[str] = []

    def write(self, request: bytes) -> None:
        command = request.decode().strip()
        if command == "MOTION DUMP 0":
            self.pending = rows[:3] + ["MOTION_CHUNK\t1\t2", "DONE"]
        elif command == "MOTION DUMP 1":
            self.pending = rows[3:] + ["MOTION_CHUNK\t2\t2", "DONE"]
        else:
            self.pending = ["ERR UNEXPECTED", "DONE"]

    def readline(self) -> bytes:
        return ((self.pending.pop(0) + "\n").encode() if self.pending else b"")


chunked = dump_trace(FakePort())
check(chunked == rows, "chunked serial dumps are reassembled without protocol rows")

with patch("motion_capture.subprocess.run") as notify:
    notify.return_value.returncode = 0
    notify_move("flick-hold-soft", 14)
    arguments = notify.call_args.args[0]
    check(arguments[-2:] == ["开始动作", "flick-hold-soft #14：立即动作并保持终点"],
          "the movement cue is sent as a desktop notification")
