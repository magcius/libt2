
/* t2_co: A simple coroutine library designed for parsing tasks
 * and other half-serious, non-prodution tools. */

/* Written by Jasper St. Pierre <jstpierre@mecheye.net>
 * I license this work into the public domain. */

/* Motivation:
 *
 * Turning a simple, recursive parser into a streaming one is
 * traditionally extremely tedious. Natural state represented
 * in local variables becomes a mess of state tracking and enums.
 *
 * For instance, this C code parses a simple key/value pair format
 * into an (imaginary) dictionary.
 *
 *     void parse_dict (dict *d, char *S) {
 *         char *key = parse_string (&S);
 *         if (*S++ != ':') error ();
 *         char *value = parse_string (&S);
 *         dict_set (d, key, value);
 *
 *         if (*S++ == ',') parse_dict (d, S);
 *     }
 *
 *     char * parse_string (char **S_p) {
 *         char *S = *S_p, *begin = S;
 *         if (*S++ != '"') error ();
 *         while (*S != '"') S++;
 *         char *end = S;
 *         *end = '\0';
 *         *S_p = end + 1;
 *         return begin + 1;
 *     }
 *
 * If any of the "S++"s would need to be broken into a large internal
 * state machine every time, that would require a heavy rewrite
 * from this other simple code.
 *
 * With a few tweaks, we can turn this into something that can handle
 * streaming data without too much pain using t2_co.
 *
 *     char read_char (char **S) {
 *         // Assume that NUL = we need more data.
 *         if (**S == '\0') t2_co_pause ();
 *         *S = *S + 1;
 *         return **S;
 *     }
 *
 *     void parse_dict (dict *d, char *S) {
 *         char *key = parse_string (&S);
 *         if (read_char (&S) != ':') error ();
 *         char *value = parse_string (&S);
 *         dict_set (d, key, value);
 *
 *         if (read_char (&S) == ',') parse_dict (d, S);
 *     }
 *
 *     char * parse_string (char **S_p) {
 *         char *S = *S_p, *begin = S;
 *         if (read_char (&S) != '"') error ();
 *         while (read_char (&S) != '"') ;
 *         char *end = S;
 *         *end = '\0';
 *         *S_p = end + 1;
 *         return begin + 1;
 *     }
 *
 * The idea is that execution of parse_dict() will pause when it needs
 * more data, ready to be resumed later. The bottom of this file contains
 * a test case showing how to use t2_co on this exact parser.
 */

#ifdef _WIN32

/* We assume Windows.h is included. */

struct t2_co {
    void *parent, *fiber;
};

#else /* _WIN32 */

#include <signal.h>
#include <stdint.h>
#include <ucontext.h>

struct t2_co {
    ucontext_t parent, ctx;
    uint8_t stack[SIGSTKSZ];
};

#endif /* !_WIN32 */

void t2_co_create (struct t2_co *co, void (*func) (void *data), void *data);

/* Called from outside a coroutine, to resume execution of the coroutine
 * that has been started. */
void t2_co_resume (struct t2_co *co);

/* Called from inside a coroutine, to pause and return execution to caller,
 * immediately after the resume that caused it. */
void t2_co_pause (void);

#ifdef T2_CO_IMPLEMENTATION

#ifdef _WIN32

#include <stdlib.h>

void t2_co_create (struct t2_co *co, void (*func) (void *data), void *data) {
    co->parent = ConvertThreadToFiber (NULL);
    co->fiber = CreateFiber (0, func, data);
}

static struct t2_co *t2_co__global;
void t2_co_resume (struct t2_co *co) {
    t2_co__global = co;
    SwitchToFiber (co->ctx);
    t2_co__global = NULL;
}

void t2_co_pause (void) {
    struct t2_co *co = t2_co__global;
    SwitchToFiber (co->parent);
}

#else

#include <stdlib.h>

void t2_co_create (struct t2_co *co, void (*func) (void *data), void *data) {
    getcontext (&co->ctx);
    co->ctx.uc_link = &co->parent;
    co->ctx.uc_stack.ss_sp = &co->stack;
    co->ctx.uc_stack.ss_size = sizeof (co->stack);
    makecontext (&co->ctx, (void (*) (void)) func, 1, data);
}

static struct t2_co *t2_co__global;
void t2_co_resume (struct t2_co *co) {
    t2_co__global = co;
    swapcontext (&co->parent, &co->ctx);
    t2_co__global = NULL;
}

void t2_co_pause (void) {
    struct t2_co *co = t2_co__global;
    swapcontext (&co->ctx, &co->parent);
}

#endif /* !_WIN32 */

#endif /* T2_CO_IMPLEMENTATION */

#ifdef T2_RUN_TESTS

#include "t2_tests.h"

#include <stdlib.h>
#include <string.h>

static char read_char (char **S) {
    if (**S == '\0') t2_co_pause ();
    char s = **S;
    *S = *S + 1;
    return s;
}

static struct {
    char *k, *v;
    int want;
    int error;
} test_parser_data;

static void error (void) {
    test_parser_data.error = 1;
    abort ();
}

static char * parse_string (char **S_p) {
    char *S = *S_p, *begin = S;
    if (read_char (&S) != '"') error ();
    while (read_char (&S) != '"') ;
    char *end = S - 1;
    *end = '\0';
    *S_p = end + 1;
    return begin + 1;
}

static void parse_dict (char *S) {
    char *key = parse_string (&S);
    if (read_char (&S) != ':') error ();
    char *value = parse_string (&S);

    test_parser_data.k = key;
    test_parser_data.v = value;

    if (read_char (&S) == ',') {
        test_parser_data.want = 1;
        parse_dict (S);
    } else {
        test_parser_data.want = 0;
    }
}

static int test_parser (void) {
    struct t2_co co = {};
    char buf[8192] = {}, *e = buf;

    e = stpcpy (e, "\"A\":\"a\",");
    t2_co_create (&co, (void (*) (void *)) parse_dict, buf);
    t2_co_resume (&co);

    /* At this point, we should have parsed the values, but should be waiting for more data... */
    t2_t_assert (strcmp (test_parser_data.k, "A") == 0);
    t2_t_assert (strcmp (test_parser_data.v, "a") == 0);
    t2_t_assert (test_parser_data.want == 1);

    e = stpcpy (e, "\"B\":\"b\",");
    t2_co_resume (&co);

    t2_t_assert (strcmp (test_parser_data.k, "B") == 0);
    t2_t_assert (strcmp (test_parser_data.v, "b") == 0);
    t2_t_assert (test_parser_data.want == 1);

    e = stpcpy (e, "\"C\":\"c\";");
    t2_co_resume (&co);

    t2_t_assert (strcmp (test_parser_data.k, "C") == 0);
    t2_t_assert (strcmp (test_parser_data.v, "c") == 0);
    t2_t_assert (test_parser_data.want == 0);

    return 0;
}

static struct t2_t_test tests[] = {
    t2_t_test(test_parser),
    {},
};

#endif /* T2_RUN_TESTS */
