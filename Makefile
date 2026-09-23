# Thin wrapper around CMake. `make` builds ./sched; see docs/DEVELOPING.md for details.
CMAKE ?= cmake
CTEST ?= ctest
JOBS  ?= 4

# gcov driver matching the compiler that produced the coverage data.
ifeq ($(shell uname),Darwin)
GCOV ?= xcrun llvm-cov gcov
else ifneq ($(findstring clang,$(shell $(CC) --version 2>/dev/null)),)
GCOV ?= llvm-cov gcov
else
GCOV ?= gcov
endif

.PHONY: all test asan coverage experiments wasm wasm-test format format-check clean

all:
	$(CMAKE) -S . -B build -DCMAKE_BUILD_TYPE=Release
	$(CMAKE) --build build -j $(JOBS)
	cp build/sched ./sched

test: all
	$(CTEST) --test-dir build --output-on-failure -j $(JOBS)

asan:
	$(CMAKE) -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DSCHED_SANITIZE=ON
	$(CMAKE) --build build-asan -j $(JOBS)
	$(CTEST) --test-dir build-asan --output-on-failure -j $(JOBS)

coverage:
	$(CMAKE) -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DSCHED_COVERAGE=ON
	$(CMAKE) --build build-cov -j $(JOBS)
	find build-cov -name '*.gcda' -delete
	$(CTEST) --test-dir build-cov --output-on-failure -j $(JOBS)
	gcovr --root . --filter 'src/' --gcov-executable "$(GCOV)" --print-summary \
	      --xml build-cov/coverage.xml --html-details build-cov/coverage.html \
	      --fail-under-line 90 build-cov

# Regenerates docs/results/ (plots, CSV tables, takeaways, benchmarks).
# Needs Python 3 with matplotlib.
experiments: all
	SCHED_BIN=$(CURDIR)/build/sched python3 experiments/run_all.py

# Browser build (needs Emscripten's emcc on PATH): the CLI compiled to
# WebAssembly plus the static page, in build-wasm/site/.
EMCC ?= emcc
WASM_FLAGS = -std=c11 -O2 -ffp-contract=off -Wall -Wextra -Wpedantic -Werror -Iinclude -Isrc \
	-sMODULARIZE=1 -sEXPORT_NAME=createSched -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 \
	-sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sEXPORTED_RUNTIME_METHODS=callMain,FS \
	-sENVIRONMENT=web,node -sEXPORT_ES6=1
wasm:
	mkdir -p build-wasm/site
	$(EMCC) $(WASM_FLAGS) $$(ls src/*/*.c) -o build-wasm/site/sched.js
	cp web/index.html web/style.css web/app.js web/presets.js build-wasm/site/

# The WebAssembly build must print exactly what the native binary prints.
wasm-test: all wasm
	node tests/wasm_smoke.mjs build-wasm/site/sched.js build/sched

format:
	clang-format -i $$(git ls-files '*.c' '*.h')

format-check:
	clang-format --dry-run -Werror $$(git ls-files '*.c' '*.h')

clean:
	rm -rf build build-asan build-cov build-wasm sched
