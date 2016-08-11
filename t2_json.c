
/* t2_json: A simple, embedded JSON parser */

/* Written by Jasper St. Pierre <jstpierre@mecheye.net>
 * I license this work into the public domain. */

#include "t2_json.h"

#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Write code-point cp in CESU-8 form into p */
static int cesu8_write(char *p, uint16_t cp) {
    if      (cp <= 0x007f) { *p++ = cp; return 1; }
    else if (cp <= 0x07ff) { *p++ = (0xc0 | (cp >>  6)); *p++ = (0x80 | (cp & 0x3f)); return 2; }
    else                   { *p++ = (0xe0 | (cp >> 12)); *p++ = (0x80 | ((cp >> 6) & 0x3f)); *p++ = (0x80 | (cp & 0x3f)); return 3; }
}

/* Parse a hex digit */
static uint8_t dhexd(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 0x0A;
    if (c >= 'A' && c <= 'F') return c - 'A' + 0x0A;
    assert(false);
}

/* Parse a four-digit hex sequence */
static uint16_t dhex(char *p)
{
    return (dhexd(p[0]) << 12) | (dhexd(p[1]) << 8) | (dhexd(p[2]) << 4) | dhexd(p[3]);
}

/* Scanner */

static inline void t2_json__scanner_init(struct t2_json__scanner *j, char *S) { j->S = S; }

static inline char  pk    (struct t2_json__scanner *j, int L) { return j->S[L]; }
static inline char  hd    (struct t2_json__scanner *j) { return j->S[0]; }
static inline void  adv   (struct t2_json__scanner *j) { ++j->S; }
static inline void  advn  (struct t2_json__scanner *j, int n) { j->S += n; }
static inline void  sync  (struct t2_json__scanner *j) { while (isblank(hd(j))) adv(j); }
static inline bool  match (struct t2_json__scanner *j, char *S) { return strcmp(j->S, S); }
static inline bool  breq  (struct t2_json__scanner *j, char c) {
    sync(j);
    if (hd(j) == c) {
        adv(j);
        return true;
    } else {
#if T2_JSON_DEBUG
        assert(false);
#endif /* t2_json_DEBUG */
        return false;
    }
}

/* t2_json_type, with the addition of the chars }]:, is our collection of tokens */
static enum t2_json_type tok(struct t2_json__scanner *j)
{
    sync(j);

    switch (hd(j)) {
    case '\0':
        return T2_JSON_END;
    case '"':
    case '\'':
        return T2_JSON_STRING;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
        return T2_JSON_NUMBER;
    case '[':
    case '{':
    case ']':
    case '}':
    case ':':
    case ',':
        return (enum t2_json_type) hd(j);
    default:
        break;
    }

    if (match(j, "false")) return T2_JSON_FALSE;
    if (match(j, "true"))  return T2_JSON_TRUE;
    if (match(j, "null"))  return T2_JSON_NULL;

    return T2_JSON_ERROR;
}

static void chomp_string(struct t2_json__scanner *j)
{
    sync(j);

    char delim = hd(j);
    while (hd(j) != '\0') {
        adv(j);

        if (hd(j) == delim) {
            adv(j);
            break;
        }

        if (hd(j) == '\\')
            adv(j);
    }
}

static char *get_string(struct t2_json__scanner *j, char *V, int Vl)
{
    sync(j);

    char delim = hd(j);

    int i = 0;
    for (i = 0; i < Vl - 1;) {
        adv(j);

        if (hd(j) == delim) {
            adv(j);
            break;
        }

        if (hd(j) == '\\') {
            switch (pk(j, 1)) {
            case '"':  V[i++] = '"';  break;
            case '\'': V[i++] = '\''; break;
            case '\\': V[i++] = '\\'; break;
            case 'b':  V[i++] = '\b'; break;
            case 'f':  V[i++] = '\f'; break;
            case 'n':  V[i++] = '\n'; break;
            case 'r':  V[i++] = '\r'; break;
            case 't':  V[i++] = '\t'; break;
            case 'u': {
                uint16_t cp = dhex(j->S+2);
                i += cesu8_write(&V[i], cp);
                advn(j, 4);
                break;
            }
            }
            adv(j);
        } else {
            V[i++] = hd(j);
        }
    }
    V[i] = '\0';
    return V;
}

static double chomp_number(struct t2_json__scanner *j)
{
    sync(j);
    /* XXX: We should at least verify the number ourselves
     * so we don't parse hexadecimals and such. */
    return strtod(j->S, &j->S);
}

static void jreq(t2_json_t *j, char c) { if (!breq(&j->s, c)) j->e = true; }

void t2_json_init(t2_json_t *j, char *S) { memset(j, 0, sizeof(*j)); t2_json__scanner_init(&j->s, S); }

enum t2_json_type t2_json_get_type(t2_json_t *j) { if (j->e) return T2_JSON_ERROR; return tok(&j->s); }
bool t2_json_has_error(t2_json_t *j) { return t2_json_get_type(j) == T2_JSON_ERROR; }

char *t2_json_save(t2_json_t *j) { j->r.s[j->r.n++] = j->s; return j->s.S; }
char *t2_json_restore(t2_json_t *j) { j->s = j->r.s[--j->r.n]; return j->r.s[j->r.n+1].S; }

double t2_json_get_number(t2_json_t *j) { return chomp_number(&j->s); }

char *t2_json_get_string(t2_json_t *j, char *buf, int len) { return get_string(&j->s, buf, len); }

int t2_json_string_len(t2_json_t *j) {
    char *S = t2_json_save(j);
    chomp_string(&j->s);
    char *S2 = t2_json_restore(j);
    /* For the convenience of the user, also include the trailing NUL. */
    return S2 - S + 1;
}

void t2_json_enter_object(t2_json_t *j)   { jreq(j, '{'); }
void t2_json_enter_array(t2_json_t *j)    { jreq(j, '['); }
void t2_json_leave_object(t2_json_t *j)   { jreq(j, '}'); }
void t2_json_leave_array(t2_json_t *j)    { jreq(j, ']'); }

/* XXX: I need a better name for this function that doesn't include a
 * homophone. */
void t2_json_read_key(t2_json_t *j)       { jreq(j, ':'); }

bool t2_json_has_next_value(t2_json_t *j) { return t2_json_get_type(j) == ','; }
void t2_json_next_value(t2_json_t *j)     { jreq(j, ','); }

void t2_json_skip(t2_json_t *j);

void t2_json_get_array_child(t2_json_t *j, int idx) {
    while (idx--) {
        t2_json_skip(j);
        jreq(j, ',');
    }
}
bool t2_json_find_object_child(t2_json_t *j, char *key) {
    char kbuf[T2_JSON_STATIC_BUFFER_LENGTH];
    assert(strlen(key) < sizeof(kbuf));

    while (true) {
        get_string(&j->s, kbuf, sizeof(kbuf));
        t2_json_read_key(j);
        if (strncmp(key, kbuf, sizeof(kbuf)) == 0)
            return true;
        t2_json_skip(j);
        if (!t2_json_has_next_value(j))
            break;
        t2_json_next_value(j);
    }
    return false;
}

static void skip_string(t2_json_t *j)  { chomp_string(&j->s); }
static void skip_number(t2_json_t *j)  { chomp_number(&j->s); }

static void skip_array(t2_json_t *j) {
    t2_json_enter_array(j);
    while (true) {
        t2_json_skip(j);
        if (t2_json_has_next_value(j))
            adv(&j->s);
        else
            break;
    }
    adv(&j->s);
}
static void skip_object(t2_json_t *j) {
    t2_json_enter_object(j);
    while (true) {
        t2_json_skip(j);
        jreq(j, ':');
        t2_json_skip(j);
        if (t2_json_has_next_value(j))
            adv(&j->s);
        else
            break;
    }
    adv(&j->s);
}

void t2_json_skip(t2_json_t *j) {
    switch (t2_json_get_type(j)) {
    case T2_JSON_FALSE:  advn(&j->s, 5); break;
    case T2_JSON_TRUE:   advn(&j->s, 4); break;
    case T2_JSON_NULL:   advn(&j->s, 4); break;
    case T2_JSON_ARRAY:  skip_array(j); break;
    case T2_JSON_OBJECT: skip_object(j); break;
    case T2_JSON_NUMBER: skip_number(j); break;
    case T2_JSON_STRING: skip_string(j); break;
    default: break;
    }
}

#if T2_JSON_PRINT_VALUE
#include <stdio.h>

static void print_value(t2_json_t *j);

static void print_array(t2_json_t *j) {
    t2_json_enter_array(j); printf("[");
    while (true) {
        print_value(j);
        if (!t2_json_has_next_value(j)) break;
        t2_json_next_value(j); printf(", ");
    }
    t2_json_leave_array(j); printf("]");
}
static void print_object(t2_json_t *j) {
    t2_json_enter_object(j); printf("{");
    while (true) {
        print_value(j);
        t2_json_read_key(j); printf(": ");
        print_value(j);
        if (!t2_json_has_next_value(j)) break;
        t2_json_next_value(j); printf(", ");
    }
    t2_json_leave_object(j); printf("}");
}
static void print_value(t2_json_t *j) {
    char strbuf[T2_JSON_STATIC_BUFFER_LENGTH];

    switch (t2_json_get_type(j)) {
    case T2_JSON_FALSE:  printf("false"); t2_json_skip(j); break;
    case T2_JSON_TRUE:   printf("true");  t2_json_skip(j); break;
    case T2_JSON_NULL:   printf("null");  t2_json_skip(j); break;
    case T2_JSON_ARRAY:  print_array(j); break;
    case T2_JSON_OBJECT: print_object(j); break;
    case T2_JSON_NUMBER: printf("%g", t2_json_get_number(j)); break;
    case T2_JSON_STRING: printf("\"%s\"", t2_json_get_string(j, strbuf, sizeof(strbuf))); break;
    case T2_JSON_ERROR:  printf("ERROR"); break;
    default:         printf("UNK"); break;
    }
}
void t2_json_print_value(t2_json_t *j) {
    print_value(j); printf("\n");
}

#endif /* t2_json_PRINT_VALUE */

#if T2_JSON_EXAMPLE
#include <stdio.h>

int main(int argc, char *argv[])
{
    char *S = "  [ { \"hello\": 3 }, { \"baz\": false }, \"blah\\u20AC\" ]";
    t2_json_t _j, *j = &_j;

    t2_json_init(j, S);

    t2_json_save(j);
    t2_json_print_value(j);
    t2_json_restore(j);

    t2_json_enter_array(j);
    t2_json_get_array_child(j, 0);
    t2_json_save(j);
    t2_json_enter_object(j);
    t2_json_find_object_child(j, "hello");
    t2_json_print_value(j);
    t2_json_restore(j);
    t2_json_skip(j);
    t2_json_next_value(j);
    t2_json_skip(j);
    t2_json_next_value(j);
    t2_json_skip(j);
    t2_json_leave_array(j);

    return 0;
}

#endif