
/* XXX: Does not work yet. */

/* t2_deflate: An easy-to-read single-file implementation of DEFLATE, based on RFC 1951. */

/* Written by Jasper St. Pierre <jstpierre@mecheye.net>
 * I license this work into the public domain. */

#pragma once

#include <stdlib.h>
#include <stdint.h>

struct t2_z_buffer {
    uint8_t *data;
    size_t size;
    size_t position;
};

#define T2_Z_BUFFER_FROM_STATIC(buf) (&((struct t2_z_buffer) { .data = buf, .size = sizeof(buf) }))

static void t2_z_deflate (struct t2_z_buffer *buf_in, struct t2_z_buffer *buf_out);

#ifdef T2_Z_IMPLEMENTATION

#include <stdio.h>
#include <string.h>

#define t2_d_die(msg) do { fprintf(stderr, "%s, %s:%d\n", msg, __FILE__, __LINE__); asm("int3"); exit(1); } while (0);
#define t2_d_assert(condition) do { if (!(condition)) { fprintf(stderr, "Assertion failed: %s, %s:%d\n", #condition, __FILE__, __LINE__); asm("int3"); exit(1); } } while (0);

/* A buffer to read / write from. */

#define t2_z__deflate_hash_num_buckets (1 << 16)
#define t2_z__deflate_hash_num_matches 4

/* Standard hash functions work by chopping up variable-length input into
 * smaller chunks, and then, for each chunk, combine the new data with the
 * internal state, and then mixing up their internal state so that the same
 * input twice doesn't cancel out combining, and that entropy is distributed
 * to all of the bits.
 *
 * Our hash function operates on fixed-length data, so we really only need
 * a unified mix / combine step. Additionally, this is being used as an input
 * to the hash table, which has N buckets, so our hash function should be
 * limited to output one of those buckets.
 */

/* Our combine step is simply to multiply against a prime number and use
 * the low bits as our bucket index. */
static uint32_t t2_z__deflate_hash (uint64_t bytes) {
    uint32_t mask = t2_z__deflate_hash_num_buckets - 1;
    return (16777619UL * bytes) & mask;
}

struct t2_z__deflate_hash_bucket {
    /* Each bucket can have up to 4 candidate matches. */
    uint8_t n_matches;
    uint64_t match[t2_z__deflate_hash_num_matches];
};

struct t2_z__deflate_hash_table {
    struct t2_z__deflate_hash_bucket buckets[t2_z__deflate_hash_num_buckets];
};

struct t2_z__deflate_state {
    struct t2_z_buffer *buf_in;
    struct t2_z_buffer *buf_out;

    struct t2_z__deflate_hash_table hash_table;
    struct t2_z__deflate_bitwriter bitwriter;
};

/* Each block starts with a 3-bit header. */
enum t2_z__block_flags {
    /* Two bits for the block type -- BTYPE. */
    T2_Z__BLOCK_TYPE_MASK             = 0x06,

    /* Block is uncompressed. */
    T2_Z__BLOCK_TYPE_UNCOMPRESSED     = 0x00,
    /* Block is compressed with fixed Huffman tables. */
    T2_Z__BLOCK_TYPE_COMPRESSED_FIXED = 0x02,
    /* Block is compressed with in-band Huffman tables. */
    T2_Z__BLOCK_TYPE_COMPRESSED_DYN   = 0x04,

    /* A flag for whether the block is the final in its series or not... */
    T2_Z__BLOCK_FLAG_FINAL            = 0x01,
};

static void t2_z__deflate_huffman_table_write (struct t2_z__deflate_state *state, struct t2_z__deflate_huffman_table *table, uint64_t v) {
}

static void t2_z__deflate_write_eof (struct t2_z__deflate_state *state) {
    t2_z__deflate_huffman_table_write (state, state->length_table, 256);
}

static void t2_z__deflate_write_literals (struct t2_z__deflate_state *state, uint64_t literals) {
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 56) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 48) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 40) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 32) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 24) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >> 16) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >>  8) & 0xFF);
    t2_z__deflate_huffman_table_write (state, state->length_table, (literals >>  0) & 0xFF);
}

static void t2_z__deflate_write_length (struct t2_z__deflate_state *state, uint64_t length) {
    t2_d_assert (length >= 3);

    uint16_t base, ebit, m;
    if (length <= 10) base = 257, ebit = 0, m = 3;
    if (length <= 18) base = 265, ebit = 1, m = 11;
    if (length <= 34) base = 269, ebit = 2, m = 19;
    /* XXX */

    uint16_t rebased_length = (length - m);
    uint16_t op = base + (rebased_length / (1 << ebit));

    t2_z__deflate_huffman_table_write (state, &state->length_table, op);
    if (ebit)
        t2_z__deflate_bitwriter_write (&state->bitwriter, rebased_length, ebit);
}

static void t2_z__deflate_write_distance (struct t2_z__deflate_state *state, uint64_t distance) {
    t2_z__deflate_bitwriter_write (&state->bitwriter, distance, 5);
}

/* There are two major parts to gzip data. The first is finding distance/length pairs
 * in large runs of text. For this, we use the classic hash approach where 8 bytes of
 * data are hashed. Each set of incoming bytes is hashed against this, and we look up
 * all candidate matches. The longest run for each is then identified with a simple
 * linear scan. It is assumed that the longest run of bytes will provide the best
 * compression. This is intuitive, but otherwise not immediately obvious with
 * Huffman coding.
 *
 * The second step is Huffman coding. Currently, we only use the "fixed" Huffman
 * tables encoded in the specification, and don't construct our own Huffman tables.
 * This drastically reduces the search space required -- there is one correct
 * canonically correct encoding, since DEFLATE is not context-adaptive.
 */
static void t2_z_deflate (struct t2_z_buffer *buf_in, struct t2_z_buffer *buf_out) {
    struct t2_z__deflate_state state = {
        .buf_in = buf_in,
        .buf_out = buf_out,
    };

    /* First, write out our block header. */
    t2_z__deflate_write_block_header (&state, T2_Z__BLOCK_FLAG_FINAL | T2_Z__BLOCK_TYPE_COMPRESSED_FIXED);

    while (1) {
        if (buf_in->position >= buf_in->size) {
            t2_z__deflate_write_eof (&state, bytes);
            break;
        }

        /* Read the next 8 bytes and hash them. */
        uint64_t bytes = * ((uint64_t *) &buf_in->data[buf_in->position]);
        uint32_t h = t2_z__deflate_hash (bytes);

        struct t2_z__deflate_hash_bucket *bucket_p = state.hash_table.buckets[h], bucket = *bucket_p;

        /* Add ourselves to the hash bucket. */
        if (bucket.n_matches < t2_z__deflate_hash_num_matches) {
            bucket_p->match[bucket_p->n_matches++] = buf_in->position;
        } else {
            /* Make room for the new value. Could use memmove or something instead, I suppose... */
            bucket_p->match[0] = bucket_p->match[1];
            bucket_p->match[1] = bucket_p->match[2];
            bucket_p->match[2] = bucket_p->match[3];
            bucket_p->match[3] = buf_in->position;
        }

        if (bucket.n_matches == 0) {
            /* No matches in the recent table. Write it out as literals. */
            goto write_literal;
        } else {
            /* We have a match. Do a linear scan to find the longest run, and then output a dist/length pair. */

            uint16_t match_distance = 0, match_length = 0;
            for (uint8_t i = bucket.n_matches - 1; i >= 0; i--) {
                uint8_t *in_data = &buf_in->data[buf_in->position];
                uint8_t *ma_data = &buf_in->data[bucket.match[i]];

                uint16_t distance = bucket.match[i] - buf_in->position;

                /* Make sure it fits in 5 bits. */
                if ((distance & 0x1f) != distance)
                    continue;

                uint16_t j = 0;
                while (*in_data++ == *ma_data++ && in_data < end && j < 65535) j++;

                if (j > match_length) {
                    match_length = j;
                    match_distance = bucket.match[i] - buf_in->position;
                }
            }

            if (match_length < 4)
                goto write_literal;

            t2_z__deflate_write_length (&state, match_length);
            t2_z__deflate_write_distance (&state, match_distance);
            buf_in->position += match_distance;
        }

    write_literal:
        buf_in->position += sizeof (bytes);
        t2_z__deflate_write_literals (&state, bytes);
    }
}

#endif /* T2_Z_IMPLEMENTATION */
