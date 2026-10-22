# Integration tests: the ffemu programs, built in $(O)/out, driven by
# scripts from Python's unittest, the standard library alone.
#
# Usage: make -C tests/integration [O=<path>] [run | all]
#
# run, the default, runs every test. all runs them only when the programs or
# the tests have changed since they last passed, so that a build that leaves
# them alone costs nothing and a failing test stops the build.

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../..)
TESTS := $(ROOT)/tests/integration

O ?= $(ROOT)
OUT := $(abspath $(O))/out

ifndef PYTHON
PYTHON := $(firstword $(foreach p,python3 python py,$(if \
	    $(shell command -v $(p) 2>/dev/null),$(p))))
endif
ifeq ($(strip $(PYTHON)),)
$(error No python3, python or py found: give PYTHON=<command>)
endif

UNITTEST := PYTHONDONTWRITEBYTECODE=1 FFEMU_OUT=$(OUT) $(PYTHON) -m unittest \
    discover -s $(TESTS) -t $(TESTS)

.PHONY: run all

run:
	$(UNITTEST) -v

all: $(OUT)/integration-tests/passed

# Every build links the programs anew, with build_info.c compiled again for
# its time: their other objects tell whether they changed. The tests read the
# 7-segment display driver's letters and digits too.
PROGRAM_OBJS := $(shell find $(OUT)/ffemu $(OUT)/ffemu-apple2 -name '*.o' \
    ! -name build_info.o 2>/dev/null)

$(OUT)/integration-tests/passed: $(PROGRAM_OBJS) $(wildcard $(TESTS)/*.py) \
    $(ROOT)/src/display/led_7seg.c
	$(UNITTEST)
	@mkdir -p $(dir $@)
	@touch $@
