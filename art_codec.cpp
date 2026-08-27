#include "art_codec.h"

#if defined(ESP32)
#include <miniz.h>
#else
#include <zlib.h>
#endif

bool artInflate(const uint8_t *compressed, size_t compressedSize,
                uint8_t *output, size_t outputSize) {
  if (!compressed || !compressedSize || !output || !outputSize) return false;
#if defined(ESP32)
  size_t written = tinfl_decompress_mem_to_mem(
      output, outputSize, compressed, compressedSize,
      TINFL_FLAG_PARSE_ZLIB_HEADER);
  return written == outputSize;
#else
  uLongf written = outputSize;
  return uncompress(output, &written, compressed, compressedSize) == Z_OK &&
         written == outputSize;
#endif
}
