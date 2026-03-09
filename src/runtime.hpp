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

#pragma clang diagnostic ignored "-Weverything"

template <typename T>
struct FunctionDecay {
    using type = T;
};

template <typename R, typename... Args>
struct FunctionDecay<std::function<R(Args...)>> {
    using type = R(Args...);
};

template <typename T>
using FunctionDecayT = typename FunctionDecay<T>::type;

template <typename... Sigs>
class $PolyFunctionImpl {
private:
    constexpr static inline std::size_t SBO_LIMIT = 3 * sizeof(void*);

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
        virtual void clone_to($PolyFunctionImpl& target) const = 0;
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
    struct InterfaceImpl : virtual InterfaceBase, ImplMixin<InterfaceImpl<Lambda>, Sigs>... {
        mutable Lambda lambda_;
        InterfaceImpl(Lambda lambda) : lambda_(std::move(lambda)) {}
        decltype(auto) invoke(auto&&... args) const {
            return lambda_(std::forward<decltype(args)>(args)...);
        }
        void clone_to($PolyFunctionImpl& target) const override {
            if constexpr (sizeof(InterfaceImpl) > SBO_LIMIT) {
                target.interface_ = new InterfaceImpl(lambda_);
            } else {
                target.interface_ = new (target.sbo_buffer_) InterfaceImpl(lambda_);
            }
        }
    };

private:
    InterfaceBase* interface_;
    std::byte sbo_buffer_[SBO_LIMIT]{};

public:
    $PolyFunctionImpl(auto lambda)
        requires(sizeof(InterfaceImpl<decltype(lambda)>) <= SBO_LIMIT)
        : interface_(new (sbo_buffer_) InterfaceImpl(std::move(lambda))) {}
    $PolyFunctionImpl(auto lambda) : interface_(new InterfaceImpl(std::move(lambda))) {}
    $PolyFunctionImpl(const $PolyFunctionImpl& other) { other.interface_->clone_to(*this); }
    $PolyFunctionImpl($PolyFunctionImpl&& other) noexcept {
        if (other.is_sbo()) {
            other.interface_->clone_to(*this);
            other.interface_->~InterfaceBase();
            other.interface_ = nullptr;
        } else {
            interface_ = other.interface_;
            other.interface_ = nullptr;
        }
    }
    $PolyFunctionImpl& operator=($PolyFunctionImpl other) {
        destory();
        if (other.is_sbo()) {
            other.interface_->clone_to(*this);
        } else {
            interface_ = other.interface_;
            other.interface_ = nullptr;
        }
    }
    ~$PolyFunctionImpl() { destory(); }
    constexpr auto is_sbo() const noexcept -> bool {
        return static_cast<const void*>(interface_) == static_cast<const void*>(sbo_buffer_);
    }
    constexpr void destory() noexcept {
        if (interface_) {
            if (is_sbo()) {
                interface_->~InterfaceBase();
            } else {
                delete interface_;
            }
            interface_ = nullptr;
        }
    }
    decltype(auto) operator()(auto&&... args) const {
        if (!interface_) {
            throw std::bad_function_call();
        }
        return interface_->call(std::forward<decltype(args)>(args)...);
    }
};

template <typename... Sigs>
using $PolyFunction = $PolyFunctionImpl<FunctionDecayT<Sigs>...>;
