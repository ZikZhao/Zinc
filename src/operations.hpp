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

class IntrinsicOpTable {
private:
    /// Placeholder for right operand absence
    struct NoOperand : public Value {
        static constexpr Kind kind = Kind::NothingOrUnknown;
        NoOperand() = delete;
        NoOperand(const NoOperand&) = delete;
        NoOperand& operator=(const NoOperand&) = delete;
    };

    /// Placeholder to ensure Kind enum can be directly used as array index
    struct AnyValue : public Value {
        static constexpr Kind kind = Kind::Any;
        AnyValue() = delete;
        AnyValue(const AnyValue&) = delete;
        AnyValue& operator=(const AnyValue&) = delete;
    };

    using AllOperations = std::tuple<
        OperatorFunctors::Add,
        OperatorFunctors::Subtract,
        OperatorFunctors::Negate,
        OperatorFunctors::Multiply,
        OperatorFunctors::Divide,
        OperatorFunctors::Remainder,
        OperatorFunctors::Increment,
        OperatorFunctors::Decrement,
        OperatorFunctors::Equal,
        OperatorFunctors::NotEqual,
        OperatorFunctors::LessThan,
        OperatorFunctors::LessEqual,
        OperatorFunctors::GreaterThan,
        OperatorFunctors::GreaterEqual,
        OperatorFunctors::LogicalAnd,
        OperatorFunctors::LogicalOr,
        OperatorFunctors::LogicalNot,
        OperatorFunctors::BitwiseAnd,
        OperatorFunctors::BitwiseOr,
        OperatorFunctors::BitwiseXor,
        OperatorFunctors::BitwiseNot,
        OperatorFunctors::LeftShift,
        OperatorFunctors::RightShift,
        OperatorFunctors::Assign>;

    using AllPrimitiveValues = std::
        tuple<NoOperand, AnyValue, NullValue, IntegerValue, FloatValue, StringValue, BooleanValue>;

public:
    using OperatorFn = ValuePtr (*)(ValuePtr, ValuePtr);
    using TableValue = std::pair<Kind, OperatorFn>;

private:
    template <typename Op, typename Left, typename Right>
    static consteval TableValue make_single_op() {
        constexpr bool valid =
            requires { Op{}(std::declval<const Left&>(), std::declval<const Right&>()).kind; };
        if constexpr (valid) {
            return TableValue{
                decltype(Op{}(std::declval<const Left&>(), std::declval<const Right&>()))::kind,
                [](ValuePtr left, ValuePtr right) -> ValuePtr {
                    auto result =
                        Op{}(static_cast<const Left&>(*left), static_cast<const Right&>(*right));
                    return new decltype(result)(std::move(result));
                },
            };
        } else {
            return TableValue{Kind::NothingOrUnknown, nullptr};
        }
    }

    static constexpr const auto& operation_table() {
        static constexpr auto table = []<typename... Ops>(std::type_identity<std::tuple<Ops...>>) {
            return std::array{[]<typename... Ls>(std::type_identity<std::tuple<Ls...>>) {
                using Op = Ops;
                return std::array{[]<typename... Rs>(std::type_identity<std::tuple<Rs...>>) {
                    using L = Ls;
                    return std::array{make_single_op<Op, L, Rs>()...};
                }(std::type_identity<AllPrimitiveValues>{})...};
            }(std::type_identity<AllPrimitiveValues>{})...};
        }(std::type_identity<AllOperations>{});
        static_assert(table.size() == static_cast<std::size_t>(OperatorCode::SIZE));
        static_assert(table[0].size() == static_cast<std::size_t>(Kind::NonCompositeSize));
        static_assert(table[0][0].size() == static_cast<std::size_t>(Kind::NonCompositeSize));

        return table;
    }

protected:
    TypeRegistry& types_;

public:
    constexpr IntrinsicOpTable(TypeRegistry& types) : types_(types) {}

    constexpr ObjectPtr eval_op(
        OperatorCode opcode, ObjectPtr left, ObjectPtr right = &UnknownType::instance
    ) const {
        if (left->as_type() && right->as_type()) {
            return eval_type_op(opcode, left->as_type(), right->as_type());
        } else if (left->as_value() && right->as_value()) {
            return eval_value_op(opcode, left->as_value(), right->as_value());
        } else {
            return eval_type_value_op(opcode, left, right);
        }
    }

    TypePtr get_result_type(
        OperatorCode opcode, TypePtr left_type, TypePtr right_type = &UnknownType::instance
    ) const {
        const TableValue& value = operation_table()[static_cast<std::size_t>(opcode)]
                                                   [static_cast<std::size_t>(left_type->kind_)]
                                                   [static_cast<std::size_t>(right_type->kind_)];
        if (value.first == Kind::NothingOrUnknown) {
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                "", left_type->repr(), right_type ? right_type->repr() : ""
            );
        }
        switch (value.first) {
        case Kind::Integer: {
            bool is_signed =
                static_cast<const IntegerType*>(left_type)->is_signed_ ||
                (right_type && static_cast<const IntegerType*>(right_type)->is_signed_);
            std::uint8_t bytes = static_cast<const IntegerType*>(left_type)->bits_ >
                                 static_cast<const IntegerType*>(right_type)->bits_;
            return types_.get<PrimitiveType<Kind::Integer>>(is_signed, bytes);
        }
        case Kind::Float: {
            std::uint8_t bytes = static_cast<const FloatType*>(left_type)->bits_ >
                                 static_cast<const FloatType*>(right_type)->bits_;
            return types_.get<PrimitiveType<Kind::Float>>(bytes);
        }
        case Kind::String:
            return types_.get<PrimitiveType<Kind::String>>();
        case Kind::Boolean:
            return types_.get<PrimitiveType<Kind::Boolean>>();
        default:
            assert(false && "Unsupported result type kind");
            std::unreachable();
        }
    }

private:
    ValuePtr eval_value_op(OperatorCode opcode, ValuePtr left, ValuePtr right) const {
        const TableValue& value = operation_table()[static_cast<std::size_t>(opcode)]
                                                   [static_cast<std::size_t>(left->kind_)]
                                                   [static_cast<std::size_t>(right->kind_)];
        if (value.second == nullptr) {
            throw UnlocatedProblem::make<OperationNotDefinedError>("", left->repr(), right->repr());
        }
        return value.second(left, right);
    }

    TypePtr eval_type_op(OperatorCode opcode, TypePtr left, TypePtr right) const {
        switch (opcode) {
        case OperatorCode::OPERATOR_BITWISE_AND:
            return types_.get<IntersectionType>(left, right);
        case OperatorCode::OPERATOR_BITWISE_OR:
            return types_.get<UnionType>(left, right);
        default:
            throw UnlocatedProblem::make<OperationNotDefinedError>("", left->repr(), right->repr());
        }
    }

    Type* eval_type_value_op(OperatorCode opcode, ObjectPtr left, ObjectPtr right) const {
        // TODO (array index, integer + 1, etc.)
        return {};
    }
};

class OpDispatcher final : private IntrinsicOpTable {
private:
    using CustomTableKey = std::tuple<OperatorCode, TypePtr, TypePtr>;
    using CustomTableValue = std::pair<TypePtr, OperatorFn>;
    FlatMap<CustomTableKey, CustomTableValue> custom_table_;

public:
    OpDispatcher(TypeRegistry& types) : IntrinsicOpTable(types), custom_table_() {}

    void register_custom_op(
        OperatorCode opcode,
        TypePtr left_type,
        TypePtr right_type,
        TypePtr result_type,
        OperatorFn func
    ) {
        custom_table_[{opcode, left_type, right_type}] = {result_type, func};
    }

    ObjectPtr eval_op(OperatorCode opcode, ObjectPtr left, ObjectPtr right = {}) const {
        bool both_types = left->as_type() && right->as_type();
        bool both_primitive_values = left->as_value() && right->as_value() &&
                                     left->as_value()->kind_ < Kind::NonCompositeSize &&
                                     right->as_value()->kind_ < Kind::NonCompositeSize;
        if (both_types || both_primitive_values) {
            return IntrinsicOpTable::eval_op(opcode, left, right);
        }
        // operation on values only
        ValuePtr left_value = left->as_value();
        ValuePtr right_value = right->as_value();
        auto it = custom_table_.find({opcode, left_value->get_type(), right_value->get_type()});
        if (it != custom_table_.end()) {
            return it->second.second(left_value, right_value);
        } else {
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                "", left_value->repr(), right_value->repr()
            );
        }
    }

    TypePtr get_result_type(OperatorCode opcode, TypePtr left_type, TypePtr right_type = {}) const {
        if (left_type->kind_ < Kind::NonCompositeSize &&
            (right_type ? right_type->kind_ < Kind::NonCompositeSize : true)) {
            return IntrinsicOpTable::get_result_type(opcode, left_type, right_type);
        }
        auto it = custom_table_.find({opcode, left_type, right_type});
        if (it != custom_table_.end()) {
            return (*it).second.first;
        } else {
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                "", left_type->repr(), right_type ? right_type->repr() : ""
            );
        }
    }
};
