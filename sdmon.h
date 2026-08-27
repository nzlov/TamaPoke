#pragma once
#include <Arduino.h>
#include "items.h"

// acciones de los sprites PMD (formato TPK2)
enum : uint8_t {
  PMD_IDLE = 0, PMD_WALKL, PMD_WALKR, PMD_SLEEP, PMD_EAT, PMD_HURT,
  PMD_ATTACK, PMD_POSE, PMD_HOP, PMD_NOD, PMD_BREATH, PMD_SIT,
  PMD_NACTS
};
constexpr uint8_t PMD_STORAGE_ACTS = PMD_NACTS * 2;
constexpr uint8_t pmdFacingAction(uint8_t action, bool back) {
  return back ? (uint8_t)(action + PMD_NACTS) : action;
}

struct PmdAct {
  uint8_t w = 0, h = 0, frames = 0;
  uint8_t base = 0;  // fila+1 del pixel mas bajo (anclar por los pies, no el lienzo)
  uint16_t ms[24];
  const uint8_t *data = nullptr;  // frames * w * h en el blob
};

// sprite PMD multi-accion cargado de la SD a PSRAM
struct PmdMon {
  bool loaded = false;
  // WHICH species is actually in here. It exists so a test can prove the file
  // that got opened matches the dex that was asked for: dexNum was a uint8_t,
  // so every species past 255 wrapped -- 258 MARSHTOMP loaded p002.bin and a
  // Hoenn creature appeared on screen as IVYSAUR.
  int16_t dex = 0;
  uint16_t palCount = 0;
  uint16_t pal[256];
  uint8_t *blob = nullptr;
  uint8_t displayScale = 0;  // precomputed from the pack's opaque Idle bounds
  PmdAct acts[PMD_STORAGE_ACTS];

  bool load(int16_t dexNum, bool shiny = false, uint8_t gender = 0,
            bool mega = false, MegaFormKind megaForm = MEGA_FORM_NONE);
  void unload();
  bool has(uint8_t a) const { return loaded && a < PMD_STORAGE_ACTS && acts[a].frames > 0; }
};

inline uint8_t pmdDisplayScale(const PmdMon &mon, const PmdAct &action,
                               uint8_t maxScale, uint8_t scaleBonus) {
  if (!action.frames || !mon.displayScale || !maxScale) return 0;
  uint16_t scale = (uint16_t)mon.displayScale + scaleBonus;
  if (scale > maxScale) scale = maxScale;
  while (scale > 2 && (uint16_t)action.h * scale > 250) scale--;
  return (uint8_t)scale;
}

// miniaturas de la galeria (thumbs.bin entero en PSRAM)
struct SdThumbs {
  bool loaded = false;
  uint8_t *data = nullptr;
  uint16_t count = 0;
  bool load();
  const uint8_t *get(int16_t dex) const;  // blob: w,h,palCount,pal[],idx[]
};
extern SdThumbs thumbs;

bool sdBegin();                 // monta la SD (SDMMC 1-bit), true si hay tarjeta
void sdScanRegionArt();         // narrows gRegionArt to the packs actually present
bool sdSerialCommand(const String &line);  // PUT/GET/LS/RM/FORMAT por USB; true si la maneja
void sdSerialPackInfo();        // PACK records + DONE for the INFO serial command
extern bool sdReady;
extern bool sdDirty;  // true tras recibir archivos: recargar sprite
