#!/usr/bin/env python3
"""Check that the Web Serial installer is driven by the generated catalogue."""

import hashlib
import json
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
index = json.loads((ROOT / "web" / "packs" / "index.json").read_text(encoding="utf-8"))
manifest = json.loads((ROOT / "web" / "manifest.json").read_text(encoding="utf-8"))
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
    "format-overlay", ".loading-overlay[hidden]", "Formatting microSD as FAT32…",
    "formatOverlay.hidden = false",
    "formatOverlay.hidden = true", "document.body.setAttribute('aria-busy', 'true')",
    "document.body.removeAttribute('aria-busy')",
    'id="build-info"', "build-info.json", "info.commit.slice(0, 7)", "info.date",
    "pumpSerial(reader)", "serialWaiters", "Refreshing installed packs…",
    "Could not refresh installed packs.", "CRC32_TABLE", "crc32Hex(data)",
    "failed the catalogue download checksum", "?v=${expectedCrc}",
    "`manifest.json?v=${Date.now()}`",
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

for build in manifest.get("builds", []):
    for part in build.get("parts", []):
        path, marker = part["path"].split("?v=", 1)
        asset = ROOT / "web" / path
        if not asset.is_file() or hashlib.sha256(asset.read_bytes()).hexdigest() != marker:
            raise SystemExit(f"firmware cache marker does not match {path!r}")

for fragment in ('BUILD_COMMIT: ${{ github.sha }}', '--format=%cs',
                 '_site/build-info.json', '"commit": sys.argv[1]', '"date": sys.argv[2]'):
    if fragment not in pages_workflow:
        raise SystemExit(f"Pages build metadata is missing {fragment!r}")

for obsolete in ("data-region=", "parsePak(", "sprites-${region}", "mons/"):
    if obsolete in html:
        raise SystemExit(f"installer still contains obsolete sprite deployment: {obsolete}")

read_line = html.split("async function readLine", 1)[1].split("async function waitFor", 1)[0]
if "reader.read()" in read_line or "setTimeout" not in read_line:
    raise SystemExit("serial line timeouts must not block directly on reader.read()")

for command in ('line == "LS"', 'line.startsWith("RM ")', 'line == "FORMAT"'):
    if command not in sdmon:
        raise SystemExit(f"firmware is missing SD management command {command!r}")
if 'isPackPath(path)' not in sdmon:
    raise SystemExit("firmware SD management must restrict pack deletion paths")
format_fat32 = sdmon.split('static const char *formatFat32()', 1)[1].split(
    'void sdSerialPackInfo()', 1)[0]
for fragment in ('f_getfree(', 'f_mount(nullptr, drive, 0)', 'options.fmt = FM_FAT32',
                 'options.au_size = 4096', 'f_mkfs(', 'f_mount(filesystem, drive, 1)',
                 'filesystem->fs_type != FS_FAT32'):
    if fragment not in format_fat32:
        raise SystemExit(f"firmware FAT32 format path is missing {fragment!r}")
format_order = ('f_mount(nullptr, drive, 0)', 'f_mkfs(', 'f_mount(filesystem, drive, 1)')
if [format_fat32.index(fragment) for fragment in format_order] != sorted(
        format_fat32.index(fragment) for fragment in format_order):
    raise SystemExit("firmware must unmount, format FAT32, and remount in order")
if 'emptyDirectory(' in sdmon:
    raise SystemExit("FORMAT must rebuild FAT32 instead of only deleting files")
if sdmon.count('SD_MMC.begin(') != 1:
    raise SystemExit("firmware must have exactly one audited microSD mount path")
mount = sdmon.split('SD_MMC.begin("/sdcard"', 1)[1].split(';', 1)[0]
if 'false /* nunca formatea implicitamente */' not in mount:
    raise SystemExit("firmware must never format the microSD implicitly after a mount failure")
if 'SDMMC_FREQ_DEFAULT' in mount:
    raise SystemExit("firmware must not force the reduced SD_MMC clock")
if sdmon.count('serialPackPath(') != 3 or 'String("/packs/") + entry.name()' not in sdmon:
    raise SystemExit("firmware must normalize INFO and LS entries to protocol pack paths")
upload_handler = sdmon.split('if (line.startsWith("PUT ")) {', 1)[1].split(
    '} else if (line == "LS")', 1)[0]
if 'f.flush();' not in upload_handler or 'f.close();' not in upload_handler:
    raise SystemExit("firmware must sync and close an uploaded pack before validation")
for fragment in ('SD_MMC.open(tempPath, "w+")',
                 'contentValidatePackFile(tempPath.c_str())'):
    if fragment not in upload_handler:
        raise SystemExit("firmware must validate the persisted upload by path")
upload_order = ('f.flush();', 'f.close();', 'contentValidatePackFile(tempPath.c_str())')
if [upload_handler.index(fragment) for fragment in upload_order] != sorted(
        upload_handler.index(fragment) for fragment in upload_order):
    raise SystemExit("firmware must close and validate the persisted upload in order")
if 'ContentPackSource' in upload_handler or 'attempt < 4' in upload_handler:
    raise SystemExit("firmware must not retain same-handle adapters or reopen retries")
pack_info = sdmon.split('void sdSerialPackInfo()', 1)[1].split(
    'bool sdSerialCommand', 1)[0]
for fragment in ('contentReadPackInfo(path.c_str(), info)',
                 'Serial.printf("FILE\\t%u\\t%s'):
    if fragment not in pack_info:
        raise SystemExit("INFO must inspect pack paths and retain unreadable files")
if 'const physical = line.match(/^FILE\\t(\\d+)\\t(.+)$/)' not in html:
    raise SystemExit("installer must display physical pack records without readable metadata")
if "waitFor('DONE', 120000" not in html:
    raise SystemExit("installer must allow enough time to sync and validate large packs")
if 'Serial.printf("PACK\\t%s\\t%lu\\t%u\\t%s' not in sdmon:
    raise SystemExit("firmware must report installed package ids and revisions")
if 'contentReadPackInfo' not in content or 'line == "INFO"' not in firmware or 'FW\\t%s' not in firmware:
    raise SystemExit("firmware must expose package and firmware versions through INFO")
for reason in ("SD_NOT_READY", "WRITE_FAILED", "READ_TIMEOUT", "PACK_VALIDATION_FAILED",
               "PACK_OPEN_FAILED", "PACK_READ_FAILED", "PACK_HEADER_INVALID",
               "PACK_ABI_MISMATCH", "PACK_SIZE_MISMATCH", "PACK_CHECKSUM_MISMATCH",
               "PACK_DIRECTORY_INVALID", "REPLACE_FAILED", "DELETE_FAILED",
               "FORMAT_VOLUME_NOT_FOUND", "FORMAT_UNMOUNT_FAILED", "FORMAT_FAILED",
               "FORMAT_REMOUNT_FAILED", "FORMAT_TYPE_MISMATCH", "FORMAT_INIT_FAILED"):
    if f'"{reason}"' not in sdmon or reason not in html:
        raise SystemExit(f"installer must expose detailed board error {reason}")

print(f"PASS catalogue-driven installer: {len(packages)} packages, "
      f"{sum(package['kind'] == 'ui' for package in packages)} UI languages")
