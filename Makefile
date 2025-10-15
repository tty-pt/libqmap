LIB-LDLIBS := -lxxhash -lqsys
LIB := qmap
BIN := test
HEADERS := ttypt/idm.h ttypt/queue.h
CFLAGS += -g

-include ../mk/include.mk
