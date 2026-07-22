#include <assert.h>
#include <string.h>

#include "sha256.h"

static void hex(const unsigned char *digest, char *output)
{
    static const char digits[] = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        output[i * 2] = digits[digest[i] >> 4];
        output[i * 2 + 1] = digits[digest[i] & 15];
    }
    output[64] = 0;
}

int main(void)
{
    sha256_ctx_t ctx;
    unsigned char digest[32];
    char output[65];
    static const char expected[] =
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";

    sha256_init(&ctx);
    sha256_update(&ctx, (const unsigned char *)"a", 1);
    sha256_update(&ctx, (const unsigned char *)"bc", 2);
    sha256_final(&ctx, digest);
    hex(digest, output);
    assert(strcmp(output, expected) == 0);

    unsigned char data[1024];
    for (int i = 0; i < (int)sizeof(data); i++)
        data[i] = (unsigned char)i;
    sha256_init(&ctx);
    for (int i = 0; i < 1024; i += 13)
        sha256_update(&ctx, data + i, (unsigned int)((i + 13 <= 1024) ? 13 : 1024 - i));
    sha256_final(&ctx, digest);
    hex(digest, output);
    assert(strcmp(output,
        "785b0751fc2c53dc14a4ce3d800e69ef9ce1009eb327ccf458afe09c242c26c9") == 0);
    return 0;
}
