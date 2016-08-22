CFLAGS = -Wall -g -O0

all: t2_json t2_inflate t2_co

t2_json: CFLAGS += -DT2_JSON_EXAMPLE

t2_inflate: CFLAGS += -DT2_RUN_TESTS -DT2_Z_IMPLEMENTATION
t2_inflate: t2_inflate.h
	$(CC) -o $@ -include $< main.c $(CPPFLAGS) $(CFLAGS)

t2_co: CFLAGS += -DT2_RUN_TESTS -DT2_CO_IMPLEMENTATION
t2_co: t2_co.h
	$(CC) -o $@ -include $< main.c $(CPPFLAGS) $(CFLAGS)
