# Rasterizer demo Makefile

CC := gcc
TARGET := main
SRC := main.zn.cpp
BUILD_DIR := build
OBJ := $(BUILD_DIR)/main.zn.o

SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL2_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)

ifeq ($(strip $(SDL2_CFLAGS) $(SDL2_LIBS)),)
SDL2_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL2_LIBS := $(shell sdl2-config --libs 2>/dev/null)
endif

GLM_CFLAGS := -I./libs/glm-0.9.7.2

CFLAGS += -O3 -march=native -mtune=native -flto=auto \
          -Wall -Wextra -Wcast-align -Wfatal-errors -Werror=return-type \
          -Wno-unused-parameter -Wno-unused-variable -Wno-ignored-attributes \
          -MMD -MP -funsigned-char -fpermissive
CXXFLAGS += -x c++ -std=gnu++23
CPPFLAGS += $(SDL2_CFLAGS) $(GLM_CFLAGS) -DGLM_FORCE_PURE
LDFLAGS += $(SDL2_LIBS) -static-libstdc++ -static-libgcc
LDLIBS += -lstdc++ -lm -pthread

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $^ -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/main.zn.o: $(SRC) | $(BUILD_DIR)
	$(CC) $(CXXFLAGS) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(OBJ:.o=.d)
