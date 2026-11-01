/* Host stand-in (test only): signing is not what this test checks, so HMAC is
 * absent and a signature fails. */
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int mbedtls_md_type_t;
typedef struct { int unused; } mbedtls_md_info_t;
typedef struct { int unused; } mbedtls_md_context_t;
#define MBEDTLS_MD_SHA256 6
static inline const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t t) { (void)t; return 0; }
static inline void mbedtls_md_init(mbedtls_md_context_t *c) { (void)c; }
static inline int mbedtls_md_setup(mbedtls_md_context_t *c, const mbedtls_md_info_t *i, int h) { (void)c; (void)i; (void)h; return -1; }
static inline int mbedtls_md_hmac_starts(mbedtls_md_context_t *c, const uint8_t *k, size_t n) { (void)c; (void)k; (void)n; return -1; }
static inline int mbedtls_md_hmac_update(mbedtls_md_context_t *c, const uint8_t *d, size_t n) { (void)c; (void)d; (void)n; return -1; }
static inline int mbedtls_md_hmac_finish(mbedtls_md_context_t *c, uint8_t *o) { (void)c; (void)o; return -1; }
static inline void mbedtls_md_free(mbedtls_md_context_t *c) { (void)c; }
