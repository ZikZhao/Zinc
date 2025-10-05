# Compiler and flags
CXX = g++
CXXFLAGS = -g -O0 --std=c++23 -I. -Iout
LDFLAGS = -lfl
TARGET = out/parser

# Source files
OBJS = out/ast.o out/value.o out/type.o out/ref.o out/exception.o out/parser.tab.o out/lex.yy.o

# Precompiled header
PCH_SRC = pch.hpp
PCH_OBJ = $(PCH_SRC).gch

# Phony targets are ones that don't represent actual files
.PHONY: all clean

# The default target
all: $(TARGET)

# Rule to create the precompiled header first
$(PCH_OBJ): $(PCH_SRC)
	$(CXX) $(CXXFLAGS) -x c++-header -o $@ $<

# Rule to link the final executable
$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

# Rule to compile the lexer and parser generated files
out/parser.tab.o: out/parser.tab.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

out/lex.yy.o: out/lex.yy.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Pattern rule to compile any other .cpp file
# $< is the first prerequisite (the .cpp file)
# $@ is the target (the .o file)
out/%.o: %.cpp $(PCH_OBJ)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to generate the C++ code for the parser from the .y file
out/parser.tab.cpp: parser.y
	mkdir -p out
	bison -d -o $@ $<

# Rule to generate the C++ code for the lexer from the .l file
out/lex.yy.cc: lexer.l out/parser.tab.cpp # Depends on parser.tab.h which is created with parser.tab.cpp
	flex --c++ -o $@ $<

# Rule to clean the build artifacts
clean:
	rm -rf out