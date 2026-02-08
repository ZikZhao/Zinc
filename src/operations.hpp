#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"
#include "object.hpp"

enum class OperatorCode : std::uint16_t {
    Add,
    Subtract,
    Negate,
    Multiply,
    Divide,
    Remainder,
    Increment,
    Decrement,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    LogicalNot,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseNot,
    LeftShift,
    RightShift,
    Assign,
    SIZE,
    AddAssign,
    SubtractAssign,
    MultiplyAssign,
    DivideAssign,
    RemainderAssign,
    LogicalAndAssign,
    LogicalOrAssign,
    BitwiseAndAssign,
    BitwiseOrAssign,
    BitwiseXorAssign,
    LeftShiftAssign,
    RightShiftAssign,
};

enum OperatorGroup : std::uint8_t {
    Arithmetic,
    Comparison,
    Logical,
    Bitwise,
    Assignment,
    UnaryArithmetic,
    UnaryLogical,
    UnaryBitwise,
};

namespace OperatorFunctors {

struct Increment {
    static constexpr OperatorCode opcode = OperatorCode::Increment;
    template <typename T>
    auto operator()(const T& value) const {
        return value++;
    }
};

struct Decrement {
    static constexpr OperatorCode opcode = OperatorCode::Decrement;
    template <typename T>
    auto operator()(const T& value) const {
        return value--;
    }
};

struct LeftShift {
    static constexpr OperatorCode opcode = OperatorCode::LeftShift;
    template <typename T>
        requires requires { std::declval<T>() << std::declval<T>(); }
    auto operator()(const T& left, const T& right) const {
        return left << right;
    }
};

struct RightShift {
    static constexpr OperatorCode opcode = OperatorCode::RightShift;
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
    static constexpr OperatorCode opcode = OperatorCode::Assign;
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

using Add = TaggedFunctor<std::plus<>, OperatorCode::Add>;
using Subtract = TaggedFunctor<std::minus<>, OperatorCode::Subtract>;
using Negate = TaggedFunctor<std::negate<>, OperatorCode::Negate>;
using Multiply = TaggedFunctor<std::multiplies<>, OperatorCode::Multiply>;
using Divide = TaggedFunctor<std::divides<>, OperatorCode::Divide>;
using Remainder = TaggedFunctor<std::modulus<>, OperatorCode::Remainder>;
using Equal = TaggedFunctor<std::equal_to<>, OperatorCode::Equal>;
using NotEqual = TaggedFunctor<std::not_equal_to<>, OperatorCode::NotEqual>;
using LessThan = TaggedFunctor<std::less<>, OperatorCode::LessThan>;
using LessEqual = TaggedFunctor<std::less_equal<>, OperatorCode::LessEqual>;
using GreaterThan = TaggedFunctor<std::greater<>, OperatorCode::GreaterThan>;
using GreaterEqual = TaggedFunctor<std::greater_equal<>, OperatorCode::GreaterEqual>;
using LogicalAnd = TaggedFunctor<std::logical_and<>, OperatorCode::LogicalAnd>;
using LogicalOr = TaggedFunctor<std::logical_or<>, OperatorCode::LogicalOr>;
using LogicalNot = TaggedFunctor<std::logical_not<>, OperatorCode::LogicalNot>;
using BitwiseAnd = TaggedFunctor<std::bit_and<>, OperatorCode::BitwiseAnd>;
using BitwiseOr = TaggedFunctor<std::bit_or<>, OperatorCode::BitwiseOr>;
using BitwiseXor = TaggedFunctor<std::bit_xor<>, OperatorCode::BitwiseXor>;
using BitwiseNot = TaggedFunctor<std::bit_not<>, OperatorCode::BitwiseNot>;
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

constexpr OperatorGroup GetOperatorGroup(OperatorCode opcode) {
    switch (opcode) {
    case OperatorCode::Add:
    case OperatorCode::Subtract:
    case OperatorCode::Multiply:
    case OperatorCode::Divide:
    case OperatorCode::Remainder:
        return OperatorGroup::Arithmetic;
    case OperatorCode::Negate:
    case OperatorCode::Increment:
    case OperatorCode::Decrement:
        return OperatorGroup::UnaryArithmetic;
    case OperatorCode::Equal:
    case OperatorCode::NotEqual:
    case OperatorCode::LessThan:
    case OperatorCode::LessEqual:
    case OperatorCode::GreaterThan:
    case OperatorCode::GreaterEqual:
        return OperatorGroup::Comparison;
    case OperatorCode::LogicalAnd:
    case OperatorCode::LogicalOr:
        return OperatorGroup::Logical;
    case OperatorCode::LogicalNot:
        return OperatorGroup::UnaryLogical;
    case OperatorCode::BitwiseAnd:
    case OperatorCode::BitwiseOr:
    case OperatorCode::BitwiseXor:
        return OperatorGroup::Bitwise;
    case OperatorCode::BitwiseNot:
        return OperatorGroup::UnaryBitwise;
    case OperatorCode::Assign:
    case OperatorCode::AddAssign:
    case OperatorCode::SubtractAssign:
    case OperatorCode::MultiplyAssign:
    case OperatorCode::DivideAssign:
    case OperatorCode::RemainderAssign:
    case OperatorCode::LogicalAndAssign:
    case OperatorCode::LogicalOrAssign:
    case OperatorCode::BitwiseAndAssign:
    case OperatorCode::BitwiseOrAssign:
    case OperatorCode::BitwiseXorAssign:
    case OperatorCode::LeftShiftAssign:
    case OperatorCode::RightShiftAssign:
        return OperatorGroup::Assignment;
    default:
        UNREACHABLE();
    }
};

constexpr OperatorCode GetAssignmentEquivalent(OperatorCode opcode) {
    switch (opcode) {
    case OperatorCode::AddAssign:
        return OperatorCode::Add;
    case OperatorCode::SubtractAssign:
        return OperatorCode::Subtract;
    case OperatorCode::MultiplyAssign:
        return OperatorCode::Multiply;
    case OperatorCode::DivideAssign:
        return OperatorCode::Divide;
    case OperatorCode::RemainderAssign:
        return OperatorCode::Remainder;
    case OperatorCode::LogicalAndAssign:
        return OperatorCode::LogicalAnd;
    case OperatorCode::LogicalOrAssign:
        return OperatorCode::LogicalOr;
    case OperatorCode::BitwiseAndAssign:
        return OperatorCode::BitwiseAnd;
    case OperatorCode::BitwiseOrAssign:
        return OperatorCode::BitwiseOr;
    case OperatorCode::BitwiseXorAssign:
        return OperatorCode::BitwiseXor;
    case OperatorCode::LeftShiftAssign:
        return OperatorCode::LeftShift;
    case OperatorCode::RightShiftAssign:
        return OperatorCode::RightShift;
    default:
        UNREACHABLE();
    }
}

constexpr std::string_view GetOperatorString(OperatorCode opcode) {
    switch (opcode) {
    case OperatorCode::Add:
        return "+";
    case OperatorCode::Subtract:
        return "-";
    case OperatorCode::Negate:
        return "-";
    case OperatorCode::Multiply:
        return "*";
    case OperatorCode::Divide:
        return "/";
    case OperatorCode::Remainder:
        return "%";
    case OperatorCode::Increment:
        return "++";
    case OperatorCode::Decrement:
        return "--";
    case OperatorCode::Equal:
        return "==";
    case OperatorCode::NotEqual:
        return "!=";
    case OperatorCode::LessThan:
        return "<";
    case OperatorCode::LessEqual:
        return "<=";
    case OperatorCode::GreaterThan:
        return ">";
    case OperatorCode::GreaterEqual:
        return ">=";
    case OperatorCode::LogicalAnd:
        return "&&";
    case OperatorCode::LogicalOr:
        return "||";
    case OperatorCode::LogicalNot:
        return "!";
    case OperatorCode::BitwiseAnd:
        return "&";
    case OperatorCode::BitwiseOr:
        return "|";
    case OperatorCode::BitwiseXor:
        return "^";
    case OperatorCode::BitwiseNot:
        return "~";
    case OperatorCode::LeftShift:
        return "<<";
    case OperatorCode::RightShift:
        return ">>";
    case OperatorCode::Assign:
        return "=";
    case OperatorCode::AddAssign:
        return "+=";
    case OperatorCode::SubtractAssign:
        return "-=";
    case OperatorCode::MultiplyAssign:
        return "*=";
    case OperatorCode::DivideAssign:
        return "/=";
    case OperatorCode::RemainderAssign:
        return "%=";
    case OperatorCode::LogicalAndAssign:
        return "&&=";
    case OperatorCode::LogicalOrAssign:
        return "||=";
    case OperatorCode::BitwiseAndAssign:
        return "&=";
    case OperatorCode::BitwiseOrAssign:
        return "|=";
    case OperatorCode::BitwiseXorAssign:
        return "^=";
    case OperatorCode::LeftShiftAssign:
        return "<<=";
    case OperatorCode::RightShiftAssign:
        return ">>=";
    default:
        UNREACHABLE();
    }
}

class TermSpec {
public:
    std::uintptr_t ptr_;

public:
    TermSpec(Term term) noexcept
        : ptr_(
              reinterpret_cast<std::uintptr_t>(term.effective_type()) |
              static_cast<std::uintptr_t>(term.is_mutable())
          ) {
        static_assert(alignof(Type) >= 8);
        assert(term.effective_type() != nullptr);
    }
    operator bool() const noexcept { return ptr_ != 0; }
    operator const Type*() const noexcept {
        return reinterpret_cast<const Type*>(ptr_ & ~std::uintptr_t(1));
    }
    bool is_mutable() const noexcept { return (ptr_ & 1) != 0; }
};

namespace PrimitiveOperations {

template <OperatorGroup G>
auto apply_op(OperatorCode opcode, auto left, auto right) {
    if constexpr (G == OperatorGroup::Arithmetic) {
        switch (opcode) {
        case OperatorCode::Add:
            return left + right;
        case OperatorCode::Subtract:
            return left - right;
        case OperatorCode::Multiply:
            return left * right;
        case OperatorCode::Divide:
            return left / right;
        case OperatorCode::Remainder:
            if constexpr (std::is_same_v<decltype(left), BigInt>) {
                return left % right;
            } else {
                return std::fmod(left, right);
            }
        default:
            UNREACHABLE();
        }
    } else if constexpr (G == OperatorGroup::Comparison) {
        switch (opcode) {
        case OperatorCode::Equal:
            return left == right;
        case OperatorCode::NotEqual:
            return left != right;
        case OperatorCode::LessThan:
            return left < right;
        case OperatorCode::LessEqual:
            return left <= right;
        case OperatorCode::GreaterThan:
            return left > right;
        case OperatorCode::GreaterEqual:
            return left >= right;
        default:
            UNREACHABLE();
        }
    } else if constexpr (G == OperatorGroup::Logical) {
        switch (opcode) {
        case OperatorCode::LogicalAnd:
            return left && right;
        case OperatorCode::LogicalOr:
            return left || right;
        default:
            UNREACHABLE();
        }
    } else if constexpr (G == OperatorGroup::Bitwise) {
        switch (opcode) {
        case OperatorCode::BitwiseAnd:
            return left & right;
        case OperatorCode::BitwiseOr:
            return left | right;
        case OperatorCode::BitwiseXor:
            return left ^ right;
        default:
            UNREACHABLE();
        }
    } else {
        static_assert(false);
    }
}

template <OperatorGroup G>
auto apply_op(OperatorCode opcode, auto value) {
    if constexpr (G == OperatorGroup::UnaryArithmetic) {
        switch (opcode) {
        case OperatorCode::Negate:
            return -value;
        case OperatorCode::Increment:
            return value + decltype(value)(1ul);
        case OperatorCode::Decrement:
            return value - decltype(value)(1ul);
        default:
            UNREACHABLE();
        }
    } else if constexpr (G == OperatorGroup::UnaryLogical) {
        assert(opcode == OperatorCode::LogicalNot);
        return !value;
    } else if constexpr (G == OperatorGroup::UnaryBitwise) {
        assert(opcode == OperatorCode::BitwiseNot);
        return ~value;
    } else {
        static_assert(false);
    }
}

inline Value* integer_op(OperatorCode opcode, Value* left, Value* right) {
    IntegerValue* left_int = left->cast<IntegerValue>();
    IntegerValue* right_int = right->cast<IntegerValue>();
    bool extended = left_int->type_->bits_ > 32 || right_int->type_->bits_ > 32;
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::Arithmetic:
        return new IntegerValue(
            extended ? &IntegerType::i64_instance : &IntegerType::i32_instance,
            apply_op<OperatorGroup::Arithmetic>(opcode, left_int->value_, right_int->value_)
        );
    case OperatorGroup::Comparison:
        return new BooleanValue(
            apply_op<OperatorGroup::Comparison>(opcode, left_int->value_, right_int->value_)
        );
    case OperatorGroup::Bitwise:
        return new IntegerValue(
            extended ? &IntegerType::i64_instance : &IntegerType::i32_instance,
            apply_op<OperatorGroup::Bitwise>(opcode, left_int->value_, right_int->value_)
        );
    default:
        UNREACHABLE();
    }
}

inline Value* integer_op(OperatorCode opcode, Value* left) {
    IntegerValue* left_int = left->cast<IntegerValue>();
    bool extended = left_int->type_->bits_ > 32;
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::UnaryArithmetic:
        return new IntegerValue(
            extended ? &IntegerType::i64_instance : &IntegerType::i32_instance,
            apply_op<OperatorGroup::UnaryArithmetic>(opcode, left_int->value_)
        );
    case OperatorGroup::UnaryBitwise:
        return new IntegerValue(
            extended ? &IntegerType::i64_instance : &IntegerType::i32_instance,
            apply_op<OperatorGroup::UnaryBitwise>(opcode, left_int->value_)
        );
    default:
        UNREACHABLE();
    }
}

inline Value* float_op(OperatorCode opcode, Value* left, Value* right) {
    FloatValue* left_float = left->cast<FloatValue>();
    FloatValue* right_float = right->cast<FloatValue>();
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::Arithmetic:
        return new FloatValue(
            left_float->type_->bits_ > 32 ? &FloatType::f64_instance : &FloatType::f32_instance,
            apply_op<OperatorGroup::Arithmetic>(opcode, left_float->value_, right_float->value_)
        );
    case OperatorGroup::Comparison:
        return new BooleanValue(
            apply_op<OperatorGroup::Comparison>(opcode, left_float->value_, right_float->value_)
        );
    default:
        UNREACHABLE();
    }
}

inline Value* float_op(OperatorCode opcode, Value* left) {
    FloatValue* left_float = left->cast<FloatValue>();
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::UnaryArithmetic:
        return new FloatValue(
            left_float->type_->bits_ > 32 ? &FloatType::f64_instance : &FloatType::f32_instance,
            apply_op<OperatorGroup::UnaryArithmetic>(opcode, left_float->value_)
        );
    default:
        UNREACHABLE();
    }
}

inline BooleanValue* boolean_op(OperatorCode opcode, Value* left, Value* right) {
    /// TODO: support equality comparison between booleans
    BooleanValue* left_bool = left->cast<BooleanValue>();
    BooleanValue* right_bool = right->cast<BooleanValue>();
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::Logical:
        return new BooleanValue(
            apply_op<OperatorGroup::Logical>(opcode, left_bool->value_, right_bool->value_)
        );
    default:
        UNREACHABLE();
    }
}

inline BooleanValue* boolean_op(OperatorCode opcode, Value* left) {
    BooleanValue* left_bool = left->cast<BooleanValue>();
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::UnaryLogical:
        return new BooleanValue(apply_op<OperatorGroup::UnaryLogical>(opcode, left_bool->value_));
    default:
        UNREACHABLE();
    }
}

inline Value* assignment_op(OperatorCode opcode, Value* left, Value* right) {
    if (opcode == OperatorCode::Assign) {
        left->assign_from(right);
    } else {
        OperatorCode inner_opcode = GetAssignmentEquivalent(opcode);
        Value* result;
        if (left->kind_ == Kind::Integer && right->kind_ == Kind::Integer) {
            IntegerValue* left_int = left->cast<IntegerValue>();
            IntegerValue* right_int = right->cast<IntegerValue>();
            result = integer_op(inner_opcode, left_int, right_int);
        } else if (left->kind_ == Kind::Float && right->kind_ == Kind::Float) {
            FloatValue* left_float = left->cast<FloatValue>();
            FloatValue* right_float = right->cast<FloatValue>();
            result = float_op(inner_opcode, left_float, right_float);
        } else if (left->kind_ == Kind::Boolean && right->kind_ == Kind::Boolean) {
            BooleanValue* left_bool = left->cast<BooleanValue>();
            BooleanValue* right_bool = right->cast<BooleanValue>();
            result = boolean_op(inner_opcode, left_bool, right_bool);
        } else {
            throw UnlocatedProblem::make<OperationNotDefinedError>("", left->repr(), right->repr());
        }
        left->assign_from(result);
    }
    return left;
}
}  // namespace PrimitiveOperations

class OperationHandler final {
private:
    GlobalMemory::FlatMap<std::tuple<OperatorCode, const Type*, const Type*>, const Object*> map_;
    GlobalMemory::MultiMap<OperatorCode, const Object* (*)(const Type*, const Type*)> templates_;

public:
    void register_custom_op(
        OperatorCode opcode, const Type* left_type, const Type* right_type, const Object* func
    ) {
        map_[{opcode, left_type, right_type}] = func;
    }

    const Type* eval_type_op(
        OperatorCode opcode, const Type* left, const Type* right = nullptr
    ) const {
        /// TODO: implement type-level operations
        return nullptr;
    }

    Term eval_value_op(OperatorCode opcode, Term left, Term right = {}) {
        if (left->kind_ == Kind::Unknown || (right && right->kind_ == Kind::Unknown)) {
            return Term::unknown();
        } else if ((left->kind_ == Kind::Integer || left->kind_ == Kind::Float ||
                    left->kind_ == Kind::Boolean) &&
                   (right ? (right->kind_ == Kind::Integer || right->kind_ == Kind::Float ||
                             right->kind_ == Kind::Boolean)
                          : true)) {
            return eval_primitive_op(opcode, left, right);
        }
        bool comptime = left.is_comptime() && (right && right.is_comptime());
        const Type* left_type = left.effective_type();
        const Type* right_type = right ? right.effective_type() : nullptr;
        auto it = map_.find({opcode, left_type, right_type});
        if (it != map_.end()) {
            if (auto func_value = it->second->dyn_cast<FunctionValue>(); func_value && comptime) {
                return func_value->invoke(GlobalMemory::pack_array(left, right));
            } else {
                auto func_type = it->second->cast<FunctionType>();
                return Term(func_type->return_type_, Term::Category::RValue);
            }
        } else {
            if (const Object* instantiated = try_instantiate(opcode, left_type, right_type)) {
                map_.insert({{opcode, left_type, right_type}, instantiated});
                if (auto func_value = instantiated->dyn_cast<FunctionValue>()) {
                    if (comptime) {
                        return func_value->invoke(GlobalMemory::pack_array(left, right));
                    } else {
                        return Term(func_value->get_type()->return_type_, Term::Category::RValue);
                    }
                } else {
                    auto func_type = instantiated->cast<FunctionType>();
                    return Term(func_type->return_type_, Term::Category::RValue);
                }
            }
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                GetOperatorString(opcode), left->repr(), right ? right->repr() : ""
            );
        }
    }

private:
    const Object* try_instantiate(OperatorCode opcode, const Type* left, const Type* right) const {
        // User-defined operator templates
        auto range = templates_.equal_range(opcode);
        for (auto it = range.first; it != range.second; ++it) {
            const Object* func_obj = it->second(left, right);
            if (func_obj != nullptr) {
                return func_obj;
            }
        }
        return nullptr;
    }

    Term eval_primitive_op(OperatorCode opcode, Term left, Term right) const {
        const Type* left_type = left.effective_type();
        Kind left_kind = left->kind_;
        Kind right_kind = right ? right->kind_ : Kind::Unknown;
        bool comptime = left.is_comptime() && (right ? right.is_comptime() : true);
        if (comptime) {
            Value* left_value = left.comp_var();
            Value* right_value = right ? right.comp_var() : nullptr;
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term(
                        PrimitiveOperations::float_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                assert(!right);
                if (left_kind == Kind::Integer) {
                    return Term(
                        PrimitiveOperations::integer_op(opcode, left_value),
                        Term::Category::CompRValue
                    );
                } else if (left_kind == Kind::Float) {
                    return Term(
                        PrimitiveOperations::float_op(opcode, left_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::Comparison:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term(
                        PrimitiveOperations::float_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term(
                        PrimitiveOperations::boolean_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::UnaryLogical:
                assert(!right);
                if (left_kind == Kind::Boolean) {
                    return Term(
                        PrimitiveOperations::boolean_op(opcode, left_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::UnaryBitwise:
                if (left_kind == Kind::Integer) {
                    return Term(
                        PrimitiveOperations::integer_op(opcode, left_value),
                        Term::Category::CompRValue
                    );
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                if (!left.is_mutable()) break;
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    (left_kind == Kind::Boolean && right_kind == Kind::Boolean)) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            }
        } else {
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float)) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                if (left_kind == Kind::Integer || left_kind == Kind::Float) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            case OperatorGroup::Comparison:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float)) {
                    return Term(&BooleanType::instance, Term::Category::RValue);
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term(&BooleanType::instance, Term::Category::RValue);
                }
                break;
            case OperatorGroup::UnaryLogical:
                if (left_kind == Kind::Boolean) {
                    return Term(&BooleanType::instance, Term::Category::RValue);
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            case OperatorGroup::UnaryBitwise:
                if (left_kind == Kind::Integer) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                if (!left.is_mutable()) break;
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    (left_kind == Kind::Boolean && right_kind == Kind::Boolean)) {
                    return Term(left_type, Term::Category::RValue);
                }
                break;
            }
        }
        /// TODO: throw error
        return Term::unknown();
    }
};

constexpr std::string_view OperatorCodeToString(OperatorCode opcode) {
    switch (opcode) {
    case OperatorCode::Add:
        return "+";
    case OperatorCode::Subtract:
        return "-";
    case OperatorCode::Negate:
        return "-";
    case OperatorCode::Multiply:
        return "*";
    case OperatorCode::Divide:
        return "/";
    case OperatorCode::Remainder:
        return "%";
    case OperatorCode::Increment:
        return "++";
    case OperatorCode::Decrement:
        return "--";
    case OperatorCode::Equal:
        return "==";
    case OperatorCode::NotEqual:
        return "!=";
    case OperatorCode::LessThan:
        return "<";
    case OperatorCode::LessEqual:
        return "<=";
    case OperatorCode::GreaterThan:
        return ">";
    case OperatorCode::GreaterEqual:
        return ">=";
    case OperatorCode::LogicalAnd:
        return "&&";
    case OperatorCode::LogicalOr:
        return "||";
    case OperatorCode::LogicalNot:
        return "!";
    case OperatorCode::BitwiseAnd:
        return "&";
    case OperatorCode::BitwiseOr:
        return "|";
    case OperatorCode::BitwiseXor:
        return "^";
    case OperatorCode::BitwiseNot:
        return "~";
    case OperatorCode::LeftShift:
        return "<<";
    case OperatorCode::RightShift:
        return ">>";
    case OperatorCode::Assign:
        return "=";
    case OperatorCode::AddAssign:
        return "+=";
    case OperatorCode::SubtractAssign:
        return "-=";
    case OperatorCode::MultiplyAssign:
        return "*=";
    case OperatorCode::DivideAssign:
        return "/=";
    case OperatorCode::RemainderAssign:
        return "%=";
    case OperatorCode::LogicalAndAssign:
        return "&&=";
    case OperatorCode::LogicalOrAssign:
        return "||=";
    case OperatorCode::BitwiseAndAssign:
        return "&=";
    case OperatorCode::BitwiseOrAssign:
        return "|=";
    case OperatorCode::BitwiseXorAssign:
        return "^=";
    case OperatorCode::LeftShiftAssign:
        return "<<=";
    case OperatorCode::RightShiftAssign:
        return ">>=";
    default:
        assert(false && "Unknown operator code");
        UNREACHABLE();
    }
}
