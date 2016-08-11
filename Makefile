CFLAGS = -Wall -g -O0

all: t2_json t2_inflate

t2_json: CFLAGS += -DT2_JSON_EXAMPLE 