# Builds sleeve's command-line tool and checks, and runs them.
#   make            the tool and the check program, into BUILD
#   make check      every check (the SVG check needs rsvg-convert, and is skipped without it)
#   make faults     each check against a faulty variant of sleeve.h, which it must fail
CC ?= cc
BUILD ?= build
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
LDLIBS ?= -lm

all: $(BUILD)/sleeve $(BUILD)/check

$(BUILD):
	mkdir -p $@

$(BUILD)/sleeve: tools/sleeve.c sleeve.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/sleeve.c $(LDLIBS)

$(BUILD)/check: tests/check.c sleeve.h | $(BUILD)
	$(CC) $(CFLAGS) -I. -isystem third_party/stb -o $@ tests/check.c $(LDLIBS)

check: $(BUILD)/check
	$(BUILD)/check --scratch $(BUILD)

faults: | $(BUILD)
	CC="$(CC)" CFLAGS="$(CFLAGS)" tests/faulty-variants.sh $(BUILD)/faults

.PHONY: all check faults
