#!/usr/bin/env python3
"""Check that Pages staging includes every web asset and generated directory."""

import importlib.util
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
PREPARER = ROOT / "tools" / "prepare_pages_site.py"
WORKFLOW = ROOT / ".github" / "workflows" / "pages.yml"

if not PREPARER.is_file():
    raise SystemExit("tools/prepare_pages_site.py is missing")

spec = importlib.util.spec_from_file_location("prepare_pages_site", PREPARER)
module = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary)
    source = root / "web"
    output = root / "site"
    (source / "firmware").mkdir(parents=True)
    (source / "packs").mkdir()
    fixtures = {
        "index.html": "<script src='serial-client.js'></script>",
        "serial-client.js": "export class SerialClient {}",
        "question-bank-model.mjs": "export const model = {};",
        "manifest.json": "{}",
        "README.md": "not a deployed asset",
        "firmware/app.bin": "firmware",
        "packs/index.json": "{}",
    }
    for relative, content in fixtures.items():
        (source / relative).write_text(content, encoding="utf-8")

    module.prepare_site(source, output, "abc1234", "2026-08-25")
    expected = {
        "index.html",
        "serial-client.js",
        "question-bank-model.mjs",
        "manifest.json",
        "build-info.json",
        "firmware/app.bin",
        "packs/index.json",
    }
    actual = {
        path.relative_to(output).as_posix()
        for path in output.rglob("*")
        if path.is_file()
    }
    if actual != expected:
        raise SystemExit(f"Pages staging mismatch: expected {expected}, got {actual}")

workflow = WORKFLOW.read_text(encoding="utf-8")
if "python3 tools/prepare_pages_site.py" not in workflow:
    raise SystemExit("Pages workflow does not use the shared site preparer")

print("PASS Pages staging includes all static files, firmware and packs")
