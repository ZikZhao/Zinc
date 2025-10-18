#pragma once
#include <cmath>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <map>
#include <set>
#include <functional>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <typeindex>
#include <ranges>
#include <cassert>
#include <array>

#define CHECK(condition) \
    do { \
        if (not (condition)) FatalError(#condition); \
    } while (false);

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

constexpr void FatalError(std::string_view message) {
    std::cout << message;
    std::abort();
}

template<typename ValueType>
using Map = std::unordered_map<std::string, ValueType>;

namespace OperatorFunctors {
    using Add = std::plus<>;
    using Subtract = std::minus<>;
    using Negate = std::negate<>;
    using Multiply = std::multiplies<>;
    using Divide = std::divides<>;
    using Remainder = std::modulus<>;
    using Equal = std::equal_to<>;
    using NotEqual = std::not_equal_to<>;
    using LessThan = std::less<>;
    using LessEqual = std::less_equal<>;
    using GreaterThan = std::greater<>;
    using GreaterEqual = std::greater_equal<>;
    using LogicalAnd = std::logical_and<>;
    using LogicalOr = std::logical_or<>;
    using LogicalNot = std::logical_not<>;
    using BitwiseAnd = std::bit_and<>;
    using BitwiseOr = std::bit_or<>;
    using BitwiseXor = std::bit_xor<>;
    using BitwiseNot = std::bit_not<>;
    struct LeftShift {
        template<typename T>
        auto operator() (const T& left, const T& right) const {
            return left << right;
        }
    };
    struct RightShift {
        template<typename T>
        auto operator() (const T& left, const T& right) const {
            return left >> right;
        }
    };
    template<typename Functor = void>
    struct OperateAndAssign {};
    template<>
    struct OperateAndAssign<void> {
        template<typename Left, typename Right>
        auto operator() (const Left& left, const Right& right) const {
            return left = right;
        }
    };
    using Assign = OperateAndAssign<>;
    using AddAssign = OperateAndAssign<Add>;
    using SubtractAssign = OperateAndAssign<Subtract>;
    using MultiplyAssign = OperateAndAssign<Multiply>;
    using DivideAssign = OperateAndAssign<Divide>;
    using RemainderAssign = OperateAndAssign<Remainder>;
    using BitwiseAndAssign = OperateAndAssign<BitwiseAnd>;
    using BitwiseOrAssign  = OperateAndAssign<BitwiseOr>;
    using BitwiseXorAssign = OperateAndAssign<BitwiseXor>;
    using LeftShiftAssign  = OperateAndAssign<LeftShift>;
    using RightShiftAssign = OperateAndAssign<RightShift>;
}

struct Location {
    struct {
        uint64_t line;
        uint64_t column;
    } begin, end;
};

std::ostream& operator << (std::ostream& os, const Location& loc);

template<uint64_t length>
class FixedString {
public:
    const char str_[length];
    constexpr FixedString(const char (&str)[length]) {
        std::copy_n(str, length, str_);
    }
    constexpr std::string_view operator * () const {
        return std::string_view(str_, length);
    }
};

template<typename Functor>
constexpr std::string_view GetOperatorString() {
    using namespace OperatorFunctors;
    if constexpr (std::is_same_v<Functor, void>) {
        return ""sv;
    } else if constexpr (std::is_same_v<Functor, Add>) {
        return "+"sv;
    } else if constexpr (std::is_same_v<Functor, Subtract>) {
        return "-"sv;
    } else if constexpr (std::is_same_v<Functor, Negate>) {
        return "-"sv;
    } else if constexpr (std::is_same_v<Functor, Multiply>) {
        return "*"sv;
    } else if constexpr (std::is_same_v<Functor, Divide>) {
        return "/"sv;
    } else if constexpr (std::is_same_v<Functor, Remainder>) {
        return "%"sv;
    } else if constexpr (std::is_same_v<Functor, Equal>) {
        return "=="sv;
    } else if constexpr (std::is_same_v<Functor, NotEqual>) {
        return "!="sv;
    } else if constexpr (std::is_same_v<Functor, LessThan>) {
        return "<"sv;
    } else if constexpr (std::is_same_v<Functor, LessEqual>) {
        return "<="sv;
    } else if constexpr (std::is_same_v<Functor, GreaterThan>) {
        return ">"sv;
    } else if constexpr (std::is_same_v<Functor, GreaterEqual>) {
        return ">="sv;
    } else if constexpr (std::is_same_v<Functor, LogicalAnd>) {
        return "and"sv;
    } else if constexpr (std::is_same_v<Functor, LogicalOr>) {
        return "or"sv;
    } else if constexpr (std::is_same_v<Functor, LogicalNot>) {
        return "not"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseAnd>) {
        return "&"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseOr>) {
        return "|"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseXor>) {
        return "^"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseNot>) {
        return "~"sv;
    } else if constexpr (std::is_same_v<Functor, LeftShift>) {
        return "<<"sv;
    } else if constexpr (std::is_same_v<Functor, RightShift>) {
        return ">>"sv;
    } else if constexpr (std::is_same_v<Functor, Assign>) {
        return "="sv;
    } else if constexpr (std::is_same_v<Functor, AddAssign>) {
        return "+="sv;
    } else if constexpr (std::is_same_v<Functor, SubtractAssign>) {
        return "-="sv;
    } else if constexpr (std::is_same_v<Functor, MultiplyAssign>) {
        return "*="sv;
    } else if constexpr (std::is_same_v<Functor, DivideAssign>) {
        return "/="sv;
    } else if constexpr (std::is_same_v<Functor, RemainderAssign>) {
        return "%="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseAndAssign>) {
        return "&="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseOrAssign>) {
        return "|="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseXorAssign>) {
        return "^="sv;
    } else if constexpr (std::is_same_v<Functor, LeftShiftAssign>) {
        return "<<="sv;
    } else if constexpr (std::is_same_v<Functor, RightShiftAssign>) {
        return ">>="sv;
    } else {
        static_assert(false, "Unsupported operator");
    }
}
