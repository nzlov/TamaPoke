# TamaPoke web installer

The page flashes the firmware with ESP Web Tools and deploys runtime data packs
to the microSD with Web Serial. Chrome or Edge is required; serve the directory
over HTTPS or `http://localhost`.

## Runtime packages

`packs/index.json` is the install catalogue. The page builds its language and
region choices from this file and never keeps a hardcoded region or language
list. Any catalogue package can be selected independently. Before transfer, the
page checks each selected package's `requires` entries against both the target's
installed package IDs and the current selection. It names missing dependencies
and lets the user cancel or explicitly force deployment without them.

- `.tui` — language strings, layout metrics and its bitmap/OpenType font payload.
- `.tmove` — moves, localized names/descriptions, learnsets/TMs and type chart.
- `.tregion` — species, localized names/descriptions, sprites, region metadata, trainers,
  regional battle data and badges.
- `.tquiz` — locale spans, fixed-width random indexes and variable-size multiple-choice records.

`question-bank.html` imports authoring JSON, compiled `.tquiz` files or an installed
bank read from a connected device. It searches and paginates questions, hides and
automatically maintains internal IDs/revision, exports either format, and deploys
the current bank directly. The same page reads and writes independent multiple-choice
and arithmetic enable switches plus the device-wide timer, choice ratio and arithmetic
generation rules shared by care, training and battle questions. With both types disabled,
interactions run directly at 100% without opening the question modal.

The firmware validates the common ABI and payload CRC before accepting an
upload. Data is written to a `.part` file first; a valid replacement is renamed
into `/packs` only after the complete transfer. Restart after deployment so the
boot catalogue can be rebuilt. The page shows an overall byte percentage while
deploying and reports the active file and phase. Firmware errors include a stable
reason code so failures such as a missing card, write timeout, invalid pack,
failed replacement or insufficient writable storage are distinguishable.

After connecting through Web Serial, the SD card management area lists the
resource packs currently deployed in `/packs`, allows individual packs to be
deleted, and can erase all microSD contents. Delete and format operations require
confirmation; restart the board afterward before deploying or playing. Current
firmware reports each installed pack ID and revision through `INFO`, so the page
shows target and web-catalogue revisions side by side. The same response exposes
the target firmware version, which is displayed next to the version from
`manifest.json`. Older firmware automatically falls back to the size-only `LS`
listing.

## Contents

- `index.html` — firmware install plus catalogue-driven data-pack deployment.
- `question-bank.html` — question-bank editor, builder, deployment and answer-rule configuration.
- `serial-client.js` — shared line-oriented Web Serial transport.
- `manifest.json` — ESP Web Tools firmware manifest.
- `firmware/` — bootloader, partition table, app and merged blank-board image.
- `packs/index.json` — generated package catalogue.
- `packs/*.tui`, `packs/*.tmove`, `packs/*.tregion`, `packs/*.tquiz` — generated deployable data.

The files under `firmware/` and `packs/` are generated outputs and are not
tracked by Git. The GitHub Pages workflow rebuilds them from the pinned Arduino
profile and PMD SpriteCollab revision before deployment. Each deployment stages
the latest published release at the Pages root and the default branch under
`/latest/`. Release builds write the tag into both the firmware and installer
manifest; latest and local builds use the current short commit ID plus UTC build
time instead. The former `/web/` address remains a redirect to the stable root.

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
2. Choose any catalogue packs. If the target and current selection do not satisfy
   `requires`, cancel or explicitly force the deployment.
3. Connect the running board and deploy the selected packs.
4. Restart the board.

Custom `.tui`, `.tmove`, `.tregion` and `.tquiz` files can be selected manually. Other
paths and extensions are rejected by the firmware.

## GitHub Pages

The Pages workflow publishes the stable installer at `/` and the default-branch
build at `/latest/`. Pages supplies HTTPS and same-origin access to each
channel's pack files. Web Serial is unavailable in Firefox and Safari.

All PMD sprites are from PMD SpriteCollab, CC BY-NC; see
[`../CREDITS.md`](../CREDITS.md).
