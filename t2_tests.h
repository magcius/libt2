
/* Test */

#pragma once

#include <stdio.h>

#define t2_t_assert(condition) do { if (!(condition)) { fprintf(stderr, "Test failed: %s, %s:%d\n", #condition, __FILE__, __LINE__); return 1; } } while (0);

typedef int (*t2_t__test_func) (void);
#define t2_t_test(fp) { .name = #fp, .func = fp }
struct t2_t_test {
    const char *name;
    t2_t__test_func func;
};

static struct t2_t_test tests[];

int main (int argc, char *argv[]) {
    int retval = 0;
    struct t2_t_test *_tests = tests; 

    while (1) {
        struct t2_t_test test = *_tests++;

        if (!test.func)
            return retval;

        if (!test.func ()) {
            fprintf (stderr, "%s: OK\n", test.name);
        } else {
            fprintf (stderr, "%s: FAIL\n", test.name);
            retval = 1;
        }
    }
}
