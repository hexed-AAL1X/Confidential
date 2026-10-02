CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
SDL      := $(shell pkg-config --cflags --libs sdl3)
MIXER    := $(shell pkg-config --cflags --libs sdl3-mixer)

all: confidential sprite_editor

confidential: src/Menu.cpp src/Game.cpp src/Game.hpp
	$(CXX) $(CXXFLAGS) src/Menu.cpp src/Game.cpp -o $@ $(MIXER) $(SDL)

sprite_editor: editor/SpriteEditor.cpp
	$(CXX) $(CXXFLAGS) editor/SpriteEditor.cpp -o $@ $(SDL)

run: confidential
	./confidential

edit: sprite_editor
	./sprite_editor

clean:
	rm -f confidential sprite_editor

.PHONY: all run edit clean
