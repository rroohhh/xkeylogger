.PHONY: dynamic static clean

CFLAGS = -Wall -Wextra -pedantic -O3
LDFLAGS = -s

dynamic: LDLIBS += -lX11 -lXi
dynamic: xkeylogger

static: LDLIBS += -Wl,-Bstatic -lX11 -lXi -lxcb -lXau -lXdmcp -lXext -Wl,-Bdynamic
static: xkeylogger

clean:
	$(RM) xkeylogger
