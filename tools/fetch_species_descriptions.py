#!/usr/bin/env python3
"""Fill missing descriptions in the committed Pokemon catalogue.

Raw PokeAPI responses are cached under tools/pokeapi_cache/.  The compact,
reviewable catalogue written to tools/pokemon_data.json is committed; existing
species descriptions are authoritative and are never regenerated.
"""

from __future__ import annotations

from concurrent.futures import ThreadPoolExecutor, as_completed
import json
from html import unescape
from pathlib import Path
import re
import ssl
import time
import urllib.request


HERE = Path(__file__).resolve().parent
CACHE = HERE / "pokeapi_cache"
OUTPUT = HERE / "pokemon_data.json"
API = "https://pokeapi.co/api/v2/pokemon-species/{}/"
POKEMON_CN = "https://dex.pokemon.cn/play/pokedex/{:04d}"
LOCALE_LANGUAGES = {
    "de-DE": "de",
    "en-US": "en",
    "es-ES": "es",
    "fr-FR": "fr",
    "it-IT": "it",
    "pt-PT": "pt",
    "zh-CN": "zh-hans",
}
FALLBACK_LOCALE = "en-US"

try:
    import certifi

    SSL_CTX = ssl.create_default_context(cafile=certifi.where())
except ImportError:
    SSL_CTX = None


def normalize_text(value: str) -> str:
    """Remove game control whitespace while preserving the authored wording."""
    normalized = " ".join(value.replace("\u00ad", "").split())
    if re.search(r"[\u3400-\u9fff]", normalized):
        normalized = normalized.replace(" ", "")
    return normalized


def version_id(entry: dict) -> int:
    return int(entry["version"]["url"].rstrip("/").rsplit("/", 1)[-1])


def newest_entry(document: dict, language: str) -> tuple[str, str] | None:
    candidates = [
        entry for entry in document.get("flavor_text_entries", [])
        if entry.get("language", {}).get("name") == language
    ]
    if not candidates:
        return None
    selected = max(candidates, key=version_id)
    return normalize_text(selected["flavor_text"]), selected["version"]["name"]


def description(text: str, version: str, language: str,
                source: str = "pokeapi") -> dict[str, str]:
    return {
        "text": text,
        "version": version,
        "language": language,
        "source": source,
    }


def select_locales(document: dict, chinese_text: str | None = None) -> dict[str, dict]:
    selected = {
        locale: newest_entry(document, language)
        for locale, language in LOCALE_LANGUAGES.items()
    }
    fallback = selected[FALLBACK_LOCALE]
    if fallback is None:
        raise ValueError(f"species {document.get('id', '?')} has no English flavor text")
    result = {}
    for locale, entry in selected.items():
        language = LOCALE_LANGUAGES[locale]
        if entry is None and locale == "zh-CN" and chinese_text:
            result[locale] = description(
                chinese_text, "pokemon-cn", "zh-hans", "pokemon-cn"
            )
            continue
        text, version = entry or fallback
        result[locale] = description(
            text,
            version,
            language if entry is not None else LOCALE_LANGUAGES[FALLBACK_LOCALE],
        )
    return result


def fetch(number: int) -> dict:
    path = CACHE / f"species_{number}.json"
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    request = urllib.request.Request(
        API.format(number), headers={"User-Agent": "TamaPoke-data-pack/1.0"}
    )
    last_error = None
    for attempt in range(3):
        try:
            with urllib.request.urlopen(request, timeout=30, context=SSL_CTX) as response:
                document = json.load(response)
            CACHE.mkdir(parents=True, exist_ok=True)
            path.write_text(json.dumps(document, ensure_ascii=False), encoding="utf-8")
            return document
        except Exception as error:  # urllib exposes several transient error types
            last_error = error
            if attempt < 2:
                time.sleep(1 << attempt)
    raise RuntimeError(f"failed to fetch species {number}: {last_error}")


def fetch_chinese(number: int) -> str:
    request = urllib.request.Request(
        POKEMON_CN.format(number), headers={"User-Agent": "TamaPoke-data-pack/1.0"}
    )
    with urllib.request.urlopen(request, timeout=30, context=SSL_CTX) as response:
        page = response.read().decode("utf-8")
    match = re.search(
        r'<p class="pokemon-story__body[^>]*>\s*<span>(.*?)</span>', page, re.S
    )
    if not match:
        raise ValueError(f"species {number} has no Chinese description")
    return normalize_text(unescape(re.sub(r"<[^>]+>", "", match.group(1))))


def load_catalogue() -> dict:
    catalogue = json.loads(OUTPUT.read_text(encoding="utf-8"))
    if catalogue.get("schema") != 1 or not isinstance(catalogue.get("species"), list):
        raise ValueError("Pokemon catalogue must use schema 1")
    return catalogue


def main() -> int:
    catalogue = load_catalogue()
    species = catalogue["species"]
    count = len(species)
    missing = [
        row["id"] for row in species
        if set(row.get("descriptions", {})) != set(LOCALE_LANGUAGES)
    ]
    if not missing:
        print(f"{OUTPUT}: already contains species 1-{count}; unchanged")
        return 0

    documents: dict[int, dict] = {}
    with ThreadPoolExecutor(max_workers=8) as executor:
        futures = {executor.submit(fetch, number): number for number in missing}
        for completed, future in enumerate(as_completed(futures), 1):
            number = futures[future]
            documents[number] = future.result()
            if completed % 50 == 0 or completed == len(missing):
                print(f"  ...{completed}/{len(missing)}")

    for number in missing:
        document = documents[number]
        chinese = None
        if newest_entry(document, LOCALE_LANGUAGES["zh-CN"]) is None:
            chinese = fetch_chinese(number)
        species[number - 1]["descriptions"] = select_locales(document, chinese)
    OUTPUT.write_text(
        json.dumps(catalogue, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(f"appended {len(missing)} species to {OUTPUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
