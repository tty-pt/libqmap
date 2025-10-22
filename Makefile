INSTALL_BIN := qmap
all := libqmap qmap test

LDLIBS-libqmap := -lxxhash -lqsys
LDLIBS-test := -lqmap
LDLIBS-qmap := -lqmap
LDLIBS-save_test := -lqmap

CFLAGS += -g

include ../mk/include.mk

test: all
	./test.sh
