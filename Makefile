CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
LDLIBS    = -lncurses
EMCC     ?= emcc

SRC = src/game.cpp
HDR = src/game.h

# Native terminal build (the default).
keyboardtd: $(SRC) src/platform_ncurses.cpp $(HDR)
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) src/platform_ncurses.cpp $(LDLIBS)

run: keyboardtd
	./keyboardtd

# Browser build — needs the Emscripten SDK (emcc) on PATH, or just use
# `docker build .` which compiles it inside the emsdk image.
web: $(SRC) src/platform_web.cpp $(HDR) web/index.html
	mkdir -p dist/web
	$(EMCC) -std=c++17 -O2 $(SRC) src/platform_web.cpp \
	  -sEXPORTED_RUNTIME_METHODS=ccall \
	  -sEXPORTED_FUNCTIONS=_main,_web_key,_web_resize \
	  -o dist/web/keyboardtd.js
	cp web/index.html dist/web/index.html

clean:
	rm -rf keyboardtd dist

.PHONY: run web clean
