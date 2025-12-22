#pragma once
#include "pch.hpp"
#include <StainlessParser.h>
#include <utility>

#include "entity.hpp"

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
    auto operator()(const T& left, const T& right) const {
        return left << right;
    }
};

struct RightShift {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_RIGHT_SHIFT;
    template <typename T>
    auto operator()(const T& left, const T& right) const {
        return left >> right;
    }
};

template <typename Functor = void>
struct OperateAndAssign {
    static constexpr OperatorCode opcode = Functor::opcode;
    template <typename Left, typename Right>
    auto operator()(Left& left, const Right& right) const {
        Functor func;
        left = func(left, right);
        return left;
    }
};

template <>
struct OperateAndAssign<void> {
    static constexpr OperatorCode opcode = OperatorCode::OPERATOR_ASSIGN;
    template <typename Left, typename Right>
    auto operator()(const Left& left, const Right& right) const {
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

class FundamentalOperationTable {
private:
    using Arithmetic = std::tuple<
        OperatorFunctors::Add,
        OperatorFunctors::Subtract,
        OperatorFunctors::Negate,
        OperatorFunctors::Multiply,
        OperatorFunctors::Divide,
        OperatorFunctors::Remainder>;
    using Comparison = std::tuple<
        OperatorFunctors::Equal,
        OperatorFunctors::NotEqual,
        OperatorFunctors::LessThan,
        OperatorFunctors::LessEqual,
        OperatorFunctors::GreaterThan,
        OperatorFunctors::GreaterEqual>;
    using Bitwise = std::tuple<
        OperatorFunctors::BitwiseAnd,
        OperatorFunctors::BitwiseOr,
        OperatorFunctors::BitwiseXor,
        OperatorFunctors::BitwiseNot>;

public:
    using OperatorFn = Entity* (*)(Entity*, Entity*);
    using TableKey = std::tuple<OperatorCode, Kind, Kind>;
    using TableValue = std::pair<Kind, OperatorFn>;
    using TypeAndFunctor = std::pair<EntityRef, OperatorFn>;

private:
    using Table = std::array<
        std::array<
            std::array<TableValue, static_cast<std::size_t>(Kind::NON_COMPOSITE_SIZE)>,
            static_cast<std::size_t>(Kind::NON_COMPOSITE_SIZE)>,
        static_cast<std::size_t>(OperatorCode::SIZE)>;

private:
    static constexpr Table make_operation_map() {
        Table table;
        register_group<IntegerValue>(table, Arithmetic{});
        register_group<FloatValue>(table, Arithmetic{});
        register_group<IntegerValue>(table, Comparison{});
        register_group<FloatValue>(table, Comparison{});
        register_group<IntegerValue>(table, Bitwise{});
        return table;
    }
    template <typename Operand, typename... Operators>
    static constexpr void register_group(Table& table, std::tuple<Operators...>) {
        (register_op<Operand, Operators>(table), ...);
    }
    template <typename Operand, typename Operator>
        requires requires { Operator()(std::declval<Operand&>(), std::declval<Operand&>()); }
    static constexpr void register_op(Table& table) {
        table[static_cast<std::size_t>(Operator::opcode)][static_cast<std::size_t>(Operand::kind)]
             [static_cast<std::size_t>(Operand::kind)] = TableValue{
                 Operand::kind,
                 [](Entity* left, Entity* right) -> Entity* {
                     return Operator()(
                         static_cast<Operand&>(*left), static_cast<const Operand&>(*right)
                     );
                 },
             };
    }
    template <typename Operand, typename Operator>
        requires requires { Operator()(std::declval<Operand&>()); }
    static constexpr void register_op(Table& table) {
        table[static_cast<std::size_t>(Operator::opcode)][static_cast<std::size_t>(Operand::kind)]
             [static_cast<std::size_t>(Operand::kind)] = TableValue{
                 Operand::kind,
                 [](Entity* left, Entity* right) -> Entity* {
                     return Operator()(static_cast<Operand&>(*left));
                 },
             };
    }

protected:
    TypeFactory& type_factory_;
    const Table table_;

public:
    constexpr FundamentalOperationTable(TypeFactory& type_factory)
        : type_factory_(type_factory), table_(make_operation_map()) {}

    constexpr TypeAndFunctor eval_value_op(OperatorCode opcode, Type* left, Type* right) const {
        const TableValue& value =
            table_[static_cast<std::size_t>(opcode)][static_cast<std::size_t>(left->kind_)]
                  [static_cast<std::size_t>(right->kind_)];
        return {type_factory_.make_kind(value.first), value.second};
    }

    constexpr TypeAndFunctor eval_op(OperatorCode opcode, Type* left, Type* right = {}) const {
        if (left->is_type() && (right || right->is_type())) {
            // type and type
            switch (opcode) {
            case OperatorCode::OPERATOR_BITWISE_AND:
                return {type_factory_.make<IntersectionType>(left, right), nullptr};
            case OperatorCode::OPERATOR_BITWISE_OR:
                return {type_factory_.make<UnionType>(left, right), nullptr};
            default:
                assert(false && "Operation on two types not implemented");
                std::unreachable();
            }
        } else if (!left->is_type() && (right || !right->is_type())) {
            assert(false && "Operation on two values should be handled elsewhere");
            std::unreachable();
        } else {
            return {};
        }
    }
};

class OperationTable final : public FundamentalOperationTable {
private:
    using CustomTableKey = std::tuple<OperatorCode, Type*, Type*>;
    using CustomTableValue = std::pair<Type*, OperatorFn>;
    std::map<CustomTableKey, CustomTableValue> custom_table_;

public:
    OperationTable(TypeFactory& type_factory)
        : FundamentalOperationTable(type_factory), custom_table_() {}

    void register_custom_op(
        OperatorCode opcode,
        TypeRef left_type,
        TypeRef right_type,
        TypeRef result_type,
        Entity* (*func)(Entity*, Entity*)
    ) {
        custom_table_[{opcode, left_type.type(), right_type.type()}] = {result_type.type(), func};
    }

    TypeAndFunctor eval_op(OperatorCode opcode, Type* left, Type* right = {}) const {
        if (left->kind_ < Kind::NON_COMPOSITE_SIZE &&
            (right || right->kind_ < Kind::NON_COMPOSITE_SIZE)) {
            // primitive and primitive
            return FundamentalOperationTable::eval_op(opcode, left, right);
        }
        auto it = custom_table_.find({opcode, left, right});
        if (it != custom_table_.end()) {
            return {type_factory_.make<Type>(it->second.first), it->second.second};
        } else {
            throw std::runtime_error("Operation not defined for given types");
        }
    }
};
