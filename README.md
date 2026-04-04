# Zinc Programming Language

> 2026 Dissertation Project | University of Bristol  
>
> Slogan: Galvanizing the C++ ecosystem.

Zinc is a statically typed systems language that transpiles `.zn` into C++23 source code.
The project focuses on stronger frontend semantics, while still leveraging C++ toolchains for final native binaries.

## Quick Start

### Prerequisites

- CMake `>= 3.24`
- C++23 compiler (GCC/Clang)
- Java (ANTLR tool)
- Python 3
- `antlr4-runtime`, `GTest`, `benchmark`

### Build compiler

```bash
cmake --preset debug
cmake --build --preset debug --target zinc
```

### Compile a Zinc program

```bash
# 1) Transpile .zn -> .zn.cpp
./build/debug/bin/zinc demos/polymorphism/main.zn

# 2) Compile generated C++
g++ -std=c++23 -O3 -o demos/polymorphism/zn_main demos/polymorphism/main.zn.cpp

# 3) Run
./demos/polymorphism/zn_main
```

## What Matters In Zinc Design

### 1) Dynamic dispatch uses fat pointers, not C++ vtables

Instead of relying on C++ virtual dispatch layout, Zinc lowers `dyn Interface` values to
a fat-pointer-like runtime representation (`data pointer + type index`) and generates
`switch(type_index)` dispatch wrappers.

This keeps dispatch behavior explicit in generated code and avoids tight coupling to C++
ABI-level virtual table conventions.

### 2) C++ is treated as backend IR

Zinc performs language semantics in its own frontend (parse/symbol/type/codegen prep), then
emits C++ for final compilation. This keeps Zinc-specific rules in one place while preserving
ecosystem interoperability.

### 3) Interned canonical types in compiler core

Compiler-internal type representations are interned/canonicalized to improve identity checks,
structural comparisons, and memory behavior in semantic passes.

## Compiler Pipeline

```text
Source (.zn)
  -> ANTLR4 Parser
  -> AST Builder
  -> Symbol Collection
  -> Type/Semantic Checking
  -> C++ Code Generation (.zn.cpp)
  -> Native binary via external C++ compiler
```

## Documentation Map

- Current language reference: [language design.md](language%20design.md)
- Historical kickoff proposal: [reports/language design (initial proposal).md](reports/language%20design%20(initial%20proposal).md)
- Weekly evolution logs: [reports/week1.md](reports/week1.md), [reports/week2.md](reports/week2.md), [reports/week3.md](reports/week3.md), [reports/week6.md](reports/week6.md), [reports/week7.md](reports/week7.md), [reports/week8.md](reports/week8.md)

## Repo Structure

```text
Zinc/
├── src/
├── demos/
├── tests/
├── reports/
├── CMakeLists.txt
└── CMakePresets.json
```

---

Created by Zik Zhao | University of Bristol, 2026