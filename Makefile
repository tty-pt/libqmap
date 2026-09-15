INSTALL_BIN := qmap
all := libqmap qmap test test_extended test_multivalue test_record rec_test rec_axis_test rec_axis_store_test rec_axis_bench bench_multivalue bench_rec

LDLIBS-libqmap := -lxxhash -lqsys
LDLIBS-libqmap-Windows := -lmman
install-dep-dlls-Windows := libmman.dll
LDLIBS-bench_multivalue := -lqmap
LDLIBS-bench_rec := -lqmap
LDLIBS-rec_axis_bench := -lqmap
LDLIBS-test := -lqmap
LDLIBS-test_extended := -lqmap
LDLIBS-test_multivalue := -lqmap
LDLIBS-test_record := -lqmap
LDLIBS-rec_test := -lqmap
LDLIBS-rec_axis_test := -lqmap
LDLIBS-rec_axis_store_test := -lqmap -lqsys
LDLIBS-qmap := -lqmap -lqsys
LDLIBS-save_test := -lqmap
LDLIBS-librec_axis_mock := -lqmap
LDLIBS-librec_axis_fold := -lqmap

libqmap-obj-y := src/idm.o src/rec.o src/rec_axis.o

CFLAGS += -g

include ../mk/include.mk

# Mock rec_axis plugin (PLAN-REC-QUERY.md §4.4, Part 3.4), test-only: built
# via an explicit standalone rule rather than the ${all}-driven LIB list
# above, since ../mk/include.mk's LIB-obj-y aggregation ($(LIB:%=%-obj-y)
# followed by a $($(...)) double-dereference) only works correctly for a
# single "lib*" target per Makefile — a second one silently drops
# libqmap's own extra objects (idm.o/rec.o/rec_axis.o) off the link line.
# Never linked into qmap/libqmap; dlopen'd only by test-cli.sh.
lib/librec_axis_mock.${SO}: src/librec_axis_mock.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_mock.c ${LDFLAGS} ${LDLIBS-librec_axis_mock}

# Single-axis stub plugins (2B-1 roster/load tests): one axis each, the
# plugin file named lib<axis>.so ("stub", "zed") so by-name dlopen
# (`@stub` → $QMAP_AXIS_PATH/libstub.so) resolves them. Same standalone-rule
# pattern as the mock (kept out of the LIB list — see note above).
lib/libstub.${SO}: src/librec_axis_stub.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_stub.c ${LDFLAGS} ${LDLIBS-librec_axis_mock}

lib/libzed.${SO}: src/librec_axis_zed.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_zed.c ${LDFLAGS} ${LDLIBS-librec_axis_mock}

# Functional 2B-3 test axes (alpha/beta/pure — mm-plan/2B-3-IMPLEMENTATION.md
# §5). Same multi-axis-one-so + standalone-rule pattern as the mock.
lib/librec_axis_fold.${SO}: src/librec_axis_fold.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_fold.c ${LDFLAGS} ${LDLIBS-librec_axis_fold}

# Minimal string-only 2B-4 test axis ("plain" — mm-plan/2B-4-
# IMPLEMENTATION.md §5): exports rec_axis_store but NO
# rec_axis_store_typed, so the CLI's typed dispatch must fall back to the
# string symbol on binary primaries. Same standalone-rule pattern.
lib/librec_axis_plain.${SO}: src/librec_axis_plain.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_plain.c ${LDFLAGS} ${LDLIBS-librec_axis_fold}

# L1 short-circuit probe (AXIS-EFF plan / test-shortcircuit.sh): one-axis
# plugin whose decode emits a stderr marker for "boom" and whose fill
# empties on "empty" — the black-box observation point for empty-branch
# skipping. Same standalone-rule pattern as the mock/plain plugins.
lib/librec_axis_probe.${SO}: src/librec_axis_probe.c lib/libqmap.${SO} lib
	${cc} ${CFLAGS} ${CFLAGS-LIB} -shared -o $@ src/librec_axis_probe.c ${LDFLAGS} ${LDLIBS-librec_axis_fold}

all: lib/librec_axis_mock.${SO} lib/libstub.${SO} lib/libzed.${SO} lib/librec_axis_fold.${SO} lib/librec_axis_plain.${SO} lib/librec_axis_probe.${SO}

test: all
	./test.sh
	./test-cli.sh
	./test-roster.sh
	./test-fanout.sh
	./test-real.sh
	./test-mm.sh
	./test-shortcircuit.sh

bench: all
	LD_LIBRARY_PATH=./lib ./bin/bench_multivalue
