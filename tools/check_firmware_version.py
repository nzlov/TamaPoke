#!/usr/bin/env python3
"""Verify Pages channel triggering and firmware-version generation stay aligned."""

import os
import re
import subprocess
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "pages.yml"
VERSION_TOOL = ROOT / "tools" / "firmware_version.py"


def fail(message: str) -> None:
    raise SystemExit(message)


workflow = WORKFLOW.read_text(encoding="utf-8")
trigger = workflow.split("permissions:", 1)[0]
for fragment in ("release:", "types: [published]", "push:", "branches: [main]",
                 "workflow_dispatch:"):
    if fragment not in trigger:
        fail(f"Pages trigger is missing {fragment!r}")
for fragment in (
    "channel: [stable, latest]",
    "stable_ref=${{ github.event.release.tag_name }}",
    "gh api \"repos/$GITHUB_REPOSITORY/releases/latest\" --jq .tag_name",
    "needs.refs.outputs.stable_ref",
    "needs.refs.outputs.latest_ref",
    "needs.refs.outputs.stable_version",
    'export TAMAPOKE_VERSION="$STABLE_VERSION"',
    "unset TAMAPOKE_VERSION",
):
    if fragment not in workflow:
        fail(f"Pages channel version wiring is missing {fragment!r}")
if "needs.refs.outputs.stable_version || ''" in workflow:
    fail("Pages latest channel must unset TAMAPOKE_VERSION instead of exporting an empty value")

if not VERSION_TOOL.is_file():
    fail("tools/firmware_version.py is missing")

clean_env = os.environ.copy()
clean_env.pop("TAMAPOKE_VERSION", None)
local = subprocess.check_output(
    ["python3", str(VERSION_TOOL)], cwd=ROOT, env=clean_env, text=True
).strip()
head = subprocess.check_output(
    ["git", "rev-parse", "--short=7", "HEAD"], cwd=ROOT, text=True
).strip()
if not re.fullmatch(rf"{re.escape(head)}-\d{{8}}T\d{{6}}Z", local):
    fail(f"local version does not contain HEAD and UTC build time: {local!r}")

release_env = clean_env | {"TAMAPOKE_VERSION": "release/v9.8.7"}
release = subprocess.check_output(
    ["python3", str(VERSION_TOOL)], cwd=ROOT, env=release_env, text=True
).strip()
define = subprocess.check_output(
    ["python3", str(VERSION_TOOL), "--cpp-define"],
    cwd=ROOT,
    env=release_env,
    text=True,
).strip()
if release != "release/v9.8.7":
    fail("an explicit release version must be preserved exactly")
if define != '-DFW_VERSION="release/v9.8.7"':
    fail(f"C++ release-version define is malformed: {define!r}")

build_web = (ROOT / "tools/build_web.sh").read_text(encoding="utf-8")
for fragment in ('if [ -z "${TAMAPOKE_VERSION:-}" ]', "unset TAMAPOKE_VERSION",
                 'TAMAPOKE_VERSION="$(python3 tools/firmware_version.py)"'):
    if fragment not in build_web:
        fail(f"Web build version fallback is missing {fragment!r}")
version_setup = build_web.split('FW_DEFINE="', 1)[0]
version_setup += '\nprintf "%s" "$TAMAPOKE_VERSION"\n'
empty = subprocess.check_output(
    ["bash", "-c", version_setup, "tools/build_web.sh"], cwd=ROOT,
    env=clean_env | {"TAMAPOKE_VERSION": ""}, text=True,
).strip()
if not re.fullmatch(rf"{re.escape(head)}-\d{{8}}T\d{{6}}Z", empty):
    fail(f"Web build does not recover from an empty version override: {empty!r}")
preserved = subprocess.check_output(
    ["bash", "-c", version_setup, "tools/build_web.sh"], cwd=ROOT,
    env=release_env, text=True,
).strip()
if preserved != "release/v9.8.7":
    fail(f"Web build does not preserve the release version: {preserved!r}")

firmware = (ROOT / "TamaPoke.ino").read_text(encoding="utf-8")
for fragment in ('#ifndef FW_VERSION', 'Serial.printf("TamaPoke fw %s\\n"',
                 'snprintf(ver, sizeof(ver), "TamaPoke %s"'):
    if fragment not in firmware:
        fail(f"firmware version display is missing {fragment!r}")

for relative in ("tools/build_web.sh", "tools/flash.sh", "tools/emu/build.sh",
                 "tools/emu/tests/run.sh"):
    source = (ROOT / relative).read_text(encoding="utf-8")
    if "firmware_version.py" not in source or "FW_DEFINE" not in source:
        fail(f"{relative} does not inject the shared firmware version")

print(f"PASS firmware versions: local={local} release={release}")
