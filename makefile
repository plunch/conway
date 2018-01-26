PKGCONFIG_LIBS=sdl2
CFLAGS+=-g -MMD -I./src/ $(shell pkg-config --cflags $(PKGCONFIG_LIBS))
LDLIBS=$(shell pkg-config --libs $(PKGCONFIG_LIBS))
LDFLAGS=

SOURCES=src/conway.c \
        src/draw.c \
        src/load.c \
        src/main.c
OBJS=$(SOURCES:.c=.o)
DEPS=$(OBJS:.o=.d)

.PHONY: all clean
all: conway


conway: $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LDLIBS)

clean:
	$(RM) conway
	$(RM) $(OBJS)
	$(RM) $(DEPS)

-include $(DEPS)
