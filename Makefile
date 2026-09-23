# Thin wrapper around CMake. `make` builds ./sched; see CLAUDE.md for details.
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

.PHONY: all test asan coverage experiments format format-check clean

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

format:
	clang-format -i $$(git ls-files '*.c' '*.h')

format-check:
	clang-format --dry-run -Werror $$(git ls-files '*.c' '*.h')

clean:
	rm -rf build build-asan build-cov sched
