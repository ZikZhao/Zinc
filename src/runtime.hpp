#pragma once

// IWYU pragma: begin_exports
#include <algorithm>
#include <any>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <complex>
#include <concepts>
#include <condition_variable>
#include <deque>
#include <exception>
#include <expected>
#include <filesystem>
#include <format>
#include <forward_list>
#include <fstream>
#include <functional>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <numbers>
#include <numeric>
#include <optional>
#include <print>
#include <queue>
#include <random>
#include <ranges>
#include <semaphore>
#include <set>
#include <span>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>
// IWYU pragma: end_exports

template <typename... Sigs>
class PolyFunction {
private:
    template <typename T>
    struct Callable;

    template <typename R, typename... Args>
    struct Callable<R(Args...)> {
        virtual ~Callable() = default;
        virtual R call(Args... args) const = 0;
    };

    struct InterfaceBase : virtual Callable<Sigs>... {
        using Callable<Sigs>::call...;
        virtual std::unique_ptr<InterfaceBase> clone() const = 0;
    };

    template <typename Derived, typename Signature>
    struct ImplMixin;

    template <typename Derived, typename R, typename... Args>
    struct ImplMixin<Derived, R(Args...)> : virtual Callable<R(Args...)> {
        R call(Args... args) const final {
            return static_cast<const Derived*>(this)->invoke(std::forward<Args>(args)...);
        }
    };

    template <typename Lambda>
    struct InterfaceImpl : InterfaceBase, ImplMixin<InterfaceImpl<Lambda>, Sigs>... {
        mutable Lambda lambda_;
        InterfaceImpl(Lambda lambda) : lambda_(std::move(lambda)) {}
        decltype(auto) invoke(auto&&... args) const {
            return lambda_(std::forward<decltype(args)>(args)...);
        }
        std::unique_ptr<InterfaceBase> clone() const override {
            return std::make_unique<InterfaceImpl<Lambda>>(lambda_);
        }
    };

private:
    std::unique_ptr<InterfaceBase> interface_;

public:
    template <typename Lambda>
    PolyFunction(Lambda lambda)
        : interface_(std::make_unique<InterfaceImpl<Lambda>>(std::move(lambda))) {}
    PolyFunction(const PolyFunction& other) : interface_(other.interface_->clone()) {}

    decltype(auto) operator()(auto&&... args) const {
        assert(interface_ != nullptr);
        return interface_->call(std::forward<decltype(args)>(args)...);
    }
};

int f(int x, int y) { return x + y; }

float f(float x, float y) { return x * y; }

auto lambda = [](auto&&... args) { return f(std::forward<decltype(args)>(args)...); };

PolyFunction<int(int, int), float(float, float)> poly_func{lambda};

static_assert(requires { poly_func(2, 3); });
static_assert(requires { poly_func(2.0f, 3.0f); });
