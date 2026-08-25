#!/usr/bin/env python3
"""Print the release version or a traceable local-build version."""

import argparse
import datetime
import json
import os
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def firmware_version() -> str:
    explicit = os.environ.get("TAMAPOKE_VERSION")
    if explicit is not None:
        if not explicit or any(ord(char) < 32 for char in explicit):
            raise SystemExit("TAMAPOKE_VERSION must be a non-empty single line")
        return explicit

    commit = subprocess.check_output(
        ["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, text=True
    ).strip()
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime(
        "%Y%m%dT%H%M%SZ"
    )
    return f"{commit}-{timestamp}"


parser = argparse.ArgumentParser()
parser.add_argument(
    "--cpp-define",
    action="store_true",
    help="emit one compiler argument defining FW_VERSION",
)
args = parser.parse_args()
version = firmware_version()
print(f"-DFW_VERSION={json.dumps(version)}" if args.cpp_define else version)
