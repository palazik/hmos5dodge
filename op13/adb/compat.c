// SPDX-License-Identifier: Apache-2.0
// Portable AOSP adbd helpers using the donor's public OpenSSL 3 ABI.
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <crypto_utils/android_pubkey.h>
#include <cutils/properties.h>

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

bool android_pubkey_decode(const uint8_t *buffer, size_t size, RSA **key)
{
    if (!buffer || !key || size != ANDROID_PUBKEY_ENCODED_SIZE || le32(buffer) != 64)
        return false;
    uint32_t exponent = le32(buffer + 520);
    if (exponent != 65537 && exponent != 3) return false;
    RSA *rsa = RSA_new();
    BIGNUM *n = BN_lebin2bn(buffer + 8, 256, NULL), *e = BN_new();
    if (!rsa || !n || !e || !BN_set_word(e, exponent) || !RSA_set0_key(rsa, n, e, NULL)) {
        BN_free(n); BN_free(e); RSA_free(rsa);
        return false;
    }
    if (RSA_bits(rsa) != 2048) { RSA_free(rsa); return false; }
    *key = rsa;
    return true;
}

int __b64_pton(const char *text, unsigned char *output, size_t capacity)
{
    size_t length = strlen(text);
    if (!length || length % 4 || length > 4096) return -1;
    size_t padding = (text[length - 1] == '=') + (length > 1 && text[length - 2] == '=');
    for (size_t i = 0; i < length - padding; ++i)
        if (!(isalnum((unsigned char)text[i]) || text[i] == '+' || text[i] == '/')) return -1;
    size_t bytes = length / 4 * 3;
    if (bytes - padding > capacity) return -1;
    // EVP writes the padded length, which can exceed the caller's capacity.
    unsigned char temporary[3072];
    int decoded = EVP_DecodeBlock(temporary, (const unsigned char *)text, (int)length);
    if (decoded < 0 || (size_t)decoded != bytes) return -1;
    memcpy(output, temporary, bytes - padding);
    return (int)(bytes - padding);
}

int property_get(const char *name, char *value, const char *fallback)
{
    const char *result = fallback ? fallback : "";
    if (!strcmp(name, "ro.adb.secure")) result = "1";
    else if (!strcmp(name, "ro.product.name")) result = "hmos5dodge";
    else if (!strcmp(name, "ro.product.model")) result = "OnePlus 13 HMOS5 diagnostics";
    else if (!strcmp(name, "ro.product.device")) result = "dodge";
    // Explicit loopback fixture only. Production launcher clears the environment.
    else if (!strcmp(name, "service.adb.tcp.port")) {
        const char *port = getenv("HMOS_ADB_TEST_PORT");
        if (port && *port && strspn(port, "0123456789") == strlen(port) &&
            strtol(port, NULL, 10) >= 1024 && strtol(port, NULL, 10) <= 65535) result = port;
    }
    size_t length = strnlen(result, PROPERTY_VALUE_MAX - 1);
    memcpy(value, result, length);
    value[length] = 0;
    return (int)length;
}

int8_t property_get_bool(const char *name, int8_t fallback)
{
    char value[PROPERTY_VALUE_MAX];
    if (!property_get(name, value, "")) return fallback;
    return !strcmp(value, "1") || !strcmp(value, "true");
}

int property_set(const char *name, const char *value)
{
    (void)name; (void)value;
    errno = ENOTSUP;
    return -1;
}
