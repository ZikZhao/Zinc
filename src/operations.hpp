#pragma once
#include "pch.hpp"
#include <StainlessParser.h>
#include <type_traits>
#include <utility>

#include "diagnosis.hpp"
#include "object.hpp"

enum class OperatorCode : std::uint16_t {
    OPERATOR_ADD,
    OPERATOR_SUBTRACT,
    OPERATOR_NEGATE,
    OPERATOR_MULTIPLY,
    OPERATOR_DIVIDE,
    OPERATOR_REMAINDER,
    OPERATOR_INCREMENT,
    OPERATOR_DECREMENT,
    OPERATOR_EQUAL,
    OPERATOR_NOT_EQUAL,
    OPERATOR_LESS_THAN,
    OPERATOR_LESS_EQUAL,
    OPERATOR_GREATER_THAN,
    OPERATOR_GREATER_EQUAL,
    OPERATOR_LOGICAL_AND,
    OPERATOR_LOGICAL_OR,
    OPERATOR_LOGICAL_NOT,
    OPERATOR_BITWISE_AND,
    OPERATOR_BITWISE_OR,
    OPERATOR_BITWISE_XOR,
    OPERATOR_BITWISE_NOT,
    OPERATOR_LEFT_SHIFT,
    OPERATOR_RIGHT_SHIFT,
    OPERATOR_ASSIGN,
    SIZE,
    OPERATOR_ADD_ASSIGN,
    OPERATOR_SUBTRACT_ASSIGN,
    OPERATOR_MULTIPLY_ASSIGN,
    OPERATOR_DIVIDE_ASSIGN,
    OPERATOR_REMAINDER_ASSIGN,
    OPERATOR_LOGICAL_AND_ASSIGN,
    OPERATOR_LOGICAL_OR_ASSIGN,
    OPERATOR_BITWISE_AND_ASSIGN,
    OPERATOR_BITWISE_OR_ASSIGN,
    OPERATOR_BITWISE_XOR_ASSIGN,
    OPERATOR_LEFT_SHIFT_ASSIGN,
    OPERATOR_RIGHT_SHIFT_ASSIGN,
};

constexpr std::string_view OperatorCodeToString(OperatorCode opcode) {
    switch (opcode) {
    case OperatorCode::OPERATOR_ADD:
        return "+";
    case OperatorCode::OPERATOR_SUBTRACT:
        return "-";
    case OperatorCode::OPERATOR_NEGATE:
        return "-";
    case OperatorCode::OPERATOR_MULTIPLY:
        return "*";
    case OperatorCode::OPERATOR_DIVIDE:
        return "/";
    case OperatorCode::OPERATOR_REMAINDER:
        return "%";
    case OperatorCode::OPERATOR_INCREMENT:
        return "++";
    case OperatorCode::OPERATOR_DECREMENT:
        return "--";
    case OperatorCode::OPERATOR_EQUAL:
        return "==";
    case OperatorCode::OPERATOR_NOT_EQUAL:
        return "!=";
    case OperatorCode::OPERATOR_LESS_THAN:
        return "<";
    case OperatorCode::OPERATOR_LESS_EQUAL:
        return "<=";
    case OperatorCode::OPERATOR_GREATER_THAN:
        return ">";
    case OperatorCode::OPERATOR_GREATER_EQUAL:
        return ">=";
    case OperatorCode::OPERATOR_LOGICAL_AND:
        return "&&";
    case OperatorCode::OPERATOR_LOGICAL_OR:
        return "||";
    case OperatorCode::OPERATOR_LOGICAL_NOT:
        return "!";
    case OperatorCode::OPERATOR_BITWISE_AND:
        return "&";
    case OperatorCode::OPERATOR_BITWISE_OR:
        return "|";
    case OperatorCode::OPERATOR_BITWISE_XOR:
        return "^";
    case OperatorCode::OPERATOR_BITWISE_NOT:
        return "~";
    case OperatorCode::OPERATOR_LEFT_SHIFT:
        return "<<";
    case OperatorCode::OPERATOR_RIGHT_SHIFT:
        return ">>";
    case OperatorCode::OPERATOR_ASSIGN:
        return "=";
    case OperatorCode::OPERATOR_ADD_ASSIGN:
        return "+=";
    case OperatorCode::OPERATOR_SUBTRACT_ASSIGN:
        return "-=";
    case OperatorCode::OPERATOR_MULTIPLY_ASSIGN:
        return "*=";
    case OperatorCode::OPERATOR_DIVIDE_ASSIGN:
        return "/=";
    case OperatorCode::OPERATOR_REMAINDER_ASSIGN:
        return "%=";
    case OperatorCode::OPERATOR_LOGICAL_AND_ASSIGN:
        return "&&=";
    case OperatorCode::OPERATOR_LOGICAL_OR_ASSIGN:
        return "||=";
    case OperatorCode::OPERATOR_BITWISE_AND_ASSIGN:
        return "&=";
    case OperatorCode::OPERATOR_BITWISE_OR_ASSIGN:
        return "|=";
    case OperatorCode::OPERATOR_BITWISE_XOR_ASSIGN:
        return "^=";
    case OperatorCode::OPERATOR_LEFT_SHIFT_ASSIGN:
        return "<<=";
    case OperatorCode::OPERATOR_RIGHT_SHIFT_ASSIGN:
        return ">>=";
    default:
        assert(false && "Unknown operator code");
        std::unreachable();
    }
}

namespace OperatorFunctors {

struct Increment {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_INCREMENT;
    template <typename T>
    auto operator()(const T& value) const {
        return value++;
    }
};

struct Decrement {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_DECREMENT;
    template <typename T>
    auto operator()(const T& value) const {
        return value--;
    }
};

struct LeftShift {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_LEFT_SHIFT;
    template <typename T>
        requires requires { std::declval<T>() << std::declval<T>(); }
    auto operator()(const T& left, const T& right) const {
        return left << right;
    }
};

struct RightShift {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_RIGHT_SHIFT;
    template <typename T>
        requires requires { std::declval<T>() >> std::declval<T>(); }
    auto operator()(const T& left, const T& right) const {
        return left >> right;
    }
};

template <typename Functor = void>
struct OperateAndAssign {
    static constexpr OperatorCode opcode = Functor::opcode;
    template <typename Left, typename Right>
        requires requires { Functor{}(std::declval<Left&>(), std::declval<const Right&>()); }
    auto operator()(Left& left, const Right& right) const {
        Functor func;
        using ResultType = std::remove_pointer_t<decltype(func(left, right))>;
        std::unique_ptr<ResultType> result = std::make_unique<ResultType>(func(left, right));
        left = *result;
        return left;
    }
};

template <>
struct OperateAndAssign<void> {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_ASSIGN;
    template <typename Left, typename Right>
        requires requires { std::declval<Left&>() = std::declval<Right&>(); }
    auto operator()(Left& left, const Right& right) const {
        return left = right;
    }
};

template <typename Functor, OperatorCode Tag>
struct TaggedFunctor {
    static constexpr OperatorCode opcode = Tag;
    template <typename... Args>
        requires requires { Functor{}(std::declval<Args>()...); }
    auto operator()(Args&&... args) const {
        return Functor{}(std::forward<Args>(args)...);
    }
};

using Add = TaggedFunctor<std::plus<>, OperatorCode::OPERATOR_ADD>;
using Subtract = TaggedFunctor<std::minus<>, OperatorCode::OPERATOR_SUBTRACT>;
using Negate = TaggedFunctor<std::negate<>, OperatorCode::OPERATOR_NEGATE>;
using Multiply = TaggedFunctor<std::multiplies<>, OperatorCode::OPERATOR_MULTIPLY>;
using Divide = TaggedFunctor<std::divides<>, OperatorCode::OPERATOR_DIVIDE>;
using Remainder = TaggedFunctor<std::modulus<>, OperatorCode::OPERATOR_REMAINDER>;
using Equal = TaggedFunctor<std::equal_to<>, OperatorCode::OPERATOR_EQUAL>;
using NotEqual = TaggedFunctor<std::not_equal_to<>, OperatorCode::OPERATOR_NOT_EQUAL>;
using LessThan = TaggedFunctor<std::less<>, OperatorCode::OPERATOR_LESS_THAN>;
using LessEqual = TaggedFunctor<std::less_equal<>, OperatorCode::OPERATOR_LESS_EQUAL>;
using GreaterThan = TaggedFunctor<std::greater<>, OperatorCode::OPERATOR_GREATER_THAN>;
using GreaterEqual = TaggedFunctor<std::greater_equal<>, OperatorCode::OPERATOR_GREATER_EQUAL>;
using LogicalAnd = TaggedFunctor<std::logical_and<>, OperatorCode::OPERATOR_LOGICAL_AND>;
using LogicalOr = TaggedFunctor<std::logical_or<>, OperatorCode::OPERATOR_LOGICAL_OR>;
using LogicalNot = TaggedFunctor<std::logical_not<>, OperatorCode::OPERATOR_LOGICAL_NOT>;
using BitwiseAnd = TaggedFunctor<std::bit_and<>, OperatorCode::OPERATOR_BITWISE_AND>;
using BitwiseOr = TaggedFunctor<std::bit_or<>, OperatorCode::OPERATOR_BITWISE_OR>;
using BitwiseXor = TaggedFunctor<std::bit_xor<>, OperatorCode::OPERATOR_BITWISE_XOR>;
using BitwiseNot = TaggedFunctor<std::bit_not<>, OperatorCode::OPERATOR_BITWISE_NOT>;
using Assign = OperateAndAssign<>;
using AddAssign = OperateAndAssign<Add>;
using SubtractAssign = OperateAndAssign<Subtract>;
using MultiplyAssign = OperateAndAssign<Multiply>;
using DivideAssign = OperateAndAssign<Divide>;
using RemainderAssign = OperateAndAssign<Remainder>;
using LogicalAndAssign = OperateAndAssign<LogicalAnd>;
using LogicalOrAssign = OperateAndAssign<LogicalOr>;
using BitwiseAndAssign = OperateAndAssign<BitwiseAnd>;
using BitwiseOrAssign = OperateAndAssign<BitwiseOr>;
using BitwiseXorAssign = OperateAndAssign<BitwiseXor>;
using LeftShiftAssign = OperateAndAssign<LeftShift>;
using RightShiftAssign = OperateAndAssign<RightShift>;
}  // namespace OperatorFunctors

class OperationHandler final {
private:
    GlobalMemory::Map<std::tuple<OperatorCode, Type*, Type*>, FunctionValue*> custom_table_;

public:
    void register_custom_op(
        OperatorCode opcode, Type* left_type, Type* right_type, FunctionValue* func
    ) {
        custom_table_[{opcode, left_type, right_type}] = func;
    }

    Object* eval_op(OperatorCode opcode, Object* left, Object* right = {}) const {
        bool any_is_type = left->as_type() || right->as_type();
        if (any_is_type) {
            return eval_type_op(opcode, left, right);
        }
        Value* left_value = left->as_value();
        Value* right_value = right->as_value();
        auto it = custom_table_.find({opcode, left_value->get_type(), right_value->get_type()});
        if (it != custom_table_.end()) {
            return it->second->invoke(GlobalMemory::pack_array(left_value, right_value));
        } else {
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                "", left_value->repr(), right_value->repr()
            );
        }
    }

    Type* get_result_type(OperatorCode opcode, Type* left_type, Type* right_type = {}) const {
        auto it = custom_table_.find({opcode, left_type, right_type});
        if (it != custom_table_.end()) {
            return (*it).second->type_->return_type_;
        } else {
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                "", left_type->repr(), right_type ? right_type->repr() : ""
            );
        }
    }

private:
    Type* eval_type_op(OperatorCode opcode, Object* left, Object* right = {}) const {
        /// TODO: implement type-level operations
        return nullptr;
    }
};
