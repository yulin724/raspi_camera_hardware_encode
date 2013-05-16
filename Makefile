
OBJS=encode.o v4l2uvc.o
BIN=hello_encode.bin
LDFLAGS+=-lilclient
CFLAGS+=-g


include ../Makefile.include
