# Linux build of the core with its tests. The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -g -Isrc/core -Isrc/third/cjson
# cJSON is third-party: built without -Werror/-pedantic, as upstream intends.
CJSON_CFLAGS = -std=c99 -g -Isrc/third/cjson
CORE = src/core/vu_ver.c src/core/vu_pcsi.c src/core/vu_cat.c src/core/vu_plan.c
HDRS = $(wildcard src/core/*.h src/third/cjson/*.h)

all: test build/vucheck

build/cJSON.o: src/third/cjson/cJSON.c src/third/cjson/cJSON.h
	@mkdir -p build
	$(CC) $(CJSON_CFLAGS) -c -o $@ $<

build/test_core: tests/unit/test_core.c $(CORE) build/cJSON.o $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE) build/cJSON.o

build/vucheck: tests/unit/vucheck.c $(CORE) build/cJSON.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/unit/vucheck.c $(CORE) build/cJSON.o

test: build/test_core
	./build/test_core

clean:
	rm -rf build
.PHONY: all test clean
