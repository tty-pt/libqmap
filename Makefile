INSTALL_BIN := qmap
all := libqmap qmap test test_extended test_multivalue test_record rec_test rec_axis_test rec_axis_bench bench_multivalue bench_rec

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
LDLIBS-qmap := -lqmap -lqsys
LDLIBS-save_test := -lqmap
LDLIBS-librec_axis_mock := -lqmap

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

all: lib/librec_axis_mock.${SO}

test: all
	./test.sh
	./test-cli.sh

bench: all
	LD_LIBRARY_PATH=./lib ./bin/bench_multivalue
