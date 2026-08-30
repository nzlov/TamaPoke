# Assets de TamaPoke — de dónde salen los sprites

Los sprites NO se editan a mano ni se guardan aquí: se **descargan de sus
fuentes y se empaquetan** con los scripts de `tools/`. Esta carpeta es
principalmente documentación del flujo. La excepción es
`fonts/NotoSansCJKsc-Medium-subset.otf`: es la entrada OpenType Noto Sans SC del paquete
chino `.tui`, no una fuente incorporada al firmware. Se regenera con
`tools/subset_ui_font.py`; `fonts/OFL-NotoSansCJK.txt` conserva su licencia.

## El flujo real

Los archivos TPK2/TPTH son entradas intermedias derivadas de PMD SpriteCollab
o de las láminas independientes de cuatro vistas en `pokemon_art/ai/`.
No se copian sueltos a la microSD: `gen_data_packs.py` los incorpora al paquete
regional `.tregion` correspondiente.

| Formato | Script | Fuente | Qué es |
|---|---|---|---|
| **TPK2** `pNNN.bin` / `psNNN.bin` / `pmNNN-form[-shiny].bin` | `pack_pmd.py` | [PMD SpriteCollab](https://github.com/PMDCollab/SpriteCollab) (CC BY-NC) | Animaciones multi-acción, incluidas las vistas traseras de batalla cuando existen — usadas en **todo**: pantalla principal, combate y **Pokédex / galería** |
| **TPK2** `pNNN.bin` / `pmNNN-form.bin` / `pgNNN.bin` | `pack_ai_art.py` | Láminas de `pokemon_art/ai/`, generadas para TamaPoke | Animaciones para todos los huecos base, Mega y Gigantamax; usa atlas de acciones cuando existen y movimiento derivado como respaldo |
| **TPTH** `thumbs.bin` | `make_thumbs.py` | (deriva de los TPK2) | Miniaturas 40×40 de la galería |
| **TIC1** `*.ticon` | `fetch_item_icons.py` | [PokeAPI/sprites](https://github.com/PokeAPI/sprites/tree/master/sprites/items) | Iconos de objetos 24×24/30×30; caché local opcional incluida en `items-core.titem` |

```bash
python3 tools/pack_pmd.py --report base-sprite-coverage.json
python3 tools/pack_pmd.py --mega --mega-report mega-sprite-coverage.json
python3 tools/pack_ai_art.py
python3 tools/make_thumbs.py  # -> tools/sdcard/mons/thumbs.bin
python3 tools/fetch_item_icons.py # -> tools/item_icon_cache/*.ticon
python3 tools/gen_data_packs.py # lee las entradas directamente y genera web/packs/*.tregion
python3 tools/send_sd.py      # envia paquetes validados a /packs por USB
```

Para actualizar el subconjunto chino después de añadir textos localizados:

```bash
python3 tools/subset_ui_font.py /ruta/NotoSansCJK-Medium.ttc \
  tools/assets/fonts/NotoSansCJKsc-Medium-subset.otf --font-number 2
```

(`s` = variante shiny. `pack_pmd.py` acepta números de Pokédex sueltos,
p. ej. `python3 tools/pack_pmd.py 7 25`.)

## Formatos binarios

Definidos en las cabeceras de cada empaquetador; el firmware los lee dentro de
las secciones del paquete regional:

- **TPK2** (`PmdMon`): `"TPK2"`, `u8 nActs`, `u16 palCount`, `u16 pal[]`, y por
  acción `u8 id,w,h,nFrames` + `u16 ms[nFrames]` + `u8 data[w*h*nFrames]`.
- **TPTH** (`SdThumbs`): `"TPTH"`, `u16 count`, `u32 offset[count]`, y por
  miniatura `u8 w,h,palCount` + `u16 pal[]` + `u8 data[w*h]`.

El firmware valida tamaños al cargar, así que un `.bin` truncado se rechaza sin
romper nada.

## Fuentes locales

`pokemon_data.json` referencia los PNG/XML de SpriteCollab fijados en
`tools/pokemon_art/pmd/`. `pack_pmd.py` los convierte sin red a los `.bin`
intermedios de `tools/sdcard/mons/`; los artefactos desplegables son los
paquetes de `web/packs/`.

`tools/pokemon_art/ai/turnarounds/` contiene 137 láminas transparentes de cuatro
vistas. Los atlas opcionales de `tools/pokemon_art/ai/actions/` son cuadrículas
4×4: cuatro acciones por archivo y cuatro fotogramas por acción. `pack_ai_art.py`
valida el catálogo completo, prefiere esos fotogramas y convierte todo sin red.

`fetch_item_icons.py` guarda los PNG y TIC1 derivados en `tools/item_icon_cache/`,
también ignorado por Git. Si no existe esa caché, el paquete de movimientos se
genera sin `IICO` y el firmware dibuja el icono de respaldo según el efecto.

> Ver [CREDITS.md](../../CREDITS.md) sobre la procedencia y los términos de los
> sprites. Son de terceros: no redistribuir con fines comerciales.
