#pragma once

#include <stdint.h>
#include <stddef.h>

bool hkdf_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *salt,
    size_t salt_len,
    const uint8_t *info,
    size_t info_len,
    uint8_t *output,
    size_t output_len);