#pragma once

#include <stddef.h>
#include <stdint.h>

bool artInflate(const uint8_t *compressed, size_t compressedSize,
                uint8_t *output, size_t outputSize);
