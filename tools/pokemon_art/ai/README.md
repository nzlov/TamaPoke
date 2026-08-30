# AI turnaround sources

These 128×128 RGBA PNGs were created with OpenAI's built-in image-generation
tool for TamaPoke. Each transparent 2×2 sheet contains front, left, back and
right views. They independently fill catalogue gaps that do not have a local
PMD SpriteCollab source:

- `base-NNNN.png`: 48 base species;
- `mega-NNNN-form.png`: 57 Mega forms;
- `gmax-NNNN.png`: 32 Gigantamax forms.

The shared prompt requested a clean four-view pixel-art turnaround, consistent
proportions and markings, no text, no shadows, and a fully transparent
background. Three generated sheets required deterministic black-background
chroma-key cleanup after generation; their character pixels remain generated
artwork.

`python3 tools/pack_ai_art.py` converts every sheet offline into deterministic
TPK2 animations. It never contacts an image service. The generated `.bin`
files remain ignored intermediates under `tools/sdcard/mons/`.

Pokémon names, characters and designs remain © Nintendo / Game Freak / The
Pokémon Company. This repository makes no licence claim over those designs.
