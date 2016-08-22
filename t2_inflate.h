
/* t2_inflate: An easy-to-read single-file implementation of INFLATE, based on RFC 1951. */

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

static void t2_z_inflate (struct t2_z_buffer *buf_in, struct t2_z_buffer *buf_out);

#ifdef T2_Z_IMPLEMENTATION

#include <stdio.h>
#include <string.h>

#define t2_d_die(msg) do { fprintf(stderr, "%s, %s:%d\n", msg, __FILE__, __LINE__); asm("int3"); exit(1); } while (0)
#define t2_d_assert(condition) do { if (!(condition)) { fprintf(stderr, "Assertion failed: %s, %s:%d\n", #condition, __FILE__, __LINE__); asm("int3"); exit(1); } } while (0);

/* A buffer to read / write from. */

static void t2_z__buffer_copy (struct t2_z_buffer *out, struct t2_z_buffer *in, ssize_t in_offset, size_t length) {
    t2_d_assert (in->position + in_offset + length < in->size);
    t2_d_assert (out->position + length < out->size);
    memmove (out->data + out->position, in->data + in->position + in_offset, length);
    out->position += length;
    in->position += length;
}

static void t2_z__buffer_write_byte (struct t2_z_buffer *out, uint8_t byte) {
    t2_d_assert (out->position + 1 < out->size);
    out->data[out->position++] = byte;
}

static uint8_t t2_z__buffer_read_byte (struct t2_z_buffer *in) {
    t2_d_assert (in->position < in->size);
    return in->data[in->position++];
}

/* Reads a buffer a bit at a time, in DEFLATE order. That is, we read
 * starting from the LSB of each byte, and having that bit define the
 * MSB of an N-bit value. */
struct t2_z__bitreader {
    struct t2_z_buffer *buffer;

    /* The current byte we're at. */
    uint8_t byte;
    /* The number of "bits left" in byte, from 0-8, inclusive. */
    uint8_t bits_left;
};

/* "Flush" means skip forward to the next byte, throwing out the
 * rest of the data in the current byte. */
static void t2_z__bitreader_flush (struct t2_z__bitreader *b) {
    b->bits_left = 0;
}

static uint64_t t2_z__bitreader_read (struct t2_z__bitreader *b, int nbits) {
    uint64_t output = 0;

    t2_d_assert (nbits <= 64);

    while (nbits > 0) {
        if (b->bits_left == 0) {
            b->byte = t2_z__buffer_read_byte (b->buffer);
            b->bits_left = 8;
        }

        uint8_t bits_to_read = (nbits < b->bits_left) ? nbits : b->bits_left;

        uint8_t mask = (1 << bits_to_read) - 1;
        output = (output << bits_to_read) | (b->byte & mask);

        nbits -= bits_to_read;
        b->bits_left -= bits_to_read;
        b->byte >>= bits_to_read;
    }

    return output;
}

/* Deflate state. Since we use stack frames and everything is guaranteed
 * to be in memory, there's not much in here -- basically, stuff we pass
 * around to internals so we don't have to pass a bunch of args. */

struct t2_z__state {
    struct t2_z_buffer buffer_in;
    struct t2_z_buffer buffer_out;
    struct t2_z__bitreader bitreader;
};

/* Huffman tables. */

/* This is intentionally slow for readability. A faster system would
 * use the prefix / overflow tables used in gzip. */

enum { T2_Z__HUFFMAN_TABLE_MAX_LEN = 15 };

/* Each individual code length has a range of codes, starting with
 * first_code, and continuing on in ascending order for num_codes.
 * The symbol for the Nth code can be found in code_idx_to_symbol. */ 
struct t2_z__huffman_table_length {
    uint32_t first_code; uint32_t num_codes; uint32_t code_idx_to_symbol[];
};

struct t2_z__huffman_table {
    uint8_t min_length, max_length;

    /* We use a statically defined huffman table with space for all
     * possible code lengths and all possible codes. This should not
     * take that much space. Some, admittedly, incredibly stupid
     * metaprogramming is used to define each length, since we need
     * the length to be a compile-time constant. */

    /* XXX: This is incredibly stupid metaprogramming. */
#define HUFFMAN_LENGTH(n) struct { uint32_t first_code; uint32_t num_codes; uint32_t code_idx_to_symbol[(1 << n) - 1]; } length_##n;
    HUFFMAN_LENGTH(1);
    HUFFMAN_LENGTH(2);
    HUFFMAN_LENGTH(3);
    HUFFMAN_LENGTH(4);
    HUFFMAN_LENGTH(5);
    HUFFMAN_LENGTH(6);
    HUFFMAN_LENGTH(7);
    HUFFMAN_LENGTH(8);
    HUFFMAN_LENGTH(9);
    HUFFMAN_LENGTH(10);
    HUFFMAN_LENGTH(11);
    HUFFMAN_LENGTH(12);
    HUFFMAN_LENGTH(13);
    HUFFMAN_LENGTH(14);
    HUFFMAN_LENGTH(15);
};

/* And some more silly stuff is used to get each code length table. */ 
static struct t2_z__huffman_table_length * t2_z__huffman_table_select_length (struct t2_z__huffman_table *table, uint8_t level) {
    switch (level) {
        case 1: return (struct t2_z__huffman_table_length *) &table->length_1;
        case 2: return (struct t2_z__huffman_table_length *) &table->length_2;
        case 3: return (struct t2_z__huffman_table_length *) &table->length_3;
        case 4: return (struct t2_z__huffman_table_length *) &table->length_4;
        case 5: return (struct t2_z__huffman_table_length *) &table->length_5;
        case 6: return (struct t2_z__huffman_table_length *) &table->length_6;
        case 7: return (struct t2_z__huffman_table_length *) &table->length_7;
        case 8: return (struct t2_z__huffman_table_length *) &table->length_8;
        case 9: return (struct t2_z__huffman_table_length *) &table->length_9;
        case 10: return (struct t2_z__huffman_table_length *) &table->length_10;
        case 11: return (struct t2_z__huffman_table_length *) &table->length_11;
        case 12: return (struct t2_z__huffman_table_length *) &table->length_12;
        case 13: return (struct t2_z__huffman_table_length *) &table->length_13;
        case 14: return (struct t2_z__huffman_table_length *) &table->length_14;
        case 15: return (struct t2_z__huffman_table_length *) &table->length_15;
        default: t2_d_die ("Invalid code length");
    }
}

static uint64_t t2_z__huffman_table_read (struct t2_z__bitreader *bitreader, struct t2_z__huffman_table *table) {
    /* Codes are guaranteed to be at least min_length long, so
     * read at least that many bits. */
    uint8_t code_length = table->min_length;

    /* Huffman codes are stored in LSB-first order, rather than
     * MSB-first, which is how the rest of the values in DEFLATE
     * are stored. As such, we read this a bit at a time. */

    /* XXX: This feels "slow" and looks bad but is probably fine
     * since it's likely to be in a register / L1 cache. I still
     * feel the bitreader could do more to support this usecase
     * though. */ 
    uint16_t code = 0;
    for (uint8_t i = 0; i < code_length; i++)
        code = (code << 1) | t2_z__bitreader_read (bitreader, 1);

    while (1) {
        struct t2_z__huffman_table_length *len_table = t2_z__huffman_table_select_length (table, code_length);

        if (code >= len_table->first_code && code < len_table->first_code + len_table->num_codes)
            return len_table->code_idx_to_symbol[code - len_table->first_code];

        code = (code << 1) | t2_z__bitreader_read (bitreader, 1);
        code_length++;

        t2_d_assert (code_length <= table->max_length);
    }
}

struct t2_z__huffman_tables {
    struct t2_z__huffman_table literal;
    struct t2_z__huffman_table distance;
};

/* Builds a Huffman table given a map of symbols to a code length, using
 * a similar, equivalent algorithm to RFC 3.2.2. */
static struct t2_z__huffman_table t2_z__build_huffman_table (uint8_t *sym_to_code_length, size_t num_symbols) {
    struct t2_z__huffman_table table = {};

    table.min_length = 16;
    table.max_length = 0;

    /* First, count up how many codes we have for each length. */
    for (size_t symbol = 0; symbol < num_symbols; symbol++) {
        uint16_t code_length = sym_to_code_length[symbol];

        /* If the code length is 0, then the symbol does not participate
         * in code construction. */
        if (code_length == 0)
            continue;

        /* Assign our symbol. */
        struct t2_z__huffman_table_length *len_table = t2_z__huffman_table_select_length (&table, code_length);
        len_table->code_idx_to_symbol[len_table->num_codes] = symbol;
        len_table->num_codes++;

        if (code_length < table.min_length)
            table.min_length = code_length;
        if (code_length > table.max_length)
            table.max_length = code_length;
    }

    /* Now create the first code for each code length. */
    for (uint8_t code_length = table.min_length + 1; code_length < table.max_length; code_length++) {
        struct t2_z__huffman_table_length *len_table = t2_z__huffman_table_select_length (&table, code_length);
        struct t2_z__huffman_table_length *len_table_prev = t2_z__huffman_table_select_length (&table, code_length - 1);
        len_table->first_code = (len_table_prev->first_code + len_table_prev->num_codes) << 1;
    }

    return table;
}

/* Dynamic huffman tables are stored in an interesting format that
 * is itself specified in Huffman codes and has some minimal compression.
 * 
 * To construct a literal / distance distance Huffman table, a Huffman
 * table called HCLEN specifies an alphabet of symbols which specifies
 * some simple RLE and ZLE. */ 
static struct t2_z__huffman_table t2_z__read_dyn_huffman_table (struct t2_z__state *state, struct t2_z__huffman_table *hclen, size_t count) {
    uint8_t sym_to_code_length[count];
    memset (&sym_to_code_length, 0, count);

    for (uint8_t i = 0; i < count; i++) {
        /* Figuring out what to call variable is confusing. It's not
         * a code length -- that's the output after ZLE / RLE. "symbol"
         * is confusing since we're building a map of symbols to code
         * lengths, so the symbol we have a code length for is actually "i".
         *
         * Building a Huffman table by reading a Huffman table is confusing.
         */
        uint8_t op = t2_z__huffman_table_read (&state->bitreader, hclen);

        /* op 0 - 15: literal code length.
         * op 16, NN: Copy the last code length N+3 times.
         * op 17, NNN: Zero the next N+3 code lengths.
         * op 18, NNNNNNN: Zero the next N+11 code lengths. */ 
        if (op <= 15) {
            sym_to_code_length[i] = op;
        } else if (op == 16) {
            uint8_t repeat_length = 3 + t2_z__bitreader_read (&state->bitreader, 2);
            uint8_t code_length = sym_to_code_length[i - 1];
            for (uint8_t j = 0; j < repeat_length; j++)
                sym_to_code_length[i++] = code_length; 
        } else if (op == 17) {
            uint8_t repeat_length = 3 + t2_z__bitreader_read (&state->bitreader, 3);
            for (uint8_t j = 0; j < repeat_length; j++)
                sym_to_code_length[i++] = 0; 
        } else if (op == 18) {
            uint8_t repeat_length = 11 + t2_z__bitreader_read (&state->bitreader, 7);
            for (uint8_t j = 0; j < repeat_length; j++)
                sym_to_code_length[i++] = 0; 
        } else {
            t2_d_die ("Invalid dyn table code length");
        }
    }

    return t2_z__build_huffman_table (sym_to_code_length, count);
}

static struct t2_z__huffman_tables t2_z__read_dyn_huffman_tables (struct t2_z__state *state) {
    struct t2_z__huffman_tables tables = {};

    uint8_t hlit  = t2_z__bitreader_read (&state->bitreader, 5);
    uint8_t hdist = t2_z__bitreader_read (&state->bitreader, 5);
    uint8_t hclen = t2_z__bitreader_read (&state->bitreader, 4);

    /* First, craft the HCLEN table, which helps us construct the symbol
     * to code length mapping for the literal / distance tables. */ 

    /* The symbols of the HCLEN table are laid out in this order... */
    const uint8_t hclen_symbols[] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

    uint8_t hclen_sym_to_code_lengths[18] = {};

    for (uint8_t i = 0; i < hclen + 4; i++) {
        /* Each code length for HCLEN is specified directly as a 3-bit value */
        uint8_t hclen_code_length = t2_z__bitreader_read (&state->bitreader, 3);

        if (hclen_code_length == 0)
            continue;

        uint8_t hclen_symbol = hclen_symbols[i];
        hclen_sym_to_code_lengths[hclen_symbol] = hclen_code_length;
    }

    struct t2_z__huffman_table hclen_table = t2_z__build_huffman_table (hclen_sym_to_code_lengths, sizeof (hclen_sym_to_code_lengths));

    /* Now we read the literal / distance tables using our constructed HCLEN table. */
    tables.literal = t2_z__read_dyn_huffman_table (state, &hclen_table, hlit + 257);
    tables.distance = t2_z__read_dyn_huffman_table (state, &hclen_table, hdist + 1);

    return tables;
}

static struct t2_z__huffman_tables *t2_z__fixed_huffman_tables (void) {
    static struct t2_z__huffman_tables tables;
    static int init = 0;

    if (init)
        return &tables;

    /* literal */
    {
        uint8_t sym_to_code_length[288];
        size_t sym;

        for (sym =   0; sym <= 143; sym++) sym_to_code_length[sym] = 8;
        for (sym = 144; sym <= 255; sym++) sym_to_code_length[sym] = 9;
        for (sym = 256; sym <= 279; sym++) sym_to_code_length[sym] = 7;
        for (sym = 280; sym <= 287; sym++) sym_to_code_length[sym] = 8;

        tables.literal = t2_z__build_huffman_table (sym_to_code_length, sizeof (sym_to_code_length));
    }

    /* distance */
    {
        uint8_t sym_to_code_length[32];
        size_t sym;

        for (sym = 0; sym < 32; sym++) sym_to_code_length[sym] = 5;

        tables.distance = t2_z__build_huffman_table (sym_to_code_length, sizeof (sym_to_code_length));
    }

    init = 1;
    return &tables;
}

/* The symbols out of the length / distance tables aren't used directly
 * as length / distance values. No, that would be too easy. Instead,
 * the symbols that come out of the Huffman tables are just lookups.
 * RFC 3.2.5 has these tables.
 */
static uint16_t t2_z__decode_length (struct t2_z__state *state, uint16_t code) {
    uint16_t base, ebit;

    if (code <= 256) t2_d_die ("Invalid code.");
    else if (code <= 264) code -= 264, base =   3, ebit = 0;
    else if (code <= 268) code -= 268, base =  11, ebit = 1;
    else if (code <= 273) code -= 273, base =  19, ebit = 2;
    else if (code <= 276) code -= 276, base =  35, ebit = 3;
    else if (code <= 280) code -= 280, base =  67, ebit = 4;
    else if (code <= 284) code -= 284, base = 131, ebit = 5;
    else t2_d_die ("Invalid code.");

    uint16_t res = base + code * ((1 << ebit) - 1);
    if (ebit)
        res += t2_z__bitreader_read (&state->bitreader, ebit);
    return res;
}

static uint16_t t2_z__decode_distance (struct t2_z__state *state, uint16_t code) {
    switch (code) {
    case 0: return 1;
    case 1: return 2;
    case 2: return 3;
    case 3: return 4;
    case 4: return 5 + t2_z__bitreader_read (&state->bitreader, 1);
    case 5: return 7 + t2_z__bitreader_read (&state->bitreader, 1);
    case 6: return 9 + t2_z__bitreader_read (&state->bitreader, 2);
    case 7: return 13 + t2_z__bitreader_read (&state->bitreader, 2);
    case 8: return 17 + t2_z__bitreader_read (&state->bitreader, 3);
    case 9: return 25 + t2_z__bitreader_read (&state->bitreader, 3);
    case 10: return 33 + t2_z__bitreader_read (&state->bitreader, 4);
    case 11: return 49 + t2_z__bitreader_read (&state->bitreader, 4);
    case 12: return 65 + t2_z__bitreader_read (&state->bitreader, 5);
    case 13: return 97 + t2_z__bitreader_read (&state->bitreader, 5);
    case 14: return 129 + t2_z__bitreader_read (&state->bitreader, 6);
    case 15: return 193 + t2_z__bitreader_read (&state->bitreader, 6);
    case 16: return 257 + t2_z__bitreader_read (&state->bitreader, 7);
    case 17: return 385 + t2_z__bitreader_read (&state->bitreader, 7);
    case 18: return 513 + t2_z__bitreader_read (&state->bitreader, 8);
    case 19: return 769 + t2_z__bitreader_read (&state->bitreader, 8);
    case 20: return 1025 + t2_z__bitreader_read (&state->bitreader, 9);
    case 21: return 1537  + t2_z__bitreader_read (&state->bitreader, 9);
    case 22: return 2049 + t2_z__bitreader_read (&state->bitreader, 10);
    case 23: return 3073 + t2_z__bitreader_read (&state->bitreader, 10);
    case 24: return 4097 + t2_z__bitreader_read (&state->bitreader, 11);
    case 25: return 6145 + t2_z__bitreader_read (&state->bitreader, 11);
    case 26: return 8193 + t2_z__bitreader_read (&state->bitreader, 12);
    case 27: return 12289 + t2_z__bitreader_read (&state->bitreader, 12);
    case 28: return 16385 + t2_z__bitreader_read (&state->bitreader, 13);
    case 29: return 24577 + t2_z__bitreader_read (&state->bitreader, 13);
    default: t2_d_die ("Invalid distance");
    }
}

static void t2_z__read_compressed_block (struct t2_z__state *state, struct t2_z__huffman_tables *tables) {
    /* The format of a Huffman-compressed block is specified in RFC 3.2.3. */
    while (1) {
        uint16_t op = t2_z__huffman_table_read (&state->bitreader, &tables->literal);

        /* op 0 - 255: literal byte output.
         * op 256: end of block.
         * op 257..285: distance/length pair, copy part of output buffer. */ 
        if (op <= 255) {
            t2_z__buffer_write_byte (&state->buffer_out, op);
        } else if (op == 256) {
            break;
        } else if (op <= 285) {
            uint16_t length, distance;

            distance = t2_z__bitreader_read (&state->bitreader, 5);
            distance = t2_z__decode_distance (state, distance);

            length = t2_z__decode_length (state, op);
            t2_z__buffer_copy (&state->buffer_out, &state->buffer_out, -distance, length);
        } else {
            t2_d_die ("Illegal code");
        }
    }
}

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

static void t2_z__inflate (struct t2_z__state *state) {
    struct t2_z__bitreader *bitreader = &state->bitreader;

    while (1) {
        uint8_t block_header = t2_z__bitreader_read (bitreader, 3);
        uint8_t block_type = block_header & T2_Z__BLOCK_TYPE_MASK;

        if (block_type == T2_Z__BLOCK_TYPE_UNCOMPRESSED) {
            /* The data in an uncompressed block is byte-aligned, so we flush the bitreader here. */
            t2_z__bitreader_flush (bitreader);
            uint16_t length = t2_z__bitreader_read (bitreader, 16);
            uint16_t nlength = t2_z__bitreader_read (bitreader, 16);
            t2_d_assert (length == (nlength ^ 0xFFFF));
            /* Just a copy -- easy. */
            t2_z__buffer_copy (&state->buffer_out, &state->buffer_in, 0, length);
        } else if (block_type == T2_Z__BLOCK_TYPE_COMPRESSED_FIXED) {
            t2_z__read_compressed_block (state, t2_z__fixed_huffman_tables ());
        } else if (block_type == T2_Z__BLOCK_TYPE_COMPRESSED_DYN) {
            struct t2_z__huffman_tables tables = t2_z__read_dyn_huffman_tables (state);
            t2_z__read_compressed_block (state, &tables);
        } else {
            t2_d_die ("Invalid block type");
        }

        if (block_header & T2_Z__BLOCK_FLAG_FINAL)
            break;
    }
}

static void t2_z_inflate (struct t2_z_buffer *buf_in, struct t2_z_buffer *buf_out) {
    struct t2_z__state state = {
        .buffer_in = *buf_in,
        .buffer_out = *buf_out,
    };
    state.bitreader = ((struct t2_z__bitreader) { .buffer = &state.buffer_in });
    t2_z__inflate (&state);
}

#ifdef T2_RUN_TESTS

#include "t2_tests.h"

static struct t2_z_buffer buffer_from_cstring (const char *string) {
    struct t2_z_buffer buffer = {
        .data = (uint8_t *) string,
        .size = strlen (string),
    };
    return buffer;
}

static int test_bitreader (void) {
    /* 'A' '10101010' '01010101' */
    struct t2_z_buffer buffer = buffer_from_cstring ("A\xF7\x12\x34\x56\x78");

    struct t2_z__bitreader b = {
        .buffer = &buffer, 
    };

    uint16_t bits;

    bits = t2_z__bitreader_read (&b, 8);
    t2_t_assert (bits == 'A');
    bits = t2_z__bitreader_read (&b, 4);
    t2_t_assert (bits == '\x07'); /* '0111' */ 
    bits = t2_z__bitreader_read (&b, 4);
    t2_t_assert (bits == '\x0F'); /* '1111' */
    bits = t2_z__bitreader_read (&b, 16);
    t2_t_assert (bits == 0x1234);
    bits = t2_z__bitreader_read (&b, 12);
    t2_t_assert (bits == 0x0568);
    bits = t2_z__bitreader_read (&b, 4);
    t2_t_assert (bits == 0x7);

    return 0;
}

static int test_inflate (void) {
    uint8_t buf_in[] = { 75, 203, 207, 7, 0 };
    uint8_t buf_out[4096] = {};

    t2_z_inflate (T2_Z_BUFFER_FROM_STATIC (buf_in), T2_Z_BUFFER_FROM_STATIC (buf_out));

    t2_t_assert (memcmp (buf_out, "foo", 3) == 0);

    return 0;
}

static struct t2_t_test tests[] = {
    t2_t_test(test_bitreader),
    t2_t_test(test_inflate),
    {},
};

#endif /* T2_RUN_TESTS */

#endif /* T2_IMPLEMENTATION */
