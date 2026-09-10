INSTALL_BIN := qmap
all := libqmap qmap test test_extended test_multivalue test_record rec_test bench_multivalue bench_rec

LDLIBS-libqmap := -lxxhash -lqsys
LDLIBS-libqmap-Windows := -lmman
install-dep-dlls-Windows := libmman.dll
LDLIBS-bench_multivalue := -lqmap
LDLIBS-bench_rec := -lqmap
LDLIBS-test := -lqmap
LDLIBS-test_extended := -lqmap
LDLIBS-test_multivalue := -lqmap
LDLIBS-test_record := -lqmap
LDLIBS-rec_test := -lqmap
LDLIBS-qmap := -lqmap
LDLIBS-save_test := -lqmap

libqmap-obj-y := src/idm.o src/rec.o

CFLAGS += -g

include ../mk/include.mk

test: all
	./test.sh

bench: all
	LD_LIBRARY_PATH=./lib ./bin/bench_multivalue
