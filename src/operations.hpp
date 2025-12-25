#pragma once
#include "pch.hpp"
#include <StainlessParser.h>
#include <type_traits>
#include <utility>

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

constexpr OperatorCode MapOperatorEnum(int enum_value) {
    switch (enum_value) {
    case StainlessParser::OP_ADD:
        return OperatorCode::OPERATOR_ADD;
    case StainlessParser::OP_SUB:
        return OperatorCode::OPERATOR_SUBTRACT;
    case StainlessParser::OP_MUL:
        return OperatorCode::OPERATOR_MULTIPLY;
    case StainlessParser::OP_DIV:
        return OperatorCode::OPERATOR_DIVIDE;
    case StainlessParser::OP_REM:
        return OperatorCode::OPERATOR_REMAINDER;
    case StainlessParser::OP_INC:
        return OperatorCode::OPERATOR_INCREMENT;
    case StainlessParser::OP_DEC:
        return OperatorCode::OPERATOR_DECREMENT;
    case StainlessParser::OP_EQ:
        return OperatorCode::OPERATOR_EQUAL;
    case StainlessParser::OP_NEQ:
        return OperatorCode::OPERATOR_NOT_EQUAL;
    case StainlessParser::OP_LT:
        return OperatorCode::OPERATOR_LESS_THAN;
    case StainlessParser::OP_LTE:
        return OperatorCode::OPERATOR_LESS_EQUAL;
    case StainlessParser::OP_GT:
        return OperatorCode::OPERATOR_GREATER_THAN;
    case StainlessParser::OP_GTE:
        return OperatorCode::OPERATOR_GREATER_EQUAL;
    case StainlessParser::OP_AND:
        return OperatorCode::OPERATOR_LOGICAL_AND;
    case StainlessParser::OP_OR:
        return OperatorCode::OPERATOR_LOGICAL_OR;
    case StainlessParser::OP_NOT:
        return OperatorCode::OPERATOR_LOGICAL_NOT;
    case StainlessParser::OP_BITAND:
        return OperatorCode::OPERATOR_BITWISE_AND;
    case StainlessParser::OP_BITOR:
        return OperatorCode::OPERATOR_BITWISE_OR;
    case StainlessParser::OP_BITXOR:
        return OperatorCode::OPERATOR_BITWISE_XOR;
    case StainlessParser::OP_BITNOT:
        return OperatorCode::OPERATOR_BITWISE_NOT;
    case StainlessParser::OP_LSHIFT:
        return OperatorCode::OPERATOR_LEFT_SHIFT;
    case StainlessParser::OP_RSHIFT:
        return OperatorCode::OPERATOR_RIGHT_SHIFT;
    case StainlessParser::OP_ASSIGN:
        return OperatorCode::OPERATOR_ASSIGN;
    case StainlessParser::OP_ADD_ASSIGN:
        return OperatorCode::OPERATOR_ADD_ASSIGN;
    case StainlessParser::OP_SUB_ASSIGN:
        return OperatorCode::OPERATOR_SUBTRACT_ASSIGN;
    case StainlessParser::OP_MUL_ASSIGN:
        return OperatorCode::OPERATOR_MULTIPLY_ASSIGN;
    case StainlessParser::OP_DIV_ASSIGN:
        return OperatorCode::OPERATOR_DIVIDE_ASSIGN;
    case StainlessParser::OP_REM_ASSIGN:
        return OperatorCode::OPERATOR_REMAINDER_ASSIGN;
    case StainlessParser::OP_AND_ASSIGN:
        return OperatorCode::OPERATOR_LOGICAL_AND_ASSIGN;
    case StainlessParser::OP_OR_ASSIGN:
        return OperatorCode::OPERATOR_LOGICAL_OR_ASSIGN;
    case StainlessParser::OP_BITAND_ASSIGN:
        return OperatorCode::OPERATOR_BITWISE_AND_ASSIGN;
    case StainlessParser::OP_BITOR_ASSIGN:
        return OperatorCode::OPERATOR_BITWISE_OR_ASSIGN;
    case StainlessParser::OP_BITXOR_ASSIGN:
        return OperatorCode::OPERATOR_BITWISE_XOR_ASSIGN;
    case StainlessParser::OP_LSHIFT_ASSIGN:
        return OperatorCode::OPERATOR_LEFT_SHIFT_ASSIGN;
    case StainlessParser::OP_RSHIFT_ASSIGN:
        return OperatorCode::OPERATOR_RIGHT_SHIFT_ASSIGN;
    default:
        assert(false && "Unknown operator enum value");
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
        static constexpr Kind kind = Kind::Nothing;
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
    using OperatorFn = ValueRef (*)(ValueRef, ValueRef);
    using TableValue = std::pair<Kind, OperatorFn>;

private:
    template <typename Op, typename Left, typename Right>
    static consteval TableValue make_single_op() {
        constexpr bool valid =
            requires { Op{}(std::declval<Left&>(), std::declval<Right&>()).kind; };
        if constexpr (valid) {
            return TableValue{
                decltype(Op{}(std::declval<Left&>(), std::declval<Right&>()))::kind,
                [](ValueRef left, ValueRef right) -> ValueRef {
                    auto result = Op{}(static_cast<Left&>(*left), static_cast<Right&>(*right));
                    auto boxed = GlobalMemory::alloc<decltype(result)>(std::move(result));
                    return ValueRef(boxed);
                },
            };
        } else {
            return TableValue{Kind::Nothing, nullptr};
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

    constexpr ObjectRef eval_op(OperatorCode opcode, ObjectRef left, ObjectRef right = {}) const {
        if (left.is_type() && right.is_type()) {
            return eval_type_op(opcode, left.as_type(), right.as_type());
        } else if (left.is_value() && right.is_value()) {
            return eval_value_op(opcode, left.as_value(), right.as_value());
        } else {
            return eval_type_value_op(opcode, left, right);
        }
    }

    TypeRef get_result_type(OperatorCode opcode, TypeRef left_type, TypeRef right_type) const {
        const TableValue& value = operation_table()[static_cast<std::size_t>(opcode)]
                                                   [static_cast<std::size_t>(left_type->kind_)]
                                                   [static_cast<std::size_t>(right_type->kind_)];
        return types_.get_kind(value.first);
    }

private:
    ValueRef eval_value_op(OperatorCode opcode, ValueRef left, ValueRef right) const {
        const TableValue& value = operation_table()[static_cast<std::size_t>(opcode)]
                                                   [static_cast<std::size_t>(left->kind_)]
                                                   [static_cast<std::size_t>(right->kind_)];
        return value.second(left, right);
    }

    TypeRef eval_type_op(OperatorCode opcode, TypeRef left, TypeRef right) const {
        switch (opcode) {
        case OperatorCode::OPERATOR_BITWISE_AND:
            return types_.get<IntersectionType>(left, right);
        case OperatorCode::OPERATOR_BITWISE_OR:
            return types_.get<UnionType>(left, right);
        default:
            assert(false && "Unsupported operation on types");
            std::unreachable();
        }
    }

    TypeRef eval_type_value_op(OperatorCode opcode, ObjectRef left, ObjectRef right) const {
        // TODO (array index, integer + 1, etc.)
        return {};
    }
};

class OpDispatcher final : private IntrinsicOpTable {
private:
    using CustomTableKey = std::tuple<OperatorCode, Type*, Type*>;
    using CustomTableValue = std::pair<TypeRef, OperatorFn>;
    FlatMap<CustomTableKey, CustomTableValue> custom_table_;

public:
    OpDispatcher(TypeRegistry& types) : IntrinsicOpTable(types), custom_table_() {}

    void register_custom_op(
        OperatorCode opcode,
        TypeRef left_type,
        TypeRef right_type,
        TypeRef result_type,
        OperatorFn func
    ) {
        custom_table_[{opcode, left_type, right_type}] = {result_type, func};
    }

    ObjectRef eval_op(OperatorCode opcode, ObjectRef left, ObjectRef right = {}) const {
        bool both_types = left.is_type() && right.is_type();
        bool both_primitive_values = left.is_value() && right.is_value() &&
                                     left.as_value()->kind_ < Kind::NonCompositeSize &&
                                     right.as_value()->kind_ < Kind::NonCompositeSize;
        if (both_types || both_primitive_values) {
            return IntrinsicOpTable::eval_op(opcode, left, right);
        }
        // operation on values only
        ValueRef left_value = left.as_value();
        ValueRef right_value = right.as_value();
        auto it = custom_table_.find({opcode, types_.of(left_value), types_.of(right_value)});
        if (it != custom_table_.end()) {
            return it->second.second(left_value, right_value);
        } else {
            throw std::runtime_error("Operation not defined for given types");
        }
    }

    TypeRef get_result_type(OperatorCode opcode, TypeRef left_type, TypeRef right_type = {}) const {
        if (left_type->kind_ < Kind::NonCompositeSize &&
            (right_type ? right_type->kind_ < Kind::NonCompositeSize : true)) {
            return IntrinsicOpTable::get_result_type(opcode, left_type, right_type);
        }
        auto it = custom_table_.find({opcode, left_type, right_type});
        if (it != custom_table_.end()) {
            return (*it).second.first;
        } else {
            throw std::runtime_error("Operation not defined for given types");
        }
    }
};
