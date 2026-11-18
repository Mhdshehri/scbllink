/* md5.h - MD5, HMAC-MD5 and RC4, as the game's network library uses them. By Mhdshehri. */
#pragma once
#include "mini.h"
void md5(const uint8_t *data, size_t n, uint8_t out[16]);
void hmac_md5(const uint8_t *key, size_t klen, const uint8_t *data, size_t n, uint8_t out[16]);
void rc4(const uint8_t *key, size_t klen, uint8_t *data, size_t n); /* in place */
