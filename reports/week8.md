### **Weekly Progress Report: Zinc to C++ Transpiler**

#### 1. Introduction & Core Philosophy

This project implements a transpiler for Zinc, a statically typed language designed to compile into high-performance, safety-guaranteed C++20.

- **Architecture:** It operates as a transpiler rather than a compiler, leveraging the C++ compiler's backend for optimization while enforcing stricter safety guarantees at the frontend level.
- **Design Philosophy:**
  - **"Pay for what you use":** Direct mapping to C++ primitives where possible (e.g., integers, operators) to ensure zero overhead.
  - **Runtime Augmentation:** A specialized runtime (`runtime.hpp`) provides capabilities absent in C++'s native semantics (e.g., `PolyFunction` for first-class overloaded function sets) with controlled overhead.
  - **Correctness by Construction:** The core hypothesis is that if the Zinc static analysis (Borrow Checker & Lock Order Checker) passes, the generated C++ is guaranteed to be safe. This allows the transpiler to emit performant raw pointers instead of overhead-heavy smart pointers.

#### 2. Architecture Overview

The system follows a highly parallelized pipeline design, separating immutable syntax from mutable semantics.

```mermaid
flowchart LR

    subgraph P1 [Async Parsing]
        SM[SourceManager] --> ANTLR[ANTLR4 Kernel]
        ANTLR --> CST[Concrete Syntax Tree]
        CST --> AST[Immutable AST]
    end

    subgraph P2 [Semantic Analysis]
        SC[Symbol Collection] --> TC[Type Check]
        BC[Borrow Checker]
        TC --> BC
    end

    subgraph P3 [CodeGen]
        CodeGen[CodeGen] --> CPP(C++20 Source)
    end

    AST --> SC
    BC --> CodeGen
```

#### 3. Language Comparison

| **Feature**           | **Zinc**                                                     | **C++ (C++20/23)**                                           | **Rust**                                                     | **Zig**                                                  |
| --------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ | ------------------------------------------------------------ | -------------------------------------------------------- |
| **Core Positioning**  | Safe subset & extension for the C++ ecosystem                | High-performance legacy bedrock                              | Safe system language (Rewrite everything)                    | A better C (No hidden control flow)                      |
| **Compilation Model** | Transpiler (to C++) Leverages existing C++ compiler backends | Native Compiler (GCC/Clang/MSVC)                             | Native Compiler (LLVM based)                                 | Native Compiler (LLVM + Self-hosted)                     |
| **Memory Safety**     | RAII + Borrow Check + Lock Order Analysis                    | Relies on discipline (RAII), risk of dangling pointers       | Rigorous ownership & lifetime enforcement                    | No implicit allocation, manual management (Defer)        |
| **C++ Interop**       | Directly inherit classes, instantiate templates, throw exceptions | N/A                                                          | High Friction, requires `cxx`/`bindgen`; struggles with templates/inheritance | Excellent C interop, but limited C++ support             |
| **Metaprogramming**   | C++ Templates + Metaprogramming + Static Reflection          | Powerful but complex syntax and unreadable diagnostics       | Declarative (`macro_rules!`) & Procedural Macros             | Comptime: Arbitrary compile-time code execution          |
| **OOP Support**       | Class + Interface                                            | Multiple inheritance,  diamond inheritance, virtual inheritance | Traits: Composition over inheritance; no class inheritance   | Structs only; polymorphism via composition/tagged unions |
| **Lifetime Syntax**   | Implicit / Anchor-based: No `'a`; uses `&{arg} T` syntax     | Managed mentally by the programmer                           | Complex generic lifetime parameters (`<'a>`)                 | Manual memory management                                 |
| **Error Handling**    | Optional + Expected + Exceptions                             | Exceptions                                                   | Result used with `?` operator                                | Error Unions: Used with `try`; no exceptions             |
| **Syntax & Feel**     | TypeScript-like: Ergonomic, low cognitive load               | Verbose, legacy syntax baggage                               | Unique, steep learning curve                                 | Minimalist C-style: Few keywords, explicit               |

#### 4. Key Technical Decisions

- **Parsing Strategy Migration**

  Migrated the parsing infrastructure from Bison (LALR/Shift-Reduce) to ANTLR4 (Adaptive LL(*)/Top-Down). This architectural shift from a bottom-up state machine to a top-down recursive descent approach aligns naturally with the custom AST builder visitor. It offers superior flexibility in handling context-sensitive syntax and simplifies the generation of meaningful error diagnostics compared to the rigid shift-reduce conflicts often encountered in Bison.

- **Unified AST for Semantic Disambiguation:**

  Zinc allows for limited compile-time type manipulation, similar to C++. This capability, however, introduces syntactic ambiguities where type operations and value expressions overlap. For instance, `array[1]` could be interpreted as:

  - Type Declaration: An array of type `array` with size 1.
  - Indexing Operation: Accessing the second element of variable `array`.

  I implemented a Unified AST Node design where all expressions inherit from `ASTExpression`. Crucially, the AST structure remains immutable after construction. Semantic distinction is achieved purely through the `eval()` method, which returns a polymorphic `Object*` (resolving to either a `Type*` or `Value*`). This allows the transpiler to handle types as first-class citizens dynamically during semantic analysis without mutating the underlying syntax tree.

- ==**The Notorious Angle Bracket Ambiguity in Template Syntax**==

  1. **The Parsing Dilemma:** In modern language design, template and generic instantiation syntax varies widely (e.g., `<>` in C++/Rust, `[]` in Python). I opted for the traditional angle brackets (`<>`) for Zinc, but immediately encountered classic parsing ambiguities. For instance, in an expression like `MyStruct<T>{ ... }`, ANTLR's greedy parsing evaluates the closing `>` as a "greater-than" comparison operator. It mistakenly treats `T` as the left operand and the anonymous struct initialization `{ ... }` as the right operand, failing to close the template parameter list because the instantiation rule inherently accepts expressions.
  2. **The Brittle Predicate Hack:** My initial attempt to resolve this involved hacking ANTLR by injecting C++ semantic predicates to track the nesting depth of parentheses, brackets, and braces. This predicate was designed to force the parser to reject the "greater-than" branch for the closing `>` at the correct nesting level, guiding it naturally into the instantiation branch. However, this approach proved fundamentally flawed. ANTLR's ALL(*) algorithm relies heavily on speculative lookahead; during this speculative phase, the custom C++ predicate state does not update correctly, making state management highly fragile. Furthermore, it aggressively consumed `>` symbols out of context, erroneously parsing logical expressions like `a < b && c > d` as a broken template instantiation of `a` rather than a logical AND of two comparisons.
  3. **Adopting the "Turbofish" Syntax:** To solve this definitively without parser hacks, I adopted Rust's "Turbofish" syntax (`::<>`). C++ handles this ambiguity poorly in dependent scopes; because chained comparisons like `1 < 2 > 3` are syntactically valid in C++ (evaluating to `false > 3`, then `0 > 3`), the compiler forces developers to use a clumsy disambiguator, requiring syntax like `obj.template method<...>()`. Rust's approach (`obj.method::<...>()`) is far more elegant. By introducing a definitive token (`::`) immediately before the instantiation list, the parser is unambiguously locked into the template instantiation branch. It easily consumes the closing `>` without falling back to the comparison operator, preserving both syntax clarity and parser stability. In explicit type contexts, the standard angle bracket (`<`) can be used directly instead of the turbofish operator to initiate an instantiation list. Since comparison operations are syntactically invalid within type contexts, no ambiguity exists; even when operators such as `decltype`, `typeof`, or `requires` are involved, these specific prefix tokens guarantee a clear and unambiguous syntactic boundary, safely managing the transition from a type context back to a value or statement context.
  4. **Lexical Right-Shift Collisions:** A related lexical issue occurred with nested templates like `A<B<C>>`, where the lexer eagerly combined the final two brackets into a right-shift operator token (`>>`), resulting in a syntax error. I resolved this by completely removing the left-shift (`<<`) and right-shift (`>>`) operator tokens from the lexer. Instead, the parser explicitly matches two consecutive `<` or `>` symbols for shift operations. While a minor side effect is that `a > > b` (with a space) technically parses as a right-shift, this invalid syntax is strictly caught and rejected during the AST construction phase.

- **Lazy Type Resolution**

  I implemented Lazy Type Resolution by strictly decoupling the Symbol Collection phase from Type Checking. During symbol collection, type definitions are captured as raw AST expressions. These expressions are evaluated into concrete Type objects lazily and on-demand during the Type Checking phase. This strategy, augmented with memorization, efficiently handles forward references and complex dependency graphs (including potential circular types) while maintaining a clean separation of concerns between scoping and typing logic.

- **Advanced Type Interning & Recursive Resolution**

  The type system utilizes an advanced interning strategy where every unique type is guaranteed to be a singleton, immutable object, reducing type equality checks to $O(1)$ pointer comparisons. To handle recursive types (e.g., a struct containing a field of its own type) within this immutable framework, I implemented a hypothesis-based structural comparison algorithm. This algorithm assumes equality for currently visiting nodes to detect cycles during traversal. Furthermore, by designing a comprehensive strong ordering for all type structures, the interning registry achieves a lookup and insertion complexity of $O(M \log N)$ (where $M$ is the structural comparison cost), significantly optimizing memory usage and compilation speed compared to naive linear deduplication.

- **Collision-Proof Name Mangling**

  For the transpilation phase, I adopted a strict name mangling scheme using the "\$" symbol to decorate runtime artifacts and hidden static function overloads. Since "\$" is syntactically invalid in user-defined variables within my language but is supported in identifiers by most major C++ compilers (GCC, Clang, MSVC extensions), this creates a guaranteed collision-free namespace. This strategy effectively isolates transpiler-generated constructs from user code without requiring complex renaming algorithms or lookup tables, ensuring that the generated C++ code remains both robust and readable.

- **Type Inference for Untyped Literals**

  Literals in Zinc initially possess unspecified types. For instance, the literal 1 is recognized simply as an integer without an inherent signedness or bit-width. The transpiler resolves these into concrete types based on three rules:

  - Homogeneous Operations: Operations between two unspecified integers (including unary operations) yield another unspecified integer.
  - Type Promotion: When an unspecified integer interacts with a specified type, the result adopts that concrete type.
  - Contextual Enforcement: An unspecified integer is assigned a concrete type upon entering a strongly-typed context, such as an initialization expression in a declaration.

  Floating-point literals do not use a separate intermediate representation; they utilize double as the universal medium for calculations.

  `let a: i8[3] = [1, 20000, 3];`

- **Recursive Check-Mode for Declarations**

  If a declaration explicitly specifies a target type, the assignment proceeds by recursively entering the expression in Check-Mode. This forces the expression tree to adopt the expected type, triggering an error if a node cannot be converted (e.g., due to range overflow or prohibited implicit conversions). For example, in `let a: i8[3] = [1, 2, 3]`, the array node propagates the i8 requirement to its three child nodes, ensuring each element validates itself against the i8 constraints.

- **Recursive Type Interning & Canonicalization**

  To support immutable recursive types efficiently, I implemented a comprehensive interning strategy based on structural graph minimization. This system addresses the challenge of cyclic dependencies through a multi-stage resolution process:

  1. **Split-Phase Construction:** Reference types are stored as direct `Type*` pointers rather than double indirections to support both identifiers and raw type expressions. To facilitate this, type construction is strictly separated into **Allocation** and **Initialization** phases. A type address is available in the cache immediately after allocation, allowing recursive references to point to the address of a type that is currently being constructed, regardless of its initialization state.
  2. **Completeness vs. Sizedness:** The system distinguishes between a type being **Sized** (having a known memory layout, e.g., a pointer is 8 bytes) and **Complete** (having all descendants fully initialized). For example, in `type A = { a: &A; }`, the field `&A` is *sized* but *incomplete* until `A` closes the cycle. This distinction is critical for the precise timing for interning: only *complete* subgraphs are eligible for the global pool.
  3. **Dependency-Driven Resolution:** While acyclic (tree-structured) types are interned immediately via standard co-inductive comparison, recursive types utilize a **dependency graph**. An incomplete type registers dependencies on its incomplete children. The type is marked as *complete* only when all dependencies are resolved or when the dependency chain forms a cycle back to itself (closing the loop), at which point it triggers the interning process.
  4. **Two-Phase Interning (Canonicalization):** Interning occurs in two stages to minimize the state machine represented by the type graph:

     - **Self-Interning (Local Canonicalization):** This phase minimizes the newly constructed recursive type graph before it enters the global pool. Using a unified bottom-up coinductive traversal, the algorithm evaluates and locally pools child nodes first. When structurally identical nodes are detected, it discards the duplicate and redirects the parent's edges in-place. This single mechanism naturally resolves both isomorphic cyclic chains (e.g., merging A and B in `type A = { a: &B }; type B = { a: &A };`) and redundant sibling branches (e.g., merging C and D in `type A = { b: &B; }; type B = { c: &C; d: &D; }; type C/D = { a: &A };`). Consequently, all equivalent recursive structures are reduced to a single canonical instance on the fly.
     - **Global Interning:** Once locally minimized, the entire connected component (the type and its dependencies) is promoted to the global registry. This is an **atomic "all-or-nothing" operation**: either the top-level type matches an existing global instance (in which case the entire new graph is discarded to prevent partial dangling references), or the whole graph is interned as a new entry.
  5. **Transient Cache Management:** To prevent the type cache from holding dangling references to discarded temporary types (e.g., intermediate nodes `b1` and `b2` created during the resolution of `type A = { b1: &B; b2: &B; };`), the system implements a **cache invalidation policy**. If a type expression evaluates to an incomplete type, that entry is removed rather than reused. This forces a re-evaluation which, guaranteed by Horizontal Congruence, will eventually merge distinct allocation addresses into a single canonical instance upon completion, ensuring memory safety without complex invalidation sets.

- **Structural-to-Nominal Mapping & Implicit Dependency Reordering**

  Since C++ relies on nominal typing (classes/structs) unlike Zinc's structural type system, the transpiler synthesizes stable nominal definitions (e.g., `struct $structural_1`) for every unique structural shape encountered. A key challenge was handling recursive types: my initial approach relied on fragile forward declarations because the generated struct body referenced external type aliases. The refined strategy decouples the struct definition from its aliases by internally canonicalizing recursive identifiers, enabling a consistent definition-before-use emission order for both recursive and non-recursive types.

  Remarkably, this ordering is achieved without an explicit topological sort algorithm. Instead, it emerges naturally from an on-demand generation model using Cursor proxies. Output tokens are buffered in scoped Cursor instances rather than being written directly to the stream. For a declaration like `type A = B`, the transpiler initializes a cursor for the alias statement "using A = ...". When traversing B, if the structural type `$structural_n` is undefined, a nested cursor is instantiated on the stack to generate its definition. Since the nested cursor completes and flushes its content to the underlying stream before the parent cursor finalizes the alias declaration, the dependency (the struct definition) is guaranteed to appear in the output strictly before its usage, effectively leveraging the transpiler's call stack to enforce topological correctness.

- **C++ Interoperability & The Mutability Inversion Model**

  To ensure seamless interoperability with C++, adopting Rust's reference semantics (which behave primarily as non-nullable pointers) proved structurally inadequate. C++ references are strictly non-rebindable aliases, whereas its pointers accommodate nullability; forcing Rust's model onto C++ generation could mislead developers into writing ill-formed templates, such as std::vector<int32_t&>. Consequently, the language comprehensively inherits C++'s reference and pointer semantics, but fundamentally inverts its mutability paradigm. C++ defaults to pervasive, mutable implicit borrowing, which heavily obfuscates strict static borrow checking. To resolve this without heavily altering C++'s syntactic intuition, the mut keyword is introduced not as a property of the binding (as in Rust), but strictly as a property of the type—acting as the exact inverse of C++'s const. All data access and borrows are strictly immutable by default. While reference borrowing remains implicit and the & operator is traditionally reserved for address-of pointer operations, obtaining mutable access requires explicit opt-in: mut something for references and &mut something for pointers. Ultimately, both pointers and references are uniformly governed by the static borrow checker, ensuring memory safety without compromising C++ compatibility.

- **Explicit `self` and Method References**

  Member functions in Zinc adopt an explicit, strongly-typed `self` parameter, aligning with the paradigms of Python and Rust. To enforce strict syntactic boundaries, invoking a method directly through the class scope, such as `MyClass.func(obj, ...)`, is strictly prohibited. However, the language introduces Java-style method references using the double colon operator (e.g., `MyClass::func`). In Zinc, `::` is exclusively reserved for this purpose, acting as syntactic sugar to generate an anonymous function, rather than serving as a general scope resolution operator.

- **Value Categories and Move Semantics**

  C++ value categories (lvalue, prvalue, xvalue, etc.) and the overloaded semantics of `&&` (rvalue references versus universal references) are notorious sources of confusion. While Rust handles moves implicitly, discarding C++'s granular value semantics is not viable, as they are crucial for paradigms like rvalue-qualified methods in builder patterns. Zinc resolves this friction by elevating `move` and `forward` to first-class language keywords. Obtaining an rvalue is done via `move x`, which evaluates to the type `move &T`. Because moving inherently requires mutability, `move &T` is strictly equivalent to `move &mut T`, allowing intuitive overloads such as `fn func(self: &mut Self)` for lvalues and `fn func(self: move &Self)` for rvalues. Perfect forwarding is similarly streamlined: declaring a universal reference becomes `fn func(x: forward &T)`. This acts as a chameleon—yielding `&T` when passed an lvalue, and `move &T` when passed an rvalue. To pass it downstream, the programmer simply writes `forward x`, entirely eliminating the need to explicitly specify types or grapple with convoluted double-reference (`&&`) syntax, thus drastically flattening the learning curve while preserving C++'s expressive power.

- ==**Function Overload Resolution**==

  Zinc’s function overload resolution system is heavily inspired by C++ but deliberately rejects the ability to fully specialize function templates. In Zinc, an overload set bound to a fully qualified path consists exclusively of non-template functions and primary (unspecialized) template functions. This restriction significantly simplifies the resolution matrix and reduces inherent ambiguities. Furthermore, Zinc strictly forbids implicit conversions between unrelated types—completely eliminating the unpredictable behavior caused by C++'s non-explicit constructors and conversion operators.

  Argument-to-parameter conversion distances are quantified using a strict **Rank system**:

  - **Rank 0 (Exact Match):** No modification required (e.g., an lvalue `i32` argument matching an `&i32` parameter).
  - **Rank 1 (Single Adjustment):** Stripping the `mut` qualifier *or* modifying the reference category (e.g., `mut &i32` to `&i32`).
  - **Rank 2 (Dual Adjustment):** Stripping the `mut` qualifier *and* modifying the reference category simultaneously (e.g., `move &i32` to `&i32`, as `move` inherently implies a mutable reference).
  - **Rank 3 (Pointer Upcasting):** Upcasting to a base class or interface, prioritizing the nearest ancestor in the inheritance tree, up to `*void`.
  - **Rank 4 (Implicit Construction):** Applied only when the argument and parameter share the same decayed type and the parameter expects a prvalue (pure rvalue).

  Crucially, individual parameter conversions form a **strict partial order**. For example, if a class implements two distinct interfaces, a pointer upcast to either interface is mathematically incomparable, as neither is functionally "closer" than the other.

  Candidate functions are evaluated against each other using this same strict partial ordering. A function is deemed the "best viable candidate" if its parameter conversions are better than or equal to all other candidates, and strictly better in at least one parameter. If Candidate A is better for the first parameter but Candidate B is better for the second, they are incomparable.

  The compiler implements this using a highly efficient **two-pass algorithm**:

  1. **Pass 1 (Election):** The compiler iterates through the overload set, electing a *potential* best candidate. The current best is only replaced if the newly evaluated candidate is strictly superior.
  2. **Pass 2 (Validation):** The elected best candidate is cross-verified against all other candidates. If any comparison yields an equality or an incomparability, the compiler proceeds to ambiguity validation phase.

  To mirror C++'s preference for concrete functions over generic ones, non-template functions are strictly prioritized. The first pass initially iterates *only* over non-template functions. If the resulting best candidate is not a Rank 0 (exact) match, the compiler then appends template functions to the candidate list, deduces their instantiation arguments, and continues the iteration.

  To handle tie-breaking between templates and non-templates, the implementation utilizes a **boundary index**. During the ambiguity validation phase, the compiler maintains a temporary array of all candidates tied with the best candidate, alongside a boolean flag tracking if any incomparability occurred. If the flag is false (no incomparability) and all tied candidates possess indices strictly greater than the boundary index (meaning every tied competitor is a template), the tie is gracefully broken in favor of the non-template best candidate.

- ==**Template Specialization Resolution**==

  Template specialization resolution is largely analogous to function overload resolution: while the latter determines which candidate can suitably fit runtime values into parameter types, the former determines which candidate can suitably fit types into a pattern, subsequently deducing the types or values for the corresponding structural slots.

  Initially, template instantiation checks the primary template. Specializations are merely attributes of the primary template, providing alternative definitions when a given pattern is matched, but they do not affect whether an instantiation satisfies the primary base template. The primary template ensures that the kind of each instantiation argument (Type or Non-Type Template Parameter [NTTP]; Template Template Parameters [TTP] will not be implemented in Zinc) is consistent, and then verifies whether each instantiated argument satisfies its associated concept (an unary boolean predicate). Subsequently, during the specialization resolution phase, the resolver first compares the instantiation against all full specializations. A full specialization matches a unique set of instantiation arguments, making such matches inherently unambiguous.

  If no full specialization matches, partial specialization matching is performed. Zinc implements this using internal semantic objects (types or values) equipped with **binding slots**, rather than relying on AST-based structural matching. These slots, designated as `Auto`, are also utilized for type capturing in implicit templates declared with the `auto` keyword. Because the evaluation of types and values is computationally expensive, and the generated matching patterns have no practical utility beyond matching itself, Zinc precomputes these patterns and strictly disables template expansion during their evaluation. During the matching process, the `Object::pattern_match` method is invoked alongside a binding table. Types and values at each layer are checked against the target; any structural mismatch immediately short-circuits and terminates the pattern matching. When an `Auto` slot is matched, the captured type or value is recorded in the binding table. If the slot already exists in the table and the newly captured entity is not identical to the previously recorded one, the match evaluates to `false`.

  If the patterns at all positions of a partial specialization match successfully, it is deemed a viable candidate. Both patterns and partial specializations adhere to a strict partial ordering. Similar to function overloads, a two-pass traversal is employed. The first pass elects a potential best candidate. When comparing a pair of candidates, the patterns at each position are evaluated, and the strictly more specific pattern wins. This is implemented using an algorithm based on **Skolem constants**: slots are filled with unique, synthetic types that do not match any actual types in the system. If candidate A's Skolemized instance successfully passes through candidate B's pattern, then B's pattern is more general than A's. We then cross-check by passing B's Skolemized instance through A's pattern. If both are more general, they are functionally equivalent; if neither is more general, they are incomparable (meaning the two patterns represent disjoint subsets in the type space). To be the definitive best candidate, a specialization must be at least as specific as all other candidates at every position, and strictly more specific in at least one position. Crucially, this partial ordering comparison does not rely on concrete instantiation arguments; the specificity of a template is determined entirely at definition time and is an intrinsic property of the template itself. The second pass then verifies against all other candidates to detect ambiguities. Finally, the compiler instantiates the "instantiation scope" using the best candidate's binding table, injects the partial specialization parameters, and evaluates the target symbol. The result of this evaluation is then permanently cached.

  *Note: A classic example of incomparable template partial specializations leading to ambiguity:*

  ```
  class A<X: type, Y: type>;
  specialize<X: type> class A<X, f64>;
  specialize<Y: type> class A<i64, Y>;
  type B = A<i64, f64>;
  ```

- **Error Recovery & Error Cascading Prevention**

  To enhance diagnostic utility, the type system implements a robust error recovery mechanism. Instead of aborting upon the first semantic failure, the compiler reports the error and injects a sentinel `UnknownType` or `UnknownValue` (depending on the context) as results. These sentinels are designed to silently propagate through upstream operations: any expression interacting with an 'Unknown' operand evaluates to 'Unknown' without emitting further diagnostics. This strategy effectively suppresses cascading false positives (spurious errors) stemming from the initial fault, while preserving the compiler's ability to continue analyzing independent code sections and report multiple genuine errors in a single pass.

- **Embracing Monomorphization: Demoting C++ to an Intermediate Representation (IR)**

  To bridge the vast semantic gap between the source and target languages, the compiler's backend architecture underwent a decisive refactoring: fully embracing monomorphization and completely abandoning the initial strategy of direct mapping via the C++ template system. Early attempts to preserve C++ templates—intended to maintain the readability of the generated code—posed severe theoretical obstacles when implementing modern language features. Specifically, enforcing strict type interning for structural types within template-dependent contexts is incredibly difficult at the C++ level, and C++'s current metaprogramming capabilities are insufficient to support the static reflection planned for Zinc. Furthermore, Zinc's lazy type declaration mechanism requires the dynamic construction of a cascading dependency graph. Forcing the retention of local scopes while handling hoisted cross-scope accesses would make topological sorting during code generation excessively complex.

  By demoting C++ to a pure compile-time Intermediate Representation (IR), the current CodeGen pipeline now performs explicit instantiation and **Type Hoisting** for all generics prior to emission. This decision yields significant architectural advantages: globally unified type lifting entirely eliminates scope shadowing issues, making precise topological sorting and the strict separation of forward declarations from definitions trivial. The generation phase now simply emits the lowered, monomorphized AST—stripped of all non-runtime constructs and replaced with mangled identifiers—directly into the output stream. Concurrently, by eliminating complex template instantiations and overload resolution branches, the parsing speed of the downstream C++ compiler is theoretically improved. The accepted trade-off is the complete loss of human readability in the generated source, increased source code volume, and restricting interoperability to a one-way FFI (Zinc can call C++, but C++ cannot easily invoke highly mangled Zinc code). Ultimately, this compromise completely removes the limitations that the target language's abstraction boundaries previously placed on Zinc's core semantic expression.

- ==**Closed-World Polymorphism & Type-Index Dispatch**==

  C++’s reliance on `vptr`-based polymorphism occupies a compromised design space, prioritizing separate compilation and open-world extensibility at the cost of the Fragile Base Class (FBC) problem. In systems programming, assuming synchronized ABIs across dynamic libraries is often unrealistic—prompting industry standards to frequently restrict cross-boundary C++ polymorphism in favor of flat C ABIs to avoid severe memory corruption or segmentation faults. Recognizing this, Zinc strictly enforces a **Closed-World Assumption (CWA)** and discards vtables in favor of **Type-Index-Based Polymorphism** (conceptually aligning with tagged unions).

  For any polymorphic class, Zinc injects a compiler-generated type index at memory offset zero. Crucially, these indices are assigned using a contiguous topological inheritance order (e.g., all descendants of branch B are strictly less than descendants of branch C). This architecture yields profound benefits: it completely eliminates the notoriously complex pointer-adjustment overhead inherent to C++ object slicing and casting, and it reduces dynamic type checks (downcasting) to ultra-fast integer range comparisons—an optimization C++ developers often manually emulate via enum tags. Furthermore, during Code Generation, Zinc lowers all member functions (except destructors, to preserve C++ RAII) into static free functions. Virtual dispatch is implemented not via indirect pointers, but through a centralized wrapper function containing a `switch` statement over the object's type index to call the underlying worker function. Because practical class hierarchies rarely exceed a manageable number of variants, this jump-table approach provides the downstream C++ compiler with massive opportunities for aggressive inlining and **devirtualization**, effectively bypassing the branch-prediction penalties and instruction cache misses typically associated with indirect virtual calls.

#### 5. Development Checkpoints (Milestones)

The development is structured into granular phases to ensure stability before introducing advanced static analysis features.

| **Phase** | **Checkpoint**            | **Status**  | **Description**                                              |
| --------- | ------------------------- | ----------- | ------------------------------------------------------------ |
| **P1**    | **Core Infrastructure**   | Done        | PMR Memory model, Async File/Module Loading, ANTLR4 Integration. |
| **P2**    | **Basic Semantics**       | Done        | Primitive Types, Symbol Collection, Type Checker, Diagnostic System. |
| **P3**    | **Control Flow & Ops**    | Done        | Control flow (if/for), Operator Overloading via `OperationHandler`. |
| **P4**    | **CodeGen**               | In Progress | Emitting C++20 code based on semantic analysis results.      |
| **P5**    | **Classes & Namespaces**  | Done        | Struct/Class layouts, Member resolution, Namespace scoping.  |
| **P6**    | ==~~**Static Safety**~~== | Dropped     | ~~Borrow Checker~~                                           |
| **P7**    | **Metaprogramming**       | In Progress | Template inference and expansion (LSP support if time permits). |
| **P8**    | ==**Evaluation**==        | Planned     | Rewriting My CG Coursework with Zinc                         |

#### 6. Concrete Implementation

- **Hierarchical Lock-Free Memory Model (PMR Funnel):**

  To maximize allocation performance during compilation, I implemented a custom "Funnel" memory model using C++23 `std::pmr`:

  1. **Thread-Local Unsynchronized Pool:** For resizable objects (vectors/maps), avoiding atomic overhead.
  2. **Thread-Local Monotonic Buffer:** For fixed-size immutable nodes (AST), offering pointer-bumping speed.
  3. **Upstream Synchronized Pool:** Acts as the backing source, ensuring thread safety only when strictly necessary.

- **Pointer Tagging for Unified Symbol Storage**

  To optimize the memory footprint of the Scope system, I consolidated the four distinct symbol categories (Type Aliases, Variables, Overloads, and Templates) into a single unified map. Using separate maps for each category would incur a 5x overhead for the map structures and complicate duplicate symbol detection. Instead, I implemented a `PointerVariant` using tagged pointers. By leveraging the 8-byte alignment of the allocated objects, the lower three bits of the pointer are utilized to store the category tag. This allows the compiler to distinguish between symbol types within a standard 64-bit pointer size, simplifying collision checks while maximizing cache efficiency.

- **The PolyFunction Runtime:**

  To support storing "Overloaded Function Sets" as first-class citizens—a feature lacking in C++—I implemented PolyFunction. It utilizes advanced template metaprogramming to perform type erasure while maintaining dispatch capabilities, bridging the semantic gap between Zinc and C++.

- **Transition to Arbitrary-Precision Integers (BigInt)**

  I have overhauled the internal representation of integer values, moving from a tagged union of int64, uint64, and string_view to a unified BigInt implementation. This provides infinite precision for compile-time evaluation; users can now write complex integer expressions as long as the final result fits within the target container. This transition eliminates the overhead of repeatedly tag-checking during integer processing and removes concerns regarding intermediate overflows during constant folding.

- **Architectural Decoupling: Modern Visitor Pattern & POD AST**

  The Abstract Syntax Tree (AST) has undergone a comprehensive refactoring, transitioning from a traditional Object-Oriented paradigm to a Plain Old Data (POD) and Visitor-based architecture. Previously, embedding phase-specific logic (such as symbol collection, type checking, and evaluation) directly into AST nodes created a heavily coupled "God header," tangling the syntax representation with auxiliary systems like `Scope` and `TypeChecker`. In compiler design, AST structures are fundamentally stable, whereas the operations performed on them (e.g., semantic passes, pattern matching, linting) scale continuously. Decoupling the data from these operations allows for infinite extensibility. Furthermore, managing traversal context as member variables within the Visitor objects eliminates the overhead of passing context singletons through the compiler's hot paths.

  Crucially, rather than relying on the classic OOP double-dispatch pattern—which demands tedious `accept`/`visit` boilerplate and rigid return types (often forcing the use of type-erased wrappers like `std::any`, as seen in ANTLR's generated visitors)—this transpiler leverages modern C++'s `std::variant` and `std::visit`. This approach replaces runtime virtual polymorphism with compile-time static dispatch. It elegantly satisfies the DRY (Don't Repeat Yourself) principle: because `std::visit` resolves the best matching overload at compile time, a single visitor implementation can naturally fall back to base-class or generic overloads to simulate default behaviors. This achieves highly efficient, pattern-matching-like AST traversal while maintaining strict type safety and zero vtable overhead.

- ==**CodeGen Pipeline & Deterministic Name Mangling**==

  The CodeGen pipeline is strictly bifurcated into two phases: Type Generation followed by Code Generation. Post-semantic analysis, all reachable types are consolidated within the interning pool. Because the compiler operates macroscopically as a pure function, the insertion order of types into this pool is absolute and deterministic. Combined with a stable topological sort, this determinism allows the transpiler to utilize the interned type's chronological pool index as its stable nominal symbol in the generated C++. Crucially, only structs and classes are emitted as index-named declarations; all other composite or primitive types are expanded inline at their point of use.

  During the subsequent Code Generation phase, every function is assigned a globally mangled identifier. This includes member functions, which are lowered and hoisted to the global scope as free functions. To map Zinc's module paths to flat C++ identifiers securely, fully qualified paths are concatenated using underscores. To eliminate any ambiguity caused by user-defined identifiers that inherently contain underscores, each path segment is strictly prefixed by its character length (e.g., the path `global.class.static_symbol` is mangled as `_6global_5class_13static_symbol`). Furthermore, to support function overloads and template instantiations, a serialized representation of the parameter type list is injected into the mangled name, prefixed with a `0` to differentiate it from standard path segments. For example, `global.f(i32, bool)` becomes `_6global_1f_03i321b` (where `3i32` encodes `i32` and `1b` encodes `bool`). This length-prefixed mangling scheme—conceptually similar to the Itanium C++ ABI—is straightforward, perfectly reversible (demangleable), and guarantees a strictly collision-free namespace.

#### 7. Remaining Goals

1. ~~Template Syntax~~
2. ~~Deferred Static Analysis on Template Instantiation~~
3. Monomorphization
4. Built-in Types (by declaration file)
5. Array Type, Vector Type, Intersection and Union of Dynamic Struct Type
6. Completing Built-in Types
7. Method Reference
8. ~~Metaprogramming: Built-in Predicates~~
9. Metaprogramming: Concepts (Traits)
10. Template: Variadic Parameters
11. Abbreviated Function Templates by `auto` keyword
12. Module System
13. Class Template Argument Deduction (CTAD): by in-class deduction guide
14. String Literals As Types
15. ==TS-style Format String==
16. ==Static Reflection==