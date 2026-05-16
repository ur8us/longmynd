BIN = longmynd
SRC = main.c nim.c ftdi.c stv0910.c stv0910_utils.c stv0903.c stvvglna.c stvvglna_utils.c stv6120.c stv6120_utils.c stb6100.c ftdi_usb.c fifo.c udp.c beep.c ts.c web/web.c
OBJ = ${SRC:.c=.o}
DEP := ${SRC:.c=.d}

ifndef CC
CC = gcc
endif
COPT = -O3 -march=native -mtune=native
CFLAGS += -Wall -Wextra -Wpedantic -Wunused -DVERSION=\"${VER}\" -pthread -D_GNU_SOURCE
LDFLAGS += -lusb-1.0 -lm -lasound -ljson-c
LDFLAGS += -Wl,-Bstatic -lwebsockets -Wl,-Bdynamic -lcap

LWS_DIR = ./web/libwebsockets/
LWS_LIBSDIR = ${LWS_DIR}/build/include
LWS_OBJDIR = ${LWS_DIR}/build/lib

all: check-gitsubmodules check-lws ${BIN} fake_read

debug: COPT = -Og
debug: CFLAGS += -ggdb -fno-omit-frame-pointer
debug: all

werror: CFLAGS += -Werror
werror: all

fake_read:
	@echo "  CC     "$@
	@${CC} fake_read.c -o $@

$(BIN): ${OBJ}
	@echo "  LD     "$@
	@${CC} ${COPT} ${CFLAGS} -o $@ ${OBJ} -L $(LWS_OBJDIR) ${LDFLAGS}

%.o: %.c
	@echo "  CC     "$<
	@${CC} ${COPT} ${CFLAGS} -I $(LWS_LIBSDIR) -MMD -MP -c -fPIC -o $@ $<

-include $(DEP)

clean:
	@rm -rf ${BIN} fake_read ${OBJ} ${DEP}

check-gitsubmodules:
	@if git submodule status | egrep -q '^[-]|^[+]' ; then \
		echo "INFO: Need to [re]initialize git submodules"; \
		git submodule update --init; \
	fi

check-lws:
	@if [ ! -f "${LWS_OBJDIR}/libwebsockets.a" ]; then \
		echo "INFO: Need to compile libwebsockets"; \
		mkdir -p ${LWS_DIR}/build/; \
		cd ${LWS_DIR}/build/; \
		cmake ../ -DLWS_WITH_SSL=off \
		          -DLWS_WITH_SHARED=off \
		          -DLWS_WITHOUT_CLIENT=on \
		          -DLWS_WITHOUT_TESTAPPS=on; \
		make; \
	fi

tags:
	@ctags *

.PHONY: all clean check-gitsubmodules check-lws tags
