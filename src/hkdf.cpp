#include "hkdf.h"

#include <string.h>

#include "mbedtls/md.h"

bool hkdf_sha256(
    const uint8_t *key,
    size_t key_len,
    const uint8_t *salt,
    size_t salt_len,
    const uint8_t *info,
    size_t info_len,
    uint8_t *output,
    size_t output_len)
{
    const mbedtls_md_info_t *md =
        mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

    if (md == nullptr)
        return false;

    uint8_t prk[32];

    // Extract: PRK = HMAC-SHA256(salt, IKM)
    // In our case, the 'key' parameter is the salt (Bambu Master Key)
    // and the 'salt' parameter is the IKM (tag UID).
    if (mbedtls_md_hmac(md,
                        key,      // HMAC key (the 'salt' in HKDF terms)
                        key_len,
                        salt,     // HMAC message (the 'IKM' in HKDF terms)
                        salt_len,
                        prk) != 0)
    {
        return false;
    }

    // Expand
    uint8_t t[32];
    size_t t_len = 0;
    size_t pos = 0;
    uint8_t counter = 1;

    while (pos < output_len)
    {
        mbedtls_md_context_t ctx;
        mbedtls_md_init(&ctx);

        if (mbedtls_md_setup(&ctx, md, 1) != 0)
        {
            mbedtls_md_free(&ctx);
            return false;
        }

        mbedtls_md_hmac_starts(&ctx, prk, sizeof(prk));

        if (t_len)
            mbedtls_md_hmac_update(&ctx, t, t_len);

        if (info && info_len)
            mbedtls_md_hmac_update(&ctx, info, info_len);

        mbedtls_md_hmac_update(&ctx, &counter, 1);

        mbedtls_md_hmac_finish(&ctx, t);
        mbedtls_md_free(&ctx);

        size_t copy = output_len - pos;
        if (copy > 32) copy = 32;
        memcpy(output + pos, t, copy);

        pos += copy;
        t_len = 32;
        counter++;
    }

    return true;
}