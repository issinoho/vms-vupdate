# Linux build of the core with its tests. The VMS build is build_vms.com.
CC ?= cc
CFLAGS = -std=c99 -pedantic -Wall -Wextra -Werror -g -Isrc/core -Isrc/third/cjson
# cJSON is third-party: built without -Werror/-pedantic, as upstream intends.
CJSON_CFLAGS = -std=c99 -g -Isrc/third/cjson
CORE = src/core/vu_ver.c src/core/vu_pcsi.c src/core/vu_cat.c src/core/vu_plan.c \
       src/core/vu_auth.c src/core/vu_report.c
HDRS = $(wildcard src/core/*.h src/third/cjson/*.h)

all: test build/vucheck syntax

build/cJSON.o: src/third/cjson/cJSON.c src/third/cjson/cJSON.h
	@mkdir -p build
	$(CC) $(CJSON_CFLAGS) -c -o $@ $<

build/test_core: tests/unit/test_core.c $(CORE) build/cJSON.o $(HDRS)
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ tests/unit/test_core.c $(CORE) build/cJSON.o

build/vucheck: tests/unit/vucheck.c $(CORE) build/cJSON.o $(HDRS)
	$(CC) $(CFLAGS) -o $@ tests/unit/vucheck.c $(CORE) build/cJSON.o

# The store codec needs OpenSSL (libssl-dev); the VMS build uses SSL3$LIBCRYPTO.
build/test_store: tests/unit/test_store.c src/app/vu_store.c src/core/vu_auth.c build/cJSON.o $(HDRS) src/app/vu_store.h
	$(CC) $(CFLAGS) -Isrc/app -o $@ tests/unit/test_store.c src/app/vu_store.c src/core/vu_auth.c build/cJSON.o -lcrypto

test: build/test_core build/test_store
	./build/test_core
	./build/test_store

# vu_portal.c compiles on Linux too (vu_http.c and the VMS main need curl/VMS headers).
syntax:
	$(CC) $(CFLAGS) -Isrc/app -fsyntax-only src/app/vu_portal.c

clean:
	rm -rf build
.PHONY: all test clean syntax
