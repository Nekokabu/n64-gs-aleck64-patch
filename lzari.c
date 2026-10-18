/*
 * LZARI encoder / decoder compatible with the N64 GameShark firmware.
 * See lzari.h. Based on Haruhiko Okumura's LZARI (public domain).
 *
 * SPDX-License-Identifier: MIT
 */

#include "lzari.h"

#include <string.h>

#define N          4096
#define F          60
#define THRESHOLD  2
#define NIL        N
#define M          15
#define Q1         (1UL << M)
#define Q2         (2 * Q1)
#define Q3         (3 * Q1)
#define Q4         (4 * Q1)
#define MAX_CUM    (Q1 - 1)
#define N_CHAR     (256 - THRESHOLD + F)

typedef struct {
    /* adaptive model */
    int      char_to_sym[N_CHAR];
    int      sym_to_char[N_CHAR + 1];
    unsigned sym_freq[N_CHAR + 1];
    unsigned sym_cum[N_CHAR + 1];
    unsigned position_cum[N + 1];
    /* arithmetic coder state */
    unsigned long low, high, value;
    int      shifts;
    /* bit I/O */
    const uint8_t *in;
    size_t   in_len, in_pos;
    uint8_t *out;
    size_t   out_cap, out_pos;
    int      overflow;
    unsigned buffer, mask;
    /* text window */
    uint8_t  text_buf[N + F - 1];
    /* encoder match tree */
    int      match_position, match_length;
    int      lson[N + 1], rson[N + 257], dad[N + 1];
} Lzari;

/* ---------------------------------------------------------------- model -- */

static void start_model(Lzari *z)
{
    int ch, sym, i;

    z->sym_cum[N_CHAR] = 0;
    for (sym = N_CHAR; sym >= 1; sym--) {
        ch = sym - 1;
        z->char_to_sym[ch] = sym;
        z->sym_to_char[sym] = ch;
        z->sym_freq[sym] = 1;
        z->sym_cum[sym - 1] = z->sym_cum[sym] + z->sym_freq[sym];
    }
    z->sym_freq[0] = 0;
    z->position_cum[N] = 0;
    for (i = N; i >= 1; i--)
        z->position_cum[i - 1] = z->position_cum[i] + 10000 / (unsigned)(i + 200);
}

static void update_model(Lzari *z, int sym)
{
    int i, ch_i, ch_sym;
    unsigned c;

    if (z->sym_cum[0] >= MAX_CUM) {
        c = 0;
        for (i = N_CHAR; i > 0; i--) {
            z->sym_cum[i] = c;
            c += (z->sym_freq[i] = (z->sym_freq[i] + 1) >> 1);
        }
        z->sym_cum[0] = c;
    }
    for (i = sym; z->sym_freq[i] == z->sym_freq[i - 1]; i--)
        ;
    if (i < sym) {
        ch_i = z->sym_to_char[i];
        ch_sym = z->sym_to_char[sym];
        z->sym_to_char[i] = ch_sym;
        z->sym_to_char[sym] = ch_i;
        z->char_to_sym[ch_i] = sym;
        z->char_to_sym[ch_sym] = i;
    }
    z->sym_freq[i]++;
    while (--i >= 0)
        z->sym_cum[i]++;
}

/* --------------------------------------------------------------- bit I/O -- */

static int get_bit(Lzari *z)
{
    if ((z->mask >>= 1) == 0) {
        z->buffer = z->in_pos < z->in_len ? z->in[z->in_pos] : 0;
        z->in_pos++;
        z->mask = 128;
    }
    return (z->buffer & z->mask) != 0;
}

static void put_bit(Lzari *z, int bit)
{
    if (bit)
        z->buffer |= z->mask;
    if ((z->mask >>= 1) == 0) {
        if (z->out_pos < z->out_cap)
            z->out[z->out_pos] = (uint8_t)z->buffer;
        else
            z->overflow = 1;
        z->out_pos++;
        z->buffer = 0;
        z->mask = 128;
    }
}

static void flush_bit_buffer(Lzari *z)
{
    int i;

    for (i = 0; i < 7; i++)
        put_bit(z, 0);
}

/* --------------------------------------------------------------- decoder -- */

static int binary_search_sym(const Lzari *z, unsigned x)
{
    int i = 1, j = N_CHAR, k;

    while (i < j) {
        k = (i + j) / 2;
        if (z->sym_cum[k] > x)
            i = k + 1;
        else
            j = k;
    }
    return i;
}

static int binary_search_pos(const Lzari *z, unsigned x)
{
    int i = 1, j = N, k;

    while (i < j) {
        k = (i + j) / 2;
        if (z->position_cum[k] > x)
            i = k + 1;
        else
            j = k;
    }
    return i - 1;
}

static void decode_normalize(Lzari *z)
{
    for (;;) {
        if (z->low >= Q2) {
            z->value -= Q2; z->low -= Q2; z->high -= Q2;
        } else if (z->low >= Q1 && z->high <= Q3) {
            z->value -= Q1; z->low -= Q1; z->high -= Q1;
        } else if (z->high > Q2) {
            break;
        }
        z->low += z->low;
        z->high += z->high;
        z->value = 2 * z->value + (unsigned long)get_bit(z);
    }
}

static int decode_char(Lzari *z)
{
    unsigned long range = z->high - z->low;
    int sym = binary_search_sym(z, (unsigned)(((z->value - z->low + 1) * z->sym_cum[0] - 1) / range));
    int ch;

    z->high = z->low + (range * z->sym_cum[sym - 1]) / z->sym_cum[0];
    z->low +=          (range * z->sym_cum[sym])     / z->sym_cum[0];
    decode_normalize(z);
    ch = z->sym_to_char[sym];
    update_model(z, sym);
    return ch;
}

static int decode_position(Lzari *z)
{
    unsigned long range = z->high - z->low;
    int pos = binary_search_pos(z, (unsigned)(((z->value - z->low + 1) * z->position_cum[0] - 1) / range));

    z->high = z->low + (range * z->position_cum[pos])     / z->position_cum[0];
    z->low +=          (range * z->position_cum[pos + 1]) / z->position_cum[0];
    decode_normalize(z);
    return pos;
}

size_t lzari_decode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap,
                    size_t *consumed)
{
    static Lzari z;
    size_t textsize, count = 0;
    int i, j, k, r, c;

    if (in_len < 4)
        return (size_t)-1;
    textsize = (size_t)in[0] | ((size_t)in[1] << 8) | ((size_t)in[2] << 16) | ((size_t)in[3] << 24);
    if (textsize > out_cap)
        return (size_t)-1;

    memset(&z, 0, sizeof(z));
    z.in = in; z.in_len = in_len; z.in_pos = 4;
    z.low = 0; z.high = Q4; z.value = 0; z.mask = 0;
    for (i = 0; i < M + 2; i++)
        z.value = 2 * z.value + (unsigned long)get_bit(&z);
    start_model(&z);
    for (i = 0; i < N - F; i++)
        z.text_buf[i] = ' ';
    r = N - F;

    while (count < textsize) {
        if (z.in_pos > in_len + 4)      /* ran far past the end: truncated */
            return (size_t)-1;
        c = decode_char(&z);
        if (c < 256) {
            out[count++] = (uint8_t)c;
            z.text_buf[r++] = (uint8_t)c;
            r &= N - 1;
        } else {
            i = (r - decode_position(&z) - 1) & (N - 1);
            j = c - 255 + THRESHOLD;
            for (k = 0; k < j && count < textsize; k++) {
                c = z.text_buf[(i + k) & (N - 1)];
                out[count++] = (uint8_t)c;
                z.text_buf[r++] = (uint8_t)c;
                r &= N - 1;
            }
        }
    }
    if (consumed)
        *consumed = z.in_pos;
    return count;
}

/* --------------------------------------------------------------- encoder -- */

static void init_tree(Lzari *z)
{
    int i;

    for (i = N + 1; i <= N + 256; i++)
        z->rson[i] = NIL;
    for (i = 0; i < N; i++)
        z->dad[i] = NIL;
}

static void insert_node(Lzari *z, int r)
{
    int i, p, cmp, temp;
    const uint8_t *key;

    cmp = 1;
    key = &z->text_buf[r];
    p = N + 1 + key[0];
    z->rson[r] = z->lson[r] = NIL;
    z->match_length = 0;
    for (;;) {
        if (cmp >= 0) {
            if (z->rson[p] != NIL) {
                p = z->rson[p];
            } else {
                z->rson[p] = r;
                z->dad[r] = p;
                return;
            }
        } else {
            if (z->lson[p] != NIL) {
                p = z->lson[p];
            } else {
                z->lson[p] = r;
                z->dad[r] = p;
                return;
            }
        }
        for (i = 1; i < F; i++)
            if ((cmp = key[i] - z->text_buf[p + i]) != 0)
                break;
        if (i > THRESHOLD) {
            if (i > z->match_length) {
                z->match_position = (r - p) & (N - 1);
                if ((z->match_length = i) >= F)
                    break;
            } else if (i == z->match_length) {
                if ((temp = (r - p) & (N - 1)) < z->match_position)
                    z->match_position = temp;
            }
        }
    }
    z->dad[r] = z->dad[p];
    z->lson[r] = z->lson[p];
    z->rson[r] = z->rson[p];
    z->dad[z->lson[p]] = r;
    z->dad[z->rson[p]] = r;
    if (z->rson[z->dad[p]] == p)
        z->rson[z->dad[p]] = r;
    else
        z->lson[z->dad[p]] = r;
    z->dad[p] = NIL;
}

static void delete_node(Lzari *z, int p)
{
    int q;

    if (z->dad[p] == NIL)
        return;
    if (z->rson[p] == NIL) {
        q = z->lson[p];
    } else if (z->lson[p] == NIL) {
        q = z->rson[p];
    } else {
        q = z->lson[p];
        if (z->rson[q] != NIL) {
            do {
                q = z->rson[q];
            } while (z->rson[q] != NIL);
            z->rson[z->dad[q]] = z->lson[q];
            z->dad[z->lson[q]] = z->dad[q];
            z->lson[q] = z->lson[p];
            z->dad[z->lson[p]] = q;
        }
        z->rson[q] = z->rson[p];
        z->dad[z->rson[p]] = q;
    }
    z->dad[q] = z->dad[p];
    if (z->rson[z->dad[p]] == p)
        z->rson[z->dad[p]] = q;
    else
        z->lson[z->dad[p]] = q;
    z->dad[p] = NIL;
}

static void output_bit(Lzari *z, int bit)
{
    put_bit(z, bit);
    for (; z->shifts > 0; z->shifts--)
        put_bit(z, !bit);
}

static void encode_normalize(Lzari *z)
{
    for (;;) {
        if (z->high <= Q2) {
            output_bit(z, 0);
        } else if (z->low >= Q2) {
            output_bit(z, 1);
            z->low -= Q2;
            z->high -= Q2;
        } else if (z->low >= Q1 && z->high <= Q3) {
            z->shifts++;
            z->low -= Q1;
            z->high -= Q1;
        } else {
            break;
        }
        z->low += z->low;
        z->high += z->high;
    }
}

static void encode_char(Lzari *z, int ch)
{
    int sym = z->char_to_sym[ch];
    unsigned long range = z->high - z->low;

    z->high = z->low + (range * z->sym_cum[sym - 1]) / z->sym_cum[0];
    z->low +=          (range * z->sym_cum[sym])     / z->sym_cum[0];
    encode_normalize(z);
    update_model(z, sym);
}

static void encode_position(Lzari *z, int position)
{
    unsigned long range = z->high - z->low;

    z->high = z->low + (range * z->position_cum[position])     / z->position_cum[0];
    z->low +=          (range * z->position_cum[position + 1]) / z->position_cum[0];
    encode_normalize(z);
}

static void encode_end(Lzari *z)
{
    z->shifts++;
    if (z->low < Q1)
        output_bit(z, 0);
    else
        output_bit(z, 1);
    flush_bit_buffer(z);
}

size_t lzari_encode(const uint8_t *in, size_t in_len, uint8_t *out, size_t out_cap)
{
    static Lzari z;
    size_t pos = 0;
    int i, c, len, r, s, last_match_length;

    if (out_cap < 4)
        return (size_t)-1;
    out[0] = (uint8_t)in_len;
    out[1] = (uint8_t)(in_len >> 8);
    out[2] = (uint8_t)(in_len >> 16);
    out[3] = (uint8_t)(in_len >> 24);
    if (in_len == 0)
        return 4;

    memset(&z, 0, sizeof(z));
    z.out = out; z.out_cap = out_cap; z.out_pos = 4;
    z.low = 0; z.high = Q4; z.shifts = 0; z.buffer = 0; z.mask = 128;
    start_model(&z);
    init_tree(&z);
    s = 0;
    r = N - F;
    for (i = s; i < r; i++)
        z.text_buf[i] = ' ';
    for (len = 0; len < F && pos < in_len; len++)
        z.text_buf[r + len] = in[pos++];
    for (i = 1; i <= F; i++)
        insert_node(&z, r - i);
    insert_node(&z, r);

    do {
        if (z.match_length > len)
            z.match_length = len;
        if (z.match_length <= THRESHOLD) {
            z.match_length = 1;
            encode_char(&z, z.text_buf[r]);
        } else {
            encode_char(&z, 255 - THRESHOLD + z.match_length);
            encode_position(&z, z.match_position - 1);
        }
        last_match_length = z.match_length;
        for (i = 0; i < last_match_length && pos < in_len; i++) {
            c = in[pos++];
            delete_node(&z, s);
            z.text_buf[s] = (uint8_t)c;
            if (s < F - 1)
                z.text_buf[s + N] = (uint8_t)c;
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            insert_node(&z, r);
        }
        while (i++ < last_match_length) {
            delete_node(&z, s);
            s = (s + 1) & (N - 1);
            r = (r + 1) & (N - 1);
            if (--len)
                insert_node(&z, r);
        }
    } while (len > 0);
    encode_end(&z);

    if (z.overflow)
        return (size_t)-1;
    return z.out_pos;
}
