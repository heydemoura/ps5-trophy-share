# ps5-trophy-share
#   make                 the payload (needs ps5-payload-sdk)
#   make host            the same program for a PC, reading a fake console folder
#   make test            unit tests + a run against a local stand-in server
PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
VERSION ?= 0.3.0
# Where the payload sends trophies. Override to point a build at your own server.
API_URL ?= https://trophy-share-api.tcgalters.workers.dev

PS5_CC := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
HOST_CC ?= cc

BEARSSL := third_party/BearSSL
BR_SRCS := $(BEARSSL)/src/settings.c $(wildcard $(BEARSSL)/src/*/*.c)
SRCS := src/main.c src/app.c src/http.c src/icon.c src/json.c src/stb_impl.c src/t2pd.c src/ucp.c
HDRS := $(wildcard src/*.h)

DEFS := -DTS_VERSION='"$(VERSION)"' -DTS_API_URL='"$(API_URL)"' -DBUILD_TIME=$(shell date +%s)L
INCS := -Isrc -I$(BEARSSL)/inc -Ithird_party/stb
WARN := -Wall -Wextra -Wno-unused-parameter
BR_CFLAGS := -O2 -I$(BEARSSL)/inc -I$(BEARSSL)/src -w

# Third-party code is built without warnings.
build/ps5/src/stb_impl.o build/host/src/stb_impl.o: WARN := -w

PS5_OBJ := $(patsubst %.c,build/ps5/%.o,$(SRCS) src/platform_ps5.c)
PS5_BR := $(patsubst %.c,build/ps5/%.o,$(BR_SRCS))
HOST_OBJ := $(patsubst %.c,build/host/%.o,$(SRCS) src/platform_host.c)
HOST_BR := $(patsubst %.c,build/host/%.o,$(BR_SRCS))

all: ps5-trophy-share.elf ps5-trophy-share-stop.elf

$(BEARSSL)/inc/bearssl.h:
	git submodule update --init

ps5-trophy-share.elf: $(PS5_OBJ) $(PS5_BR)
	$(PS5_CC) -o $@ $^ -lSceNet -lSceUserService -lkernel_sys

ps5-trophy-share-stop.elf: src/stop.c src/app.h
	$(PS5_CC) -O2 $(WARN) -D_BSD_SOURCE -std=gnu11 -Isrc -o $@ src/stop.c

# The console's clock and random source are picked by hand: no /dev/urandom is assumed.
build/ps5/$(BEARSSL)/%.o: $(BEARSSL)/%.c | $(BEARSSL)/inc/bearssl.h
	@mkdir -p $(dir $@)
	$(PS5_CC) $(BR_CFLAGS) -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=1 -c -o $@ $<
build/ps5/src/%.o: src/%.c $(HDRS) | $(BEARSSL)/inc/bearssl.h
	@mkdir -p $(dir $@)
	$(PS5_CC) -O2 $(WARN) -D_BSD_SOURCE -std=gnu11 $(DEFS) $(INCS) -c -o $@ $<

host: build/trophyshare-host
build/trophyshare-host: $(HOST_OBJ) $(HOST_BR)
	$(HOST_CC) -o $@ $^ -lm
build/host/$(BEARSSL)/%.o: $(BEARSSL)/%.c | $(BEARSSL)/inc/bearssl.h
	@mkdir -p $(dir $@)
	$(HOST_CC) $(BR_CFLAGS) -c -o $@ $<
build/host/src/%.o: src/%.c $(HDRS) | $(BEARSSL)/inc/bearssl.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -O1 -g $(WARN) -std=gnu11 -D_GNU_SOURCE $(DEFS) $(INCS) -c -o $@ $<

build/unit: tests/unit.c src/t2pd.c src/ucp.c src/json.c src/icon.c build/host/src/stb_impl.o $(HDRS)
	@mkdir -p build
	$(HOST_CC) -O1 -g $(WARN) -std=gnu11 -D_GNU_SOURCE -fsanitize=address,undefined -Isrc -Ithird_party/stb -o $@ tests/unit.c src/t2pd.c src/ucp.c src/json.c src/icon.c build/host/src/stb_impl.o -lm

test: build/unit build/trophyshare-host
	python3 tests/make_fixture.py build/fakeroot
	build/unit build/fakeroot
	python3 tests/e2e.py

# Rebuild src/trust_anchors.h from certs/*.pem (needs BearSSL's brssl tool, see docs/CERTS.md).
anchors:
	$(BRSSL) ta certs/*.pem > src/trust_anchors.h

clean:
	rm -rf build ps5-trophy-share.elf ps5-trophy-share-stop.elf

.PHONY: all host test anchors clean
