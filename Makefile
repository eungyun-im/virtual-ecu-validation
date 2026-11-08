# Build, test and analysis entry points. CI runs the same targets (.github/workflows/ci.yml).
CC ?= gcc
export PYTHONNOUSERSITE := 1
export PYTHONPATH := tools
CFLAGS_COMMON := -std=c99 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Iecu/include
CORE_SRC := ecu/src/e2e.c ecu/src/dtc.c ecu/src/sha256.c ecu/src/isotp.c ecu/src/swupdate.c ecu/src/uds.c ecu/src/ecu.c
APP_SRC := ecu/app/main.c ecu/app/bus.c
UNIT_TESTS := $(sort $(basename $(notdir $(wildcard ecu/tests/test_*.c))))
B := build
# unit tests use GNU statement expressions in their helpers; product code stays strict C99
UNIT_CFLAGS := -std=gnu99 -Wall -Wextra -Werror -Wno-unused-function -Iecu/include

.PHONY: all release asan coverage unit unit-asan systest systest-asan analyze fuzz clean report rtm

all: release

$(B)/release/vecu: $(CORE_SRC) $(APP_SRC) $(wildcard ecu/include/vecu/*.h ecu/app/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -O2 -D_GNU_SOURCE $(CORE_SRC) $(APP_SRC) -o $@

$(B)/asan/vecu: $(CORE_SRC) $(APP_SRC) $(wildcard ecu/include/vecu/*.h ecu/app/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS_COMMON) -O1 -g -D_GNU_SOURCE -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer $(CORE_SRC) $(APP_SRC) -o $@

$(B)/cov/vecu: $(CORE_SRC) $(APP_SRC) $(wildcard ecu/include/vecu/*.h ecu/app/*.h)
	@mkdir -p $(dir $@)
	cd $(B)/cov && $(CC) $(CFLAGS_COMMON:-Iecu/include=-I../../ecu/include) -O0 -g --coverage -D_GNU_SOURCE $(addprefix ../../,$(CORE_SRC) $(APP_SRC)) -o vecu

release: $(B)/release/vecu
asan: $(B)/asan/vecu

# C unit tests on the simulated clock, built with ASan and UBSan
unit:
	@mkdir -p $(B)/unit
	@set -e; for t in $(UNIT_TESTS); do \
	  $(CC) $(UNIT_CFLAGS) -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
	    -Iecu/tests ecu/tests/$$t.c $(CORE_SRC) -o $(B)/unit/$$t; \
	  ./$(B)/unit/$$t --junit $(B)/unit/$$t.xml; done

# Unit tests with coverage instrumentation (one build folder per test, so .gcda files do not collide)
unit-cov:
	@set -e; for t in $(UNIT_TESTS); do mkdir -p $(B)/cov/unit/$$t; \
	  ( cd $(B)/cov/unit/$$t && $(CC) $(UNIT_CFLAGS:-Iecu/include=-I../../../../ecu/include) -O0 -g --coverage \
	    -I../../../../ecu/tests ../../../../ecu/tests/$$t.c $(addprefix ../../../../,$(CORE_SRC)) -o t && ./t > /dev/null ); done

systest: release
	.venv/bin/python -m pytest tests -m "not slow" --vecu $(B)/release/vecu

systest-asan: asan
	.venv/bin/python -m pytest tests --vecu $(B)/asan/vecu

coverage: unit-cov $(B)/cov/vecu
	.venv/bin/python -m pytest tests -q --vecu $(B)/cov/vecu
	tools/coverage.sh

analyze:
	.venv/bin/python tools/static_analysis.py

fuzz:
	tools/fuzz.sh

rtm:
	.venv/bin/python tools/rtm.py

report:
	.venv/bin/python tools/test_report.py

clean:
	rm -rf $(B)
