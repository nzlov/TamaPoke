#include "sdmon.h"
#include "pin_config.h"
#include "pet.h"
#include "content.h"
#include <FS.h>
#include <SD_MMC.h>

bool sdReady = false;
bool sdDirty = false;
SdThumbs thumbs;

static void recoverPackUploads() {
  String partials[32], backups[32];
  uint8_t partialCount = 0, backupCount = 0;
  File dir = SD_MMC.open("/packs");
  if (!dir || !dir.isDirectory()) return;
  File entry;
  while ((entry = dir.openNextFile())) {
    if (!entry.isDirectory()) {
      String path = entry.path();
      if (path.endsWith(".part") && partialCount < 32) partials[partialCount++] = path;
      else if (path.endsWith(".bak") && backupCount < 32) backups[backupCount++] = path;
    }
    entry.close();
  }
  dir.close();
  for (uint8_t i = 0; i < partialCount; i++) SD_MMC.remove(partials[i]);
  for (uint8_t i = 0; i < backupCount; i++) {
    String finalPath = backups[i].substring(0, backups[i].length() - 4);
    if (SD_MMC.exists(finalPath)) SD_MMC.remove(backups[i]);
    else SD_MMC.rename(backups[i], finalPath);
  }
}

bool PmdMon::load(int16_t dexNum, bool shiny) {
  // int16_t, NOT uint8_t. The dex reached 386 and this did not follow, so
  // everything from 256 up wrapped into Kanto: MARSHTOMP (258) opened
  // p002.bin and drew an IVYSAUR. Evolution and trainer species IDs were hit by
  // the same width bug when the catalogue expanded.
  if (!dexValid(dexNum)) return false;
  unload();
  uint32_t size = 0;
  if (!contentLoadSprite((SpeciesId)dexNum, shiny, &blob, &size) ||
      size < 7 || size > 3UL * 1024 * 1024 || memcmp(blob, "TPK2", 4) != 0) {
    if (blob) { free(blob); blob = nullptr; }
    return false;
  }

  dex = dexNum;                  // what is actually in here, for the tests
  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *p = blob + 7 + palCount * 2;
  const uint8_t *end = blob + size;
  for (uint8_t i = 0; i < nActs && p + 4 <= end; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    // valida que ms[] y los datos del frame caben en el blob (archivo truncado)
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || p + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) {
      a.ms[k] = p[0] | (p[1] << 8);
      p += 2;
    }
    a.data = p;
    p += (uint32_t)w * h * nf;
    // fila mas baja con contenido en cualquier frame: anclar por los pies
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++)
          if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  Serial.printf("sprite #%u cargado del pack (%u KB)\n", (unsigned)dexNum, size / 1024);
  return true;
}

void PmdMon::unload() {
  if (blob) {
    free(blob);
    blob = nullptr;
  }
  for (auto &a : acts) {
    a.w = a.h = a.frames = a.base = 0;
    a.data = nullptr;
  }
  loaded = false;
}

bool SdThumbs::load() {
  uint32_t size = 0;
  if (!contentLoadThumbs(&data, &size) || size < 6 || memcmp(data, "TPTH", 4) != 0) {
    Serial.println("miniaturas ausentes o invalidas");
    if (data) { free(data); data = nullptr; }
    return false;
  }
  memcpy(&count, data + 4, 2);
  loaded = true;
  Serial.printf("miniaturas cargadas: %u (%u KB)\n", count, size / 1024);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded || dex < 1 || dex > count) return nullptr;
  uint32_t off;
  memcpy(&off, data + 6 + 4 * (dex - 1), 4);
  return data + off;
}

void sdScanRegionArt() {
  uint16_t mask = 0;
  for (uint8_t r = 0; r < regionCount(); r++) {
    if (r == regionAll()) continue;
    const RegionInfo &rg = regionInfo(r);
    bool available = regionPackAvailable(r);
    if (available) mask |= (uint16_t)(1u << r);
    Serial.printf("pack: %-8s %s\n", rg.name, available ? "si" : "NO");
  }
  gRegionArt = mask;
}

bool sdBegin() {
  SD_MMC.setPins(SDMMC_CLK, SDMMC_CMD, SDMMC_DATA);
  sdReady = SD_MMC.begin("/sdcard", true /* modo 1-bit */, true /* formatea si no monta */);
  if (sdReady) {
    Serial.printf("SD montada: %llu MB\n", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    SD_MMC.mkdir("/packs");
    recoverPackUploads();
    contentBegin();
    sdScanRegionArt();
  } else {
    Serial.println("SD no detectada (modo de recuperacion)");
  }
  return sdReady;
}

// ---------------------------------------------------------------------------
// Protocolo de carga por USB (para llenar la SD sin sacarla de la placa):
//   PUT <ruta> <bytes>\n  + datos crudos   -> "OK" ... "DONE"
//   LS\n                                   -> listado de /packs
// Usar con tools/send_sd.py
// ---------------------------------------------------------------------------

bool sdSerialCommand(const String &line) {
  if (line.startsWith("PUT ")) {
    int sp = line.lastIndexOf(' ');
    if (sp < 5) { Serial.println("ERR"); return true; }
    String path = line.substring(4, sp);
    uint32_t size = line.substring(sp + 1).toInt();
    if (!path.startsWith("/")) path = "/" + path;
    bool packPath = path.startsWith("/packs/") &&
                    (path.endsWith(".tui") || path.endsWith(".tmove") ||
                     path.endsWith(".tregion"));
    if (!sdReady || path.indexOf("..") >= 0 || !packPath ||
        size == 0 || size > 256UL * 1024 * 1024) {
      Serial.println("ERR");
      return true;
    }
    String tempPath = path + ".part";
    SD_MMC.remove(tempPath);
    File f = SD_MMC.open(tempPath, FILE_WRITE);
    if (!f) {
      Serial.println("ERR");
      return true;
    }
    Serial.println("OK");
    static uint8_t buf[2048];
    uint32_t remaining = size;
    Serial.setTimeout(5000);
    while (remaining > 0) {
      size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      size_t n = Serial.readBytes(buf, want);
      if (n == 0) break;  // timeout
      if (f.write(buf, n) != n) break;
      remaining -= n;
      Serial.println("#");  // ack: listo para el siguiente bloque
    }
    f.close();
    Serial.setTimeout(1000);
    bool valid = remaining == 0 && contentValidatePackFile(tempPath.c_str());
    if (valid) {
      String backupPath = path + ".bak";
      SD_MMC.remove(backupPath);
      bool hadOld = SD_MMC.exists(path);
      if (hadOld && !SD_MMC.rename(path, backupPath)) valid = false;
      if (valid && !SD_MMC.rename(tempPath, path)) {
        if (hadOld) SD_MMC.rename(backupPath, path);
        valid = false;
      }
      if (valid) SD_MMC.remove(backupPath);
    }
    if (!valid) SD_MMC.remove(tempPath);
    sdDirty = valid;
    Serial.println(valid ? "DONE" : "ERR");
    return true;
  } else if (line == "LS") {
    File dir = SD_MMC.open("/packs");
    if (dir) {
      File e;
      while ((e = dir.openNextFile())) {
        Serial.printf("%s %u\n", e.name(), (uint32_t)e.size());
        e.close();
      }
      dir.close();
    }
    Serial.println("DONE");
    return true;
  }
  return false;
}
