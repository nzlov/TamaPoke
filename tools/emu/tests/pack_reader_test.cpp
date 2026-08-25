#include "Arduino.h"
#include "Preferences.h"
#include "content.h"
#include <cstdio>
#include <string>
#include <vector>

uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}

extern "C" size_t __real_fread(void *, size_t, size_t, FILE *);
extern "C" size_t __wrap_fread(void *buffer, size_t size, size_t count, FILE *stream) {
  if (count > 7) count = 7;
  return __real_fread(buffer, size, count, stream);
}

static int seekCount = 0;
extern "C" int __real_fseek(FILE *, long, int);
extern "C" int __wrap_fseek(FILE *stream, long offset, int origin) {
  seekCount++;
  return __real_fseek(stream, offset, origin);
}

static bool seekSource(void *context, uint32_t offset) {
  return __real_fseek(static_cast<FILE *>(context), (long)offset, SEEK_SET) == 0;
}

static size_t readSource(void *context, uint8_t *out, size_t length) {
  return __real_fread(out, 1, length, static_cast<FILE *>(context));
}

int main() {
  bool ok = contentValidatePackFile(PACK_READER_FIXTURE) == CONTENT_PACK_VALID;
  bool sequential = seekCount == 5;
  FILE *source = fopen(PACK_READER_FIXTURE, "rb");
  __real_fseek(source, 0, SEEK_END);
  long size = ftell(source);
  __real_fseek(source, 0, SEEK_SET);
  std::vector<uint8_t> raw((size_t)size);
  bool loaded = size > 64 && __real_fread(raw.data(), 1, raw.size(), source) == raw.size();
  fclose(source);
  FILE *uploaded = tmpfile();
  bool uploadedWritten = uploaded && loaded &&
      fwrite(raw.data(), 1, raw.size(), uploaded) == raw.size() && fflush(uploaded) == 0;
  ContentPackSource uploadedSource{
      uploaded, (uint32_t)raw.size(), seekSource, readSource
  };
  bool sameHandle = uploadedWritten &&
      contentValidatePackSource(uploadedSource) == CONTENT_PACK_VALID;
  if (uploaded) fclose(uploaded);
  std::string badPath = std::string(PACK_READER_FIXTURE) + ".bad";
  auto mutationIs = [&](size_t offset, ContentPackValidation expected) {
    uint8_t saved = raw[offset];
    raw[offset] ^= 1;
    FILE *bad = fopen(badPath.c_str(), "wb");
    bool written = bad && fwrite(raw.data(), 1, raw.size(), bad) == raw.size();
    if (bad) fclose(bad);
    ContentPackValidation actual = written ? contentValidatePackFile(badPath.c_str())
                                               : CONTENT_PACK_OPEN_FAILED;
    raw[offset] = saved;
    return written && actual == expected;
  };
  bool precise = loaded &&
      mutationIs(0, CONTENT_PACK_HEADER_INVALID) &&
      mutationIs(4, CONTENT_PACK_ABI_MISMATCH) &&
      mutationIs(8, CONTENT_PACK_SIZE_MISMATCH) &&
      mutationIs(52, CONTENT_PACK_DIRECTORY_INVALID) &&
      mutationIs(raw.size() - 1, CONTENT_PACK_CHECKSUM_MISMATCH);
  remove(badPath.c_str());
  printf("%s  pack validation tolerates short filesystem reads\n", ok ? "PASS" : "FAIL");
  printf("%s  payload CRC uses one sequential filesystem scan\n",
         sequential ? "PASS" : "FAIL");
  printf("%s  uploaded pack validates through its existing open handle\n",
         sameHandle ? "PASS" : "FAIL");
  printf("%s  pack validation reports the failing format stage\n",
         precise ? "PASS" : "FAIL");
  return ok && sequential && sameHandle && precise ? 0 : 1;
}
