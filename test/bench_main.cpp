#include "pch.hpp"
#include <algorithm>
#include <benchmark/benchmark.h>
#include <random>

static void BM_FlatMap_Insert(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        FlatMap<int, int> fm;
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            fm.insert({i, i});
        }
    }
    state.SetComplexityN(state.range(0));
}

static void BM_StdMap_Insert(benchmark::State& state) {
    for (auto _ : state) {
        state.PauseTiming();
        std::map<int, int> m;
        state.ResumeTiming();

        for (int i = 0; i < state.range(0); ++i) {
            m.insert({i, i});
        }
    }
    state.SetComplexityN(state.range(0));
}

static void BM_FlatMap_Lookup(benchmark::State& state) {
    FlatMap<int, int> fm;
    const std::int64_t size = state.range(0);
    for (int i = 0; i < size; ++i) {
        fm.insert({i, i});
    }

    std::vector<int> lookups(static_cast<std::size_t>(size));
    std::iota(lookups.begin(), lookups.end(), 0);
    std::mt19937 g(1337);
    std::shuffle(lookups.begin(), lookups.end(), g);

    for (auto _ : state) {
        for (int key : lookups) {
            auto it = fm.find(key);
            benchmark::DoNotOptimize(it);
        }
    }
    state.SetItemsProcessed(state.iterations() * size);
    state.SetComplexityN(size);
}

static void BM_StdMap_Lookup(benchmark::State& state) {
    std::map<int, int> m;
    const std::int64_t size = state.range(0);
    for (int i = 0; i < size; ++i) {
        m.insert({i, i});
    }

    std::vector<int> lookups(static_cast<std::size_t>(size));
    std::iota(lookups.begin(), lookups.end(), 0);
    std::mt19937 g(1337);
    std::shuffle(lookups.begin(), lookups.end(), g);

    for (auto _ : state) {
        for (int key : lookups) {
            auto it = m.find(key);
            benchmark::DoNotOptimize(it);
        }
    }
    state.SetItemsProcessed(state.iterations() * size);
    state.SetComplexityN(size);
}

BENCHMARK(BM_FlatMap_Insert)->RangeMultiplier(2)->Range(8, 4096)->Complexity();
BENCHMARK(BM_StdMap_Insert)->RangeMultiplier(2)->Range(8, 4096)->Complexity();

BENCHMARK(BM_FlatMap_Lookup)->RangeMultiplier(2)->Range(8, 4096)->Complexity();
BENCHMARK(BM_StdMap_Lookup)->RangeMultiplier(2)->Range(8, 4096)->Complexity();

BENCHMARK_MAIN();
