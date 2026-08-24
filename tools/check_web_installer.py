#!/usr/bin/env python3
"""Check that the Web Serial installer is driven by the generated catalogue."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
index = json.loads((ROOT / "web" / "packs" / "index.json").read_text(encoding="utf-8"))
html = (ROOT / "web" / "index.html").read_text(encoding="utf-8")
sdmon = (ROOT / "sdmon.cpp").read_text(encoding="utf-8")
content = (ROOT / "content.cpp").read_text(encoding="utf-8")
firmware = (ROOT / "TamaPoke.ino").read_text(encoding="utf-8")
pages_workflow = (ROOT / ".github" / "workflows" / "pages.yml").read_text(encoding="utf-8")
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
    ".tui,.tmove,.tregion", "Deploy selected packs", "SD card management",
    "runCommand('LS')", "`RM packs/${name}`", "runCommand('FORMAT'", "confirm(",
    "progress-percent", "deploy-error", "BOARD_ERRORS", "isBoardError",
    "reportDeploymentError", "transferred === total ? total * .999",
    "confirmDependencyOverride", "installedPackIds", "ignore dependencies",
    "web-firmware-version", "target-firmware-version", "web revision",
    "runCommand('INFO'", "versions match", "versions differ",
    "format-overlay", ".loading-overlay[hidden]", "Formatting microSD…",
    "formatOverlay.hidden = false",
    "formatOverlay.hidden = true", "document.body.setAttribute('aria-busy', 'true')",
    "document.body.removeAttribute('aria-busy')",
    'id="build-info"', "build-info.json", "info.commit.slice(0, 7)", "info.date",
]
for fragment in required_fragments:
    if fragment not in html:
        raise SystemExit(f"installer is missing {fragment!r}")

format_handler = html.split("formatButton.onclick = async () => {", 1)[1].split("\n};", 1)[0]
format_finally = format_handler.split("} finally {", 1)[1]
if "formatOverlay.hidden = false" not in format_handler.split("try {", 1)[0]:
    raise SystemExit("format overlay must be shown before formatting starts")
for cleanup in ("formatOverlay.hidden = true", "document.body.removeAttribute('aria-busy')"):
    if cleanup not in format_finally:
        raise SystemExit(f"format cleanup must always run: {cleanup}")

catalog_renderer = html.split("function renderCatalog(index) {", 1)[1].split(
    "async function loadCatalog()", 1)[0]
if "web revision" in catalog_renderer:
    raise SystemExit("pending deployment catalogue must not show redundant pack revisions")
installed_renderer = html.split("async function refreshInstalledPacks() {", 1)[1].split(
    "async function deleteInstalledPack", 1)[0]
if "target revision" not in installed_renderer or "web revision" not in installed_renderer:
    raise SystemExit("installed packs must retain target and web revision comparison")

for fragment in ('BUILD_COMMIT: ${{ github.sha }}', '--format=%cs',
                 '_site/build-info.json', '"commit": sys.argv[1]', '"date": sys.argv[2]'):
    if fragment not in pages_workflow:
        raise SystemExit(f"Pages build metadata is missing {fragment!r}")

for obsolete in ("data-region=", "parsePak(", "sprites-${region}", "mons/"):
    if obsolete in html:
        raise SystemExit(f"installer still contains obsolete sprite deployment: {obsolete}")

for command in ('line == "LS"', 'line.startsWith("RM ")', 'line == "FORMAT"'):
    if command not in sdmon:
        raise SystemExit(f"firmware is missing SD management command {command!r}")
if 'isPackPath(path)' not in sdmon or 'emptyDirectory("/", false)' not in sdmon:
    raise SystemExit("firmware SD management must restrict deletion and erase all card contents")
if 'Serial.printf("PACK\\t%s\\t%lu\\t%u\\t%s' not in sdmon:
    raise SystemExit("firmware must report installed package ids and revisions")
if 'contentReadPackInfo' not in content or 'line == "INFO"' not in firmware or 'FW\\t%s' not in firmware:
    raise SystemExit("firmware must expose package and firmware versions through INFO")
for reason in ("SD_NOT_READY", "WRITE_FAILED", "READ_TIMEOUT", "PACK_VALIDATION_FAILED",
               "REPLACE_FAILED", "DELETE_FAILED", "FORMAT_ERASE_FAILED"):
    if f'"{reason}"' not in sdmon or reason not in html:
        raise SystemExit(f"installer must expose detailed board error {reason}")

print(f"PASS catalogue-driven installer: {len(packages)} packages, "
      f"{sum(package['kind'] == 'ui' for package in packages)} UI languages")
