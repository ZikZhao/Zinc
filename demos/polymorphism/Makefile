CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic

CPP_SRC := main.cpp
ZINC_SRC := main.zn.cpp
CPP_TARGET := main_cpp
ZINC_TARGET := main_zinc

SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)

.PHONY: all run_cpp run_zinc benchmark_cpp benchmark_zinc clean

all: $(CPP_TARGET) $(ZINC_TARGET)

$(CPP_TARGET): $(CPP_SRC)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

$(ZINC_TARGET): $(ZINC_SRC)
	$(CXX) $(CXXFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)

run_cpp: $(CPP_TARGET)
	./$(CPP_TARGET)

run_zinc: $(ZINC_TARGET)
	./$(ZINC_TARGET)

benchmark_cpp: $(CPP_TARGET)
	./$(CPP_TARGET) --benchmark 10000

benchmark_zinc: $(ZINC_TARGET)
	./$(ZINC_TARGET) --benchmark 10000

clean:
	rm -f $(CPP_TARGET) $(ZINC_TARGET)
