#!/usr/bin/env python3
"""Check that the Web Serial installer is driven by the generated catalogue."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
index = json.loads((ROOT / "web" / "packs" / "index.json").read_text(encoding="utf-8"))
html = (ROOT / "web" / "index.html").read_text(encoding="utf-8")
packages = index.get("packages", [])
ids = {package["id"] for package in packages}

if not packages or {package["kind"] for package in packages} != {"ui", "move", "region"}:
    raise SystemExit("catalogue must expose UI, move and region packs")
if len(ids) != len(packages):
    raise SystemExit("duplicate package id")
if sum(bool(package.get("default")) for package in packages if package["kind"] == "ui") != 1:
    raise SystemExit("exactly one UI pack must be the default")
if len({locale for package in packages if package["kind"] == "ui"
        for locale in package.get("locales", [])}) != sum(
            len(package.get("locales", [])) for package in packages if package["kind"] == "ui"):
    raise SystemExit("UI locale appears in more than one pack")

visiting, visited = set(), set()
def visit(package_id: str) -> None:
    if package_id in visited:
        return
    if package_id in visiting:
        raise SystemExit(f"dependency cycle at {package_id}")
    visiting.add(package_id)
    package = next((item for item in packages if item["id"] == package_id), None)
    if not package:
        raise SystemExit(f"missing dependency {package_id}")
    for dependency in package.get("requires", []):
        visit(dependency)
    visiting.remove(package_id)
    visited.add(package_id)

for package_id in ids:
    visit(package_id)

required_fragments = [
    "packs/index.json", "data-pack-id", "pkg.requires", "PUT packs/",
    ".tui,.tmove,.tregion", "Deploy selected packs",
]
for fragment in required_fragments:
    if fragment not in html:
        raise SystemExit(f"installer is missing {fragment!r}")
for obsolete in ("data-region=", "parsePak(", "sprites-${region}", "mons/"):
    if obsolete in html:
        raise SystemExit(f"installer still contains obsolete sprite deployment: {obsolete}")

print(f"PASS catalogue-driven installer: {len(packages)} packages, "
      f"{sum(package['kind'] == 'ui' for package in packages)} UI languages")
