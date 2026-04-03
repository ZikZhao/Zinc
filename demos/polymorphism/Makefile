CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := polymorphism_demo
SRC := main.cpp
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

.PHONY: all run benchmark clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

run: $(TARGET)
	./$(TARGET)

benchmark: $(TARGET)
	./$(TARGET) --benchmark 10000

clean:
	rm -f $(TARGET)
