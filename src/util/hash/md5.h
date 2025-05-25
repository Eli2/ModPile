#pragma once

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

/* Any 32-bit or wider unsigned integer data type will do */
typedef unsigned int MD5_u32plus;

typedef struct {
	MD5_u32plus lo, hi;
	MD5_u32plus a, b, c, d;
	unsigned char buffer[64];
	MD5_u32plus block[16];
} MD5_CTX;

void MD5_Init(MD5_CTX *ctx);
void MD5_Update(MD5_CTX *ctx, const void *data, unsigned long size);
void MD5_Final(uint32_t result[4], MD5_CTX *ctx);

#if defined(__cplusplus)
}
#endif
