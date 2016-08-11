
/* t2_json: A simple, embedded JSON parser */

/* Written by Jasper St. Pierre <jstpierre@mecheye.net>
 * I license this work into the public domain. */

#pragma once

#include <stdbool.h>

/* Define to enable extra assertions to know when things go wrong. */
#define T2_JSON_DEBUG 1

/* Define to enable the printf-style functions. Requires stdio.h */
#define T2_JSON_PRINT_VALUE 1

/* Several convenience functions, like t2_json_print_value and
 * t2_json_find_object_child, have to decode strings internally.
 * This determines how long their internal static buffers are. */
#define T2_JSON_STATIC_BUFFER_LENGTH 255

enum t2_json_type {
    T2_JSON_ERROR,
    T2_JSON_END,
    T2_JSON_STRING,
    T2_JSON_NUMBER,
    T2_JSON_FALSE,
    T2_JSON_TRUE,
    T2_JSON_NULL,
    T2_JSON_OBJECT = '{',
    T2_JSON_ARRAY = '[',
};

/* Scanner -- intended to be private. Only included here 
 * so that you can place a t2_json_t on the stack. */

struct t2_json__scanner { char *S; };

/* Simple high-level parser interface */

/* A t2_json_t is composed of a scanner, along with a stack of up to ten (by default)
 * restore points, to save your position and come back later with. */

#define T2_JSON__T_STACK_LENGTH 10

typedef struct {
    struct t2_json__scanner s;
    struct { struct t2_json__scanner s[T2_JSON__T_STACK_LENGTH]; int n; } r;
    /* Error. */
    bool e;
} t2_json_t;

void t2_json_init(t2_json_t *parser, char *string);

/* Returns the current pointer into the string. */
static inline char *t2_json__parser_get_cursor(t2_json_t *j) { return j->s.S; }

/* t2_json_get_type is the main dispatch function -- it return what data type
 * the cursor is sitting on right now. */
enum t2_json_type t2_json_get_type(t2_json_t *j);
bool t2_json_has_error(t2_json_t *j);

/* These save the current position in the stack, and also return the current
 * cursor *before* these functions took effect. */
char *t2_json_save(t2_json_t *j);
char *t2_json_restore(t2_json_t *j);

/* Gets and decodes the contents of the currently pointed to string into
 * the given buffer.
 *
 * XXX: A way to detect string truncation. Return the length that would
 * have been without truncation? */
char *t2_json_get_string(t2_json_t *j, char *buf, int len);

/* Gets the length of the given string. Note that this is an upper bound
 * designed to be passed into malloc and may not accurately predict the
 * actual length of the string. */
int t2_json_string_len(t2_json_t *j);

void t2_json_enter_object(t2_json_t *j);
void t2_json_enter_array(t2_json_t *j);
void t2_json_leave_object(t2_json_t *j);
void t2_json_leave_array(t2_json_t *j);

/* To be used after an object key is read. */
void t2_json_read_key(t2_json_t *j);

/* To be used after an array or object value is read. */
void t2_json_next_value(t2_json_t *j);

/* Whether there's another value in the current array or object. */
bool t2_json_has_next_value(t2_json_t *j);

/* After entering an array or object, you can use these to skip to
 * the given indexed object or find a key by name. You probably want
 * to use t2_json_save before these methods if you're looking up multiple
 * values. */
void t2_json_get_array_child(t2_json_t *j, int idx);
bool t2_json_find_object_child(t2_json_t *j, char *key);

/* Skips over the current value. This can be used for content-less
 * values like false, true, and null, and also if you just don't care
 * about the current value. */
void t2_json_skip(t2_json_t *j);

#if T2_JSON_PRINT_VALUE
/* A convenience function for debugging to help you figure out the
 * current value. */
void t2_json_print_value(t2_json_t *j);
#endif /* T2_JSON_PRINT_VALUE */
