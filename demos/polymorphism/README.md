# Polymorphism Benchmark Demo (Zinc + C++)

This demo benchmarks polymorphic dispatch performance with a large AST evaluator.

Unlike the rasterizer demo, this one does not reference an external C++ repository.
Its native C++ implementation is already in this folder.

## Files in This Folder

- `main.cpp`: native C++23 benchmark implementation.
- `main.zn`: Zinc source implementation.
- `main.zn.cpp`: generated C++ from `main.zn`.
- `Makefile`: helper targets to build and run both versions.

## What It Does

The benchmark:

- builds a high-entropy random AST (integer-only operations),
- flattens it into an RPN execution list,
- runs one warm-up pass,
- runs timed stack-based evaluation loops,
- runs an additional `dynamic_cast` scan benchmark,
- prints throughput and checksum values.

Operations include arithmetic (`+ - * / %`), bitwise (`& | ^`), comparisons (`> ==`), and ternary (`cond ? a : b`) style evaluation.

## Prerequisites

Required:

- a C++23 compiler (`g++` or `clang++`),
- `make`.

Optional (only if you want to regenerate `main.zn.cpp` from Zinc source):

- Zinc compiler binary (default path in Makefile: `../../build/debug/bin/zinc`).

## Quick Start (Native C++ Baseline)

```bash
cd demos/polymorphism
make cpp
./cpp_main
```

## Run the Zinc Version

If Zinc compiler already exists at `../../build/debug/bin/zinc`:

```bash
cd demos/polymorphism
make zn
./zn_main
```

If you still need to build the Zinc compiler first:

```bash
# from repository root
cmake --preset debug
cmake --build --preset debug --target zinc

# then run the demo
cd demos/polymorphism
make zn
./zn_main
```

## Build and Run Both for Comparison

```bash
cd demos/polymorphism
make clean
make all
./cpp_main
./zn_main
```

## Typical Output Includes

- generated node count,
- RPN size,
- warm-up output,
- max stack size,
- benchmark total time,
- node throughput (million nodes/sec),
- final checksum,
- dynamic-cast throughput (million checks/sec).

## Example Run Output

The following outputs were captured by running both binaries once in this folder:

```text
# ./cpp_main
Generating AST...
Nodes generated: 1000042
RPN generation complete. Size: 1000042
Warm-up output: 0
Max stack size needed: 25
Running benchmark on 2000 iterations...
Total time: 13.3691 s
Throughput: 149.605 million nodes/sec
Final Result Checksum: 0
ValueNode count: 521690
ValueNode single-pass XOR (hex): 0x5f
DynamicCast loop time: 5.3806 s
DynamicCast throughput: 95.1607 million checks/sec
ValueNode DynamicCast XOR (hex): 0x0
```

```text
# ./zn_main
Generating AST...
Nodes generated: 1000042
RPN generation complete. Size: 1000042
Warm-up output: 0
Max stack size needed: 25
Running benchmark on 2000 iterations...
Total time: 11.7094 s
Throughput: 170.811 million nodes/sec
Final Result Checksum: 0
ValueNode count: 521690
ValueNode single-pass XOR (hex): 0x5f
DynamicCast loop time: 1.38068 s
DynamicCast throughput: 370.846 million checks/sec
ValueNode DynamicCast XOR (hex): 0x0
```

Performance numbers will vary by compiler version and running environment.

## Notes

Current benchmark sizing is fixed in source:

- `max_nodes = 1,000,000`
- `max_depth = 50`
- `benchmark_iterations = 2000`
- `dynamic_cast_iterations = 512`

You can tune these constants in `main.cpp` and `main.zn` if you want different stress levels.