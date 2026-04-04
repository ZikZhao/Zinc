# Zinc Language Design (Current)

Status: living reference for the currently implemented Zinc language.  

Last updated: 2026-04-04.

## 0. How This Document Is Organized

Modern languages usually separate documents by purpose, and Zinc now follows the same pattern:

- Reference-first: a stable, searchable spec-like document for syntax and semantics.
- Design notes separate: rationale and historical evolution are stored independently.
- Status explicit: each major area marks "implemented" vs "planned" to avoid ambiguity.

This structure is similar in spirit to how Rust (Reference + RFCs), Go (Spec + proposals), and
Swift (Language Guide + Evolution) split language docs.

### Document conventions

- "Must" means required language behavior.
- "Should" means recommended behavior/implementation strategy.
- "Current implementation" blocks describe repository reality, not future intent.

## 1. Language Identity

Zinc is a statically typed systems language that transpiles `.zn` to C++23 source.

Core goals:

- Keep systems-level performance and low-level control.
- Add stricter semantic checks than idiomatic C++.
- Preserve C++ ecosystem interop via generated C++.

Non-goals (current stage):

- Replacing downstream native codegen backend.
- Full language-stability guarantee across versions.

## 2. Compilation Model

Pipeline:

1. Parse `.zn` via ANTLR4 grammar.
2. Build immutable AST.
3. Collect symbols and scopes.
4. Type/semantic checking.
5. Generate `.zn.cpp`.
6. Compile generated C++ with external compiler.

Current implementation:

- The `zinc` executable does not invoke `g++/clang++` itself.
- Output file name is input path + `.cpp` suffix (example: `main.zn -> main.zn.cpp`).

## 3. Lexical and Top-Level Syntax

Source of truth: `src/Zinc.g4`.

### 3.1 Top-level items

- variable declarations (`let`, `let mut`, `const`, optional `static`)
- type alias (`type`)
- function definition (`fn`)
- interface/class definition (`interface`, `class`)
- namespace definition (`namespace`)
- import statement (`import "..." as Alias;`)
- inline C++ block (`#CPP { ... }`)

### 3.2 Statements

- local block
- expression statement
- declaration statement
- `if` / `else`
- `switch` / `case` / `default`
- `match`
- `for` (C-style and while-style)
- `break`, `continue`, `return`, `throw`

## 4. Declarations, Scope, and Names

### 4.1 Variable declarations

```zn
let x = 1i32;
let mut y: i64 = 2i64;
const LIMIT: usize = 1024usize;
```

Rules:

- `const` initializer must be compile-time evaluable.
- global mutable variable declarations are rejected by current checker.

### 4.2 Unified symbol namespace per scope

Current implementation stores type/value/template/namespace in a unified identifier map.

- Redeclaration of an identifier in the same scope is rejected.
- Functions are modeled as overload sets under one symbol.

## 5. Type System

### 5.1 Primitive types

`void`, `i8`, `i16`, `i32`, `i64`, `isize`, `u8`, `u16`, `u32`, `u64`, `usize`, `f32`, `f64`, `bool`, `strview`, `nullptr`

### 5.2 Composite types

- struct type literal: `{ x: i32, y: f64 }`
- function type: `(T1, T2) -> R`
- reference: `&T`, `&mut T`, `move &T`
- pointer: `*T`, `*mut T`
- dynamic interface type: `dyn I`, `dyn mut I`
- union: `A | B`
- array/span:
  - `[T]` (span-like)
  - `[T; N]` (fixed size, `N` compile-time integer)

### 5.3 User-defined types

- interface with function members
- class with fields, methods, constructors (`init`), destructors (`drop`), operators
- type alias (`type Name = ...`)

### 5.4 Template types

Template parameters support:

- type parameters (`T: type`)
- non-type parameters (`N: i32`)
- variadic parameters (`Ts...: type`)
- defaults

Instantiation supports both `<...>` and `::<...>`.

## 6. Functions and Methods

### 6.1 Definitions

```zn
fn add(a: i32, b: i32) -> i32 {
    return a + b;
}
```

### 6.2 Parameter forms

- `x: T`
- `x: T = default_expr`
- `x: T...` (variadic)
- explicit receiver: `self: &Self`, `self: &mut Self`, `self: move &Self`

### 6.3 Lambdas

```zn
let inc = (x: i32) => x + 1;
let f = (x: i32) => { return x + 1; };
```

## 7. Expressions and Operators

### 7.1 Core expression forms

- literals, identifiers
- member access (`.` / `->`)
- indexing and slicing
- function calls
- struct and array initialization
- lambda
- unary/binary/ternary operators
- assignment family
- cast: `as` and `as?`
- move/forward: `move expr`, `forward expr`

### 7.2 Operators

Current grammar supports arithmetic, comparison, logical, bitwise, shift,
assignment variants, and overloadable call/index/deref/pointer operators.

## 8. Control Flow

### 8.1 `if`

Condition is type-checked against `bool` in current semantics.

### 8.2 `switch`

Expression-based branching with `case` and `default` blocks.

### 8.3 `match`

Current semantic implementation focuses on union values:

- typed cases can narrow value type
- unreachable cases may be diagnosed

Example:

```zn
match (u) {
    x: i32 => { return x; }
    y: bool => { return y ? 1i32 : 0i32; }
    _ => { return 0i32; }
}
```

### 8.4 `for`

- C-style: init / cond / update
- while-style: `for (cond) { ... }`

## 9. Conversions and Overload Resolution

### 9.1 Assignability and conversion ranking

Current overload resolution uses ranked conversion and pairwise partial ordering.

Main conversion categories:

- exact match
- qualifier/reference adjustment
- upcast-like conversion (including pointer/dynamic paths)
- copy fallback
- no match

### 9.2 Overload selection strategy

- try non-template overloads first
- include template-generated candidates when needed
- detect ambiguity via second validation pass

## 10. Runtime Representation Decisions (Important)

### 10.1 Dynamic interface dispatch uses fat pointer, not C++ vtable

Current codegen lowers `dyn I` into a structure conceptually containing:

- `void* data`
- `uint64_t type_index`

Interface calls dispatch through generated `switch (type_index)` wrappers to concrete
implementations.

### 10.2 Union representation

Union is lowered to variant-like representation over flattened member types.

### 10.3 Type interning

Compiler-internal types are canonicalized/interned for fast identity/equality and
better memory behavior.

## 11. Module and Standard Library Model

### 11.1 Import model

`import "path" as Alias;`

- file-based resolution relative to importing file
- module cache is used during compile session

### 11.2 Standard declarations

`std` declarations are embedded from `src/std.d.zn` and available in root scope via
compiler bootstrap.

## 12. Compile-time Evaluation and Diagnostics

- `const` declarations require compile-time evaluable values.
- numeric compile-time casts perform range checks and can emit overflow diagnostics.
- semantic analyzer attempts to keep compiling to report multiple errors where possible.

## 13. Implemented vs Planned

Implemented (current repository):

- parser/AST/type checker/codegen pipeline
- classes/interfaces/dynamic dispatch
- union + union-match semantics
- template core infrastructure + specialization matching paths
- array/span typing (`[T]`, `[T; N]`)

Not in current grammar (historical ideas):

- nullable shorthand `T?`
- intersection type syntax `A & B`

Planned or partial:

- enum support
- more complete type-level operations
- deeper constexpr/function-eval extensions

## 14. Document Map

- Current reference (this file): `language design.md`
- Historical kickoff proposal: `reports/language design (initial proposal).md`
- Project evolution logs: `reports/week1.md` ... `reports/week8.md`
