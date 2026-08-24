# FreeType embedded subset

This directory contains only the FreeType 2.14.3 source directories needed to
render the CFF/OpenType font carried by TamaPoke UI packs. It follows the
official `espressif/freetype` component at FreeType commit
`0a0221a1347e2f1e07c395263540026e9a0aa7c7`.

Arduino compiles the small wrapper translation units under
`src/tamafreetype/`; desktop builds link the system FreeType library. Optional
compression, SVG, color bitmap and unused font drivers are not built.

See `LICENSE.TXT` and `docs/FTL.TXT` for the FreeType Project License.
