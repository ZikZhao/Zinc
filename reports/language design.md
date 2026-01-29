# Zinc Language Design Specification (`.zn`)

Slogan: Galvanizing Systems Programming.

Type: Static, Strong-typed, Systems Programming Language.

Core Identity: The memory safety of Rust combined with the ergonomic syntax of TypeScript.

---

## 1. Philosophy

Zinc aims to provide a high-performance, system-level development experience that prioritizes safety and developer ergonomics. It enforces strict type safety and memory discipline (RAII) without a garbage collector, serving as a robust layer against common system programming pitfalls.

------

## 2. Basic Syntax & Scoping

Zinc follows a modern C-family syntax heavily influenced by TypeScript, prioritizing readability and explicit intent.

### Declarations

```
// Type inference is enabled by default
let x = 10;           // Immutable by default
let mut y = 20;
const PI = 3.14;      // Compile-time constant

// Explicit typing
let name: string = "Zinc";
```

### Namespace Strategy

- **Single Namespace Constraint:** Within a single scope, a symbol name must be unique. A name cannot refer to both a Type and a Variable simultaneously. This eliminates ambiguity and simplifies code analysis.

------

## 3. Data Types

### Arrays

Zinc distinguishes syntactically between array *construction/definition* and array *access* to provide clarity.

- **Fixed-size Array:** Declared using C-style suffix `Type[Size]`.
- **Dynamic Array:** Declared using `Type[]`.

```
// Declaration
let grid: int[4][4];  // A 4x4 Fixed-size array
let list: int[];      // A Dynamic array (Vector)

// Element Access (Values) - Uses standard brackets
let val = list[0];
```

### Tuples & Type Indexing

To strictly separate value indexing from type manipulation, Zinc uses **Dot Syntax** for accessing types within tuples or variadic packs.

```
type Pair = (int, float);

// Accessing the 0th type in the tuple
type FirstType = Pair.0; 
```

### Unions & Intersections

Zinc employs an algebraic type system to express complex data and behavioral relationships.

- **Union (`|`)**: Represents **Data Polymorphism** (A value is either Type A OR Type B).
  - *Feature:* **Common Base Access**. If all types in a union share a common base class (e.g., `Dog | Cat` where both extend `Animal`), the language allows direct access to the shared members (like `Animal.name`) without explicit casting.
- **Intersection (`&`)**: Represents **Behavioural Polymorphism** (Function Overloads).

```
// Data Union
let id: int | string; 

// Function Intersection (Overloading)
type Handler = ((int) -> void) & ((string) -> void);
```

### Null Safety

Zinc enforces explicit nullability. By default, types are **non-nullable**.

- **Nullable Type:** `T?`.
- **Safety Rules:** Implicit boolean conversion of nullable types (e.g., `if (user)`) is forbidden to prevent ambiguity. Explicit comparison is required.

```
let user: User? = get_user();

// Syntax Error: if (user) { ... } 

// Correct: Explicit Check
if (user != null) {
    // Flow typing automatically refines 'user' to non-nullable 'User' in this block
    print(user.name);
}

// Helpers
let name = user?.name ?? "Anonymous"; // Nullish operator for safe access and defaults
```

------

## 4. Functions

Zinc treats functions as first-class citizens, unifying named functions and closures under a single semantic model.

### Syntax

Arrows (`->`) are mandatory in definitions to clearly separate parameters from return types.

```
// Named Function
fn add(a: int, b: int) -> int {
    return a + b;
}

// Lambda / Anonymous Function
// Supports type inference for arguments in known contexts
let sub = (a, b) => a - b; 
```

### Mutability (The Position Rule)

Zinc uses a positional syntax to disambiguate the role of the `mut` keyword, resolving the confusion between "modifying the object" and "returning a mutable reference."

1. **`mut` before `fn`**: The method modifies the receiver (`this`).
2. **`mut` after `->`**: The function returns a mutable reference.

```
class Buffer {
    // 1. Const method (read-only 'this'), returns a value
    fn size() -> int { ... }

    // 2. Mutable method (writable 'this')
    mut fn push(val: int) { ... }

    // 3. Const method, returns a MUTABLE reference to internal data
    fn get_mut(idx: int) -> mut int { ... }
}
```

------

## 5. Classes & OOP

Zinc supports single class inheritance and multiple interface implementations. It utilizes RAII (Resource Acquisition Is Initialization) for deterministic resource management without a Garbage Collector.

### Syntax

- **Keywords:** `class`, `interface`.
- **Relationships:** `extends Base`, `implements Interface`.
- **Safety:** Assigning a derived class **value** to a base class variable (slicing) is a compile-time error. Polymorphism is only allowed via references.

### Structural Type

```
type Node = {
	value: int;
	prev: &Node;
	next: &Node;
};
```

Structural types will be interned while classes will not.

### Lifecycle

Zinc uses specific keywords to distinguish lifecycle hooks from standard methods.

- **Constructor:** `init`.
- **Destructor:** `drop` (Signifies the end of ownership and resource release).

```
class FileHandler {
    fd: int;

    // Constructor
    init(path: string) {
        this.fd = open(path);
    }

    // Destructor (RAII)
    drop() {
        close(this.fd);
    }
}

// Instantiation uses the class name
let f = FileHandler("log.txt");
```

------

## 6. Generics & Metaprogramming

Zinc provides powerful compile-time metaprogramming capabilities with a clean, readable syntax.

### Variadic Parameters

Zinc avoids complex fold expressions in favor of imperative-style compile-time loops (`for const`).

- **Definition:** `...T` prefix.
- **Spread:** `...args` prefix.
- **Iteration:** `for const`.

```
fn process<...Ts>(args: ...Ts) {
    // 1. Type Access via Dot
    type FirstType = Ts.0;

    // 2. Value Access via Bracket
    let firstVal = args[0];

    // 3. Compile-time Iteration (Unrolled during compilation)
    for const (arg in args) {
        print(arg);
    }

    // 4. Forwarding (Spread syntax)
    other_fn(...args);
}
```

### Lifetimes (Contracts)

Zinc simplifies memory safety contracts by avoiding abstract lifetime parameters (like `'a`) in generics.

- **Default Rule:** The output lifetime is automatically bound to the first input parameter or `this`.
- **Explicit Anchor:** Use `{arg_name}` syntax to explicitly bind output lifetimes to specific input arguments.

```
// Explicitly states: Return value lives as long as both 'x' and 'y'
fn pick(x: &str, y: &str) -> &{x, y} str {
    return cond ? x : y;
}
```

------

## 7. Module System

**Design Philosophy:** File-based modules (ESM style) with a global standard library.

### Import/Export

- **No Default Exports:** Zinc mandates named exports to ensure refactoring safety and clarity.
- **Syntax:** A hybrid of Python and TypeScript.

TypeScript

```
// math.zn
export fn add(a: int, b: int) -> int { return a + b; }

// main.zn
import "./math" as m;          // Namespace import
from "./math" import add;      // Named import
from "./math" import *;        // Wildcard import (imports all exported symbols)
```

### Standard Library

The standard library (`std`) is globally available as a built-in symbol (Prelude). Users access standard features via `std.` without explicit import statements (e.g., `std.vector`, `std.print`).