# Compiler and flags
CXX = g++
CXXFLAGS = -g -O0 --std=c++23 -I. -Iout # Get flags from your tasks.json [cite: 27, 28]
LDFLAGS = -lfl # Get linker flags from your tasks.json [cite: 27]
TARGET = out/parser

# Source files
# Note: We list the .o files we want, and 'make' will find the corresponding .cpp
OBJS = out/ast.o out/value.o out/type.o out/ref.o out/exception.o out/parser.tab.o out/lex.yy.o

# Phony targets are ones that don't represent actual files
.PHONY: all clean

# The default target, executed when you just run 'make'
all: $(TARGET)

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
out/%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Rule to generate the C++ code for the parser from the .y file
out/parser.tab.cpp: parser.y
	mkdir -p out # Ensure directory exists [cite: 15]
	bison -d -o $@ $< [cite: 18]

# Rule to generate the C++ code for the lexer from the .l file
out/lex.yy.cc: lexer.l out/parser.tab.cpp # Depends on parser.tab.h which is created with parser.tab.cpp
	flex --c++ -o $@ $< [cite: 21, 22]

# Rule to clean the build artifacts
clean:
	rm -rf out