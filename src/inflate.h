/* inflate.h - unpacks a zlib stream (RFC 1950/1951). By Mhdshehri. */
#pragma once
#include "mini.h"
/* Returns the number of bytes written to out, or -1 (bad data, or more than cap). */
int zlib_inflate(const uint8_t *in, int n, uint8_t *out, int cap);
/* Wraps data as a zlib stream of stored blocks (no packing). Returns the length, or -1 if cap is too small. */
int zlib_store(const uint8_t *in, int n, uint8_t *out, int cap);
