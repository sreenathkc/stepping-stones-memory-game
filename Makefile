# Stepping Stones — a memory path game for H700 Anbernic handhelds (and other PortMaster
# devices), plus a one-file web version.
#
#   make stones       Mac/Linux desktop build for testing (needs SDL2)
#   make device       aarch64 cross build (needs a cross compiler and the device's libSDL2 in device-libs/)
#   make portmaster   public PortMaster package: dist/portmaster/ (repo layout) + dist/steppingstones.zip
#   make web          one self-contained web page: docs/index.html (needs Emscripten)
#   make package      personal build: like the PortMaster zip's game, plus anything in personal/
#                     (e.g. a welcome.wav greeting, owner.txt "made for" name), with a plain launcher
#   make deploy       install the personal build on the handheld over SSH
SRC := $(wildcard src/*.c)
HDR := $(wildcard src/*.h)
SDL2_CONFIG := $(shell command -v sdl2-config || echo /opt/homebrew/opt/sdl2-compat/bin/sdl2-config)

CROSS_CC ?= aarch64-unknown-linux-gnu-gcc
DEV_CFLAGS := -I/opt/homebrew/include/SDL2 -D_REENTRANT -O2 -Wall -Wextra -std=gnu11
DEV_LIBS := -L device-libs -lSDL2 -lm -Wl,--allow-shlib-undefined

DEVICE ?= root@169.254.170.2
PORTS ?= /storage/roms/ports

stones: $(SRC) $(HDR)                       # desktop build for testing
	$(CC) $(shell $(SDL2_CONFIG) --cflags) -O2 -Wall -Wextra -std=c11 -o $@ $(SRC) $(shell $(SDL2_CONFIG) --libs) -lm

build/stones-arm64: $(SRC) $(HDR)
	@mkdir -p build
	$(CROSS_CC) $(DEV_CFLAGS) -o $@ $(SRC) $(DEV_LIBS)

device: build/stones-arm64

# Public PortMaster package. dist/portmaster/steppingstones/ is laid out like a port folder in
# the PortMaster-New repo; dist/steppingstones.zip is what testers unzip into their ports folder.
portmaster: build/stones-arm64
	rm -rf dist/portmaster dist/steppingstones.zip && mkdir -p dist/portmaster/steppingstones/steppingstones
	cp portmaster/port.json portmaster/gameinfo.xml portmaster/README.md "portmaster/Stepping Stones.sh" dist/portmaster/steppingstones/
	cp cover.png dist/portmaster/steppingstones/screenshot.png
	cp cover.png dist/portmaster/steppingstones/cover.png
	G=dist/portmaster/steppingstones/steppingstones; \
	  cp build/stones-arm64 $$G/stones.aarch64 && cp -R assets $$G/assets && cp -R licenses $$G/licenses && \
	  cp portmaster/port.json portmaster/gameinfo.xml $$G/ && cp cover.png $$G/screenshot.png
	cd dist/portmaster/steppingstones && zip -qr ../../steppingstones.zip "Stepping Stones.sh" steppingstones
	@ls -la dist/steppingstones.zip

# One self-contained web page (code, font and all) for any static host
web: $(SRC) $(HDR) web/shell.html
	@mkdir -p docs
	emcc $(SRC) -O2 -sUSE_SDL=2 -sSINGLE_FILE=1 -sALLOW_MEMORY_GROWTH=1 \
	  -sEXPORTED_FUNCTIONS=_main,_web_button '-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=$$UTF8ToString' \
	  --embed-file assets/Fredoka-SemiBold.ttf@/assets/Fredoka-SemiBold.ttf \
	  --shell-file web/shell.html -o docs/index.html
	@ls -la docs/index.html

# Personal build (the one on our handheld): plain launcher, plus personal/ extras
package: build/stones-arm64
	rm -rf build/pkg dist/SteppingStones.zip && mkdir -p build/pkg/steppingstones dist
	cp build/stones-arm64 build/pkg/steppingstones/stones
	cp -R assets build/pkg/steppingstones/assets
	-cp personal/* build/pkg/steppingstones/assets/ 2>/dev/null
	cp cover.png build/pkg/steppingstones/cover.png
	cp launcher.sh "build/pkg/Stepping Stones.sh"
	cp README-install.txt build/pkg/steppingstones/
	cd build/pkg && zip -qr ../../dist/SteppingStones.zip "Stepping Stones.sh" steppingstones
	@ls -la dist/SteppingStones.zip

deploy: package
	ssh $(DEVICE) 'killall stones 2>/dev/null; sleep 1; true'
	scp dist/SteppingStones.zip $(DEVICE):/tmp/SteppingStones.zip
	ssh $(DEVICE) 'cd $(PORTS) && unzip -oq /tmp/SteppingStones.zip && rm /tmp/SteppingStones.zip && chmod +x "Stepping Stones.sh" steppingstones/stones'

clean:
	rm -rf stones build dist

.PHONY: device portmaster web package deploy clean
