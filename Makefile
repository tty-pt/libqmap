INSTALL_BIN := qmap
all := libqmap qmap test

LDLIBS-libqmap := -lxxhash -lqsys
LDLIBS-libqmap-Windows := -lmman
LDLIBS-test := -lqmap
LDLIBS-qmap := -lqmap
LDLIBS-save_test := -lqmap

libqmap-obj-y := src/idm.o

CFLAGS += -g

include ../mk/include.mk

test: all
	./test.sh
