CXX ?= g++
CXXFLAGS ?= -std=c++23 -O3

ZINC ?= ../../build/debug/bin/zinc

CPP_SRC := main.cpp
CPP_BIN := main_cpp

ZN_SRC := main.zn
ZN_CPP := main.zn.cpp
ZN_BIN := main_zn

.PHONY: all cpp zn run-cpp run-zn clean distclean

all: cpp zn

cpp: $(CPP_BIN)

zn: $(ZN_BIN)

$(CPP_BIN): $(CPP_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(ZN_CPP): $(ZN_SRC)
	$(ZINC) $<

$(ZN_BIN): $(ZN_CPP)
	$(CXX) $(CXXFLAGS) -o $@ $<

run-cpp: $(CPP_BIN)
	./$(CPP_BIN)

run-zn: $(ZN_BIN)
	./$(ZN_BIN)

clean:
	$(RM) $(CPP_BIN) $(ZN_BIN)

distclean: clean
	$(RM) $(ZN_CPP)