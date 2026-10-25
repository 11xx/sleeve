# Builds sleeve's command-line tool and checks, and runs them.
#   make               the tool and the check program, into BUILD
#   make check         every check (the SVG check needs rsvg-convert, and is skipped without
#                      it), that the tool and the checks rebuild when tools/styles.h does, and
#                      that sleeve.h, CHANGELOG.md and sleeve --version name one release
#   make faults        each check against a faulty variant of sleeve.h, which it must fail
#   make sanitize      every check built by Clang with ASan and UBSan, within SANITIZE_TIME
#   make sanitize-faults  each fault that is undefined behaviour, caught by UBSan in its check
#   make fuzz          tests/fuzz.c under libFuzzer, ASan and UBSan for FUZZ_TIME seconds,
#                      its corpus in FUZZ_CORPUS
#   make freestanding  the whole library for wasm32 with no C library: a module that imports
#                      nothing
CC ?= cc
CLANG ?= clang
BUILD ?= build
CFLAGS ?= -std=c99 -O2 -Wall -Wextra -Werror
LDLIBS ?= -lm
SANITIZERS = -fsanitize=address,undefined,float-cast-overflow -fno-sanitize-recover=all
SANITIZE_CFLAGS ?= -std=c99 -O1 -g -ffp-contract=off -Wall -Wextra -Werror $(SANITIZERS)
SANITIZE_TIME ?= 600
FUZZ_TIME ?= 60
FUZZ_CORPUS ?= $(BUILD)/fuzz-corpus

all: $(BUILD)/sleeve $(BUILD)/check

$(BUILD):
	mkdir -p $@

$(BUILD)/sleeve: tools/sleeve.c tools/styles.h sleeve.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tools/sleeve.c $(LDLIBS)

$(BUILD)/check: tests/check.c tools/styles.h sleeve.h | $(BUILD)
	$(CC) $(CFLAGS) -I. -isystem third_party/stb -o $@ tests/check.c $(LDLIBS)

check: $(BUILD)/check $(BUILD)/sleeve
	$(BUILD)/check --scratch $(BUILD)
	@for target in $(BUILD)/sleeve $(BUILD)/check; do \
	  if ! $(MAKE) -f $(firstword $(MAKEFILE_LIST)) --no-print-directory -n -W tools/styles.h \
	         $$target | grep -qF -- "-o $$target"; then \
	    echo "FAIL makefile: $$target is not rebuilt when tools/styles.h changes"; exit 1; \
	  fi; \
	done; \
	echo "pass makefile: the tool and the checks are rebuilt when tools/styles.h changes"
	@version=$$(sed -n 's/^#define SLEEVE_VERSION "\(.*\)"$$/\1/p' sleeve.h); \
	heading=$$(sed -n 's/^## \([0-9][0-9.]*\).*/\1/p' CHANGELOG.md | head -n 1); \
	said=$$($(BUILD)/sleeve --version); \
	if [ -z "$$version" ] || [ "$$heading" != "$$version" ] || [ "$$said" != "sleeve $$version" ]; then \
	  echo "FAIL version: sleeve.h names '$$version', CHANGELOG.md's newest heading '$$heading', sleeve --version '$$said'"; exit 1; \
	fi; \
	echo "pass version: sleeve.h, CHANGELOG.md's newest heading and sleeve --version name $$version"

faults: | $(BUILD)
	CC="$(CC)" CFLAGS="$(CFLAGS)" tests/faulty-variants.sh $(BUILD)/faults

$(BUILD)/sanitize/check: tests/check.c tools/styles.h sleeve.h
	mkdir -p $(@D)
	$(CLANG) $(SANITIZE_CFLAGS) -I. -isystem third_party/stb -o $@ tests/check.c $(LDLIBS)

sanitize: $(BUILD)/sanitize/check
	timeout $(SANITIZE_TIME) $(BUILD)/sanitize/check --scratch $(BUILD)/sanitize

sanitize-faults: | $(BUILD)
	CC="$(CLANG)" CFLAGS="$(SANITIZE_CFLAGS)" SLEEVE_FAULTS=ub \
	  tests/faulty-variants.sh $(BUILD)/sanitize-faults

$(BUILD)/fuzz: tests/fuzz.c sleeve.h | $(BUILD)
	$(CLANG) $(SANITIZE_CFLAGS) -fsanitize=fuzzer -I. -o $@ tests/fuzz.c $(LDLIBS)

fuzz: $(BUILD)/fuzz
	mkdir -p $(FUZZ_CORPUS)
	$(BUILD)/fuzz $(FUZZ_CORPUS) -max_len=512 -timeout=10 -max_total_time=$(FUZZ_TIME) \
	  -print_final_stats=1 -artifact_prefix=$(BUILD)/fuzz-

$(BUILD)/freestanding.wasm: tests/freestanding.c sleeve.h | $(BUILD)
	$(CLANG) --target=wasm32 -std=c99 -O2 -nostdlib -Wall -Wextra -Werror -I. \
	  -Wl,--no-entry -Wl,--export-all -Wl,--export-memory -o $@ tests/freestanding.c

freestanding: $(BUILD)/freestanding.wasm
	python3 -I tests/wasm-imports.py $<

.PHONY: all check faults sanitize sanitize-faults fuzz freestanding
