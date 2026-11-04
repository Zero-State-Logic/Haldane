# HALDANE host build for Linux/macOS (Windows: python tools/run_all.py uses MSVC).
#   make -C host          build C tests + shared library into ../build
#   make -C host test     run C tests and Python tests
CC     ?= cc
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
ROOT   := ..
BUILD  := $(ROOT)/build
CORE   := $(wildcard $(ROOT)/firmware/core/*.c)
CTEST  := $(wildcard ctest/*.c)
UNAME  := $(shell uname)
LIB    := $(BUILD)/libhaldane_core.$(if $(filter Darwin,$(UNAME)),dylib,so)

all: $(BUILD)/haldane_tests $(LIB)

$(BUILD)/haldane_tests: $(CTEST) $(CORE)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/firmware/core -o $@ $^ -lm

$(LIB): capi/haldane_api.c $(CORE)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -shared -fPIC -fvisibility=hidden -I$(ROOT)/firmware/core -o $@ $^

test: all
	$(BUILD)/haldane_tests
	python3 -m unittest discover -s tests -v

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
