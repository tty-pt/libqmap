all := libqmap test

LDLIBS-libqmap := -lxxhash -lqsys
LDLIBS-test := -lqmap

CFLAGS += -g

include ../mk/include.mk
