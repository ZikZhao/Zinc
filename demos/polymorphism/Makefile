CXX := g++
CXXFLAGS := -std=c++20 -O2 -Wall -Wextra -pedantic
TARGET := polymorphism_demo
SRC := main.cpp

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
