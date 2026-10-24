# Test tools for the OpenJazz GBA port (see README.md). Run make here.

HERE := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SRC := $(HERE)../../src
BUILD := $(HERE)build

# mGBA source tree for the headers; the library comes from the system
MGBA_VERSION := 0.10.2
LIBMGBA ?= $(firstword $(wildcard /usr/lib/*/libmgba.so.0.10 /usr/lib/libmgba.so.0.10 /usr/lib64/libmgba.so.0.10))

all: $(BUILD)/ojgbarun $(BUILD)/detshim.so $(BUILD)/blittest $(BUILD)/scaletest

$(BUILD)/mgba/include/mgba/core/core.h:
	rm -rf $(BUILD)/mgba
	git clone -q --depth 1 --branch $(MGBA_VERSION) https://github.com/mgba-emu/mgba.git $(BUILD)/mgba

$(BUILD)/inc/mgba/flags.h: $(HERE)runner/mgba-flags.h
	mkdir -p $(@D)
	cp $< $@

$(BUILD)/ojgbarun: $(HERE)runner/ojgbarun.c $(BUILD)/inc/mgba/flags.h $(BUILD)/mgba/include/mgba/core/core.h
	@test -n "$(LIBMGBA)" || { echo "libmgba.so.0.10 not found; set LIBMGBA"; exit 1; }
	gcc -O1 -o $@ $< -I$(BUILD)/inc -I$(BUILD)/mgba/include $(LIBMGBA) -lz

$(BUILD)/detshim.so: $(HERE)detshim/detshim.c
	mkdir -p $(@D)
	gcc -O2 -shared -fPIC -o $@ $< -ldl

$(BUILD)/%: $(HERE)tests/%.cpp $(SRC)/platforms/gba/blit_gba.iwram.cpp $(SRC)/platforms/gba/blit_gba.h
	mkdir -p $(@D)
	g++ -O2 -I$(SRC)/platforms/gba -o $@ $< $(SRC)/platforms/gba/blit_gba.iwram.cpp

test: $(BUILD)/blittest $(BUILD)/scaletest
	$(BUILD)/blittest
	$(BUILD)/scaletest

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
