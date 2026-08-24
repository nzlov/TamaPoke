# TamaPoke web installer

The page flashes the firmware with ESP Web Tools and deploys runtime data packs
to the microSD with Web Serial. Chrome or Edge is required; serve the directory
over HTTPS or `http://localhost`.

## Runtime packages

`packs/index.json` is the install catalogue. The page builds its language and
region choices from this file, resolves `requires` dependencies, and never keeps
a hardcoded region or language list.

- `.tui` — language strings, layout metrics and its bitmap/OpenType font payload.
- `.tmove` — moves, localized names/descriptions, learnsets/TMs and type chart.
- `.tregion` — species, localized names/descriptions, sprites, region metadata, trainers,
  regional battle data and badges.

The firmware validates the common ABI and payload CRC before accepting an
upload. Data is written to a `.part` file first; a valid replacement is renamed
into `/packs` only after the complete transfer. Restart after deployment so the
boot catalogue can be rebuilt.

## Contents

- `index.html` — firmware install plus catalogue-driven data-pack deployment.
- `manifest.json` — ESP Web Tools firmware manifest.
- `firmware/` — bootloader, partition table, app and merged blank-board image.
- `packs/index.json` — generated package catalogue.
- `packs/*.tui`, `packs/*.tmove`, `packs/*.tregion` — generated deployable data.

Regional packs are generated directly from the per-species TPK2/TPTH sources;
there is no regional intermediate bundle.

## Regenerate and validate

```bash
python3 tools/pack_pmd.py
python3 tools/make_thumbs.py
python3 tools/gen_data_packs.py
python3 tools/check_data_packs.py
python3 tools/check_web_installer.py
bash tools/build_web.sh
```

Each generated pack remains below GitHub's 100 MB per-file limit. The installer
fetches them same-origin from `web/packs/`, which also works on GitHub Pages.

## Test locally

```bash
cd web
python3 -m http.server 8000
# open http://localhost:8000 in Chrome or Edge
```

End-user flow:

1. Install or update the firmware. This runtime-pack migration intentionally
   resets saves from older firmware; later same-schema updates preserve the save
   when “Erase device” stays unchecked.
2. Choose UI languages and regions; move and regional dependencies are selected
   automatically.
3. Connect the running board and deploy the selected packs.
4. Restart the board.

Custom `.tui`, `.tmove` and `.tregion` files can be selected manually. Other
paths and extensions are rejected by the firmware.

## GitHub Pages

Serve the repository's `/web` directory from Pages. Pages supplies HTTPS and
same-origin access to the pack files. Web Serial is unavailable in Firefox and
Safari.

All PMD sprites are from PMD SpriteCollab, CC BY-NC; see
[`../CREDITS.md`](../CREDITS.md).
