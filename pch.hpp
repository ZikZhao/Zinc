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
using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

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
    struct Assign {
        template<typename T>
        T operator() (T& left, T& right) const {
            static Functor func;
            return left = func(left, right);
        }
    };
    template<>
    struct Assign<void> {
        template<typename T>
        T operator() (T& left, T& right) const {
            return left = right;
        }
    };
    using AddAssign = Assign<Add>;
    using SubtractAssign = Assign<Subtract>;
    using MultiplyAssign = Assign<Multiply>;
    using DivideAssign = Assign<Divide>;
    using RemainderAssign = Assign<Remainder>;
    using BitwiseAndAssign = Assign<BitwiseAnd>;
    using BitwiseOrAssign  = Assign<BitwiseOr>;
    using BitwiseXorAssign = Assign<BitwiseXor>;
    using LeftShiftAssign  = Assign<LeftShift>;
    using RightShiftAssign = Assign<RightShift>;
}

template<uint64_t length>
class FixedString {
public:
    char str[length];
    constexpr FixedString(const char (&str)[length]) {
        std::copy_n(str, length, this->str);
    }
    constexpr std::string_view operator * () const {
        return std::string_view(str, length);
    }
};

template<typename Functor>
constexpr std::string_view GetOperatorString() {
    using namespace OperatorFunctors;
    if constexpr (std::is_same_v<Functor, Add>) {
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
    } else if constexpr (std::is_same_v<Functor, Assign<>>) {
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
