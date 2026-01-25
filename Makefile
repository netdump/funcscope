
CC      := gcc
CFLAGS  := -O2 -g -Wall -Wextra -fno-omit-frame-pointer -lm
LDFLAGS := 

TARGETS := demo tool

.PHONY: all clean

all: $(TARGETS)

demo: demo.c funcscope.c funcscope.h
	$(CC) $(CFLAGS) -o $@ demo.c funcscope.c $(LDFLAGS)

tool: tool.c funcscope.c funcscope.h
	$(CC) $(CFLAGS) -o $@ tool.c funcscope.c $(LDFLAGS)

clean:
	rm -f $(TARGETS)


