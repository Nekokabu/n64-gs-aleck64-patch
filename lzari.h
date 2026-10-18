/*
 * LZARI (LZSS + adaptive arithmetic coding) as used by the N64 GameShark
 * firmware to pack shell.bin and the other files in its ROM.
 *
 * This is a reimplementation of Haruhiko Okumura's LZARI (1989, public
 * domain) with the same parameters (N = 4096, F = 60, THRESHOLD = 2, M = 15)
 * and the same stream layout: a 4-byte little-endian uncompressed size,
 * followed by the arithmetic-coded data.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef LZARI_H
#define LZARI_H

#include <stddef.h>
#include <stdint.h>

/*
 * Decode a stream. Returns the number of output bytes written, or
 * (size_t)-1 if the stream is truncated or the output buffer is too small.
 * *consumed receives the number of input bytes used (may be NULL).
 */
size_t lzari_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                    size_t *consumed);

/*
 * Encode a buffer. Returns the number of output bytes written, or (size_t)-1
 * if the output buffer is too small. out_cap should be at least
 * in_len * 9 / 8 + 16.
 */
size_t lzari_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap);

#endif
