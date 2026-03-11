#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"
#include "symbol_collect.hpp"

namespace PrimitiveOperations {

template <OperatorGroup G>
auto apply_op(OperatorCode opcode, const auto& left, const auto& right) {
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
            if constexpr (std::is_same_v<std::decay_t<decltype(left)>, BigInt>) {
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
auto apply_op(OperatorCode opcode, const auto& value) {
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

inline auto integer_op(OperatorCode opcode, Value* left, Value* right) -> Value* {
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

inline auto integer_op(OperatorCode opcode, Value* left) -> Value* {
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

inline auto float_op(OperatorCode opcode, Value* left, Value* right) -> Value* {
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

inline auto float_op(OperatorCode opcode, Value* left) -> Value* {
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

inline auto boolean_op(OperatorCode opcode, Value* left, Value* right) -> BooleanValue* {
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

inline auto boolean_op(OperatorCode opcode, Value* left) -> BooleanValue* {
    BooleanValue* left_bool = left->cast<BooleanValue>();
    switch (GetOperatorGroup(opcode)) {
    case OperatorGroup::UnaryLogical:
        return new BooleanValue(apply_op<OperatorGroup::UnaryLogical>(opcode, left_bool->value_));
    default:
        UNREACHABLE();
    }
}

inline auto assignment_op(OperatorCode opcode, Value* left, Value* right) -> Value* {
    if (opcode == OperatorCode::Assign) {
        left->assign_from(right);
    } else {
        OperatorCode inner_opcode = GetAssignmentEquivalent(opcode);
        Value* result = nullptr;
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

class TypeChecker;

class TemplateHandler : public SemaInfrastructure {
private:
    struct TemplateArgumentComparator {
        auto operator()(Term left, Term right) noexcept {
            if (left.is_type()) {
                return left.get_type() == right.get_type();
            } else {
                /// TODO: value equality
                return false;
            }
        }
    };

private:
    GlobalMemory::MultiMap<const ASTTemplateDefinition*, std::pair<std::span<Term>, const Scope*>>
        instantiations_;

protected:
    auto as_checker() noexcept -> TypeChecker&;

public:
    [[nodiscard]] auto is_template_about_type(const TemplateFamily& family) const noexcept -> bool;
    auto instantiate_as_term(const TemplateFamily& family, std::span<Term> args) -> Term;
    auto instantiate_as_type(const TemplateFamily& family, std::span<Term> args) -> TypeResolution;
    auto template_inference(const TemplateFamily& family, std::span<Term> args) -> std::span<Term>;

private:
    auto validate(const ASTTemplateDefinition& primary, std::span<Term> args) -> bool;
    auto specialization_resolution(const TemplateFamily& family, std::span<Term> args) -> Scope&;
};

class OperationHandler : public TemplateHandler {
private:
    GlobalMemory::FlatMap<std::tuple<OperatorCode, const Type*, const Type*>, FunctionObject> map_;

public:
    auto eval_type_op(OperatorCode opcode, const Type* left, const Type* right = nullptr) const
        -> const Type* {
        /// TODO: implement type-level operations
        return nullptr;
    }

    auto eval_value_op(OperatorCode opcode, Term left, Term right = {}) -> Term {
        bool left_is_mutable = false;
        Term decayed_left = value_decay(left, &left_is_mutable);
        Term decayed_right = value_decay(right);
        if (decayed_left->kind_ == Kind::Unknown ||
            (decayed_right && decayed_right->kind_ == Kind::Unknown)) {
            return Term::unknown();
        } else if ((decayed_left->kind_ == Kind::Integer || decayed_left->kind_ == Kind::Float ||
                    decayed_left->kind_ == Kind::Boolean) &&
                   (right ? (decayed_right->kind_ == Kind::Integer ||
                             decayed_right->kind_ == Kind::Float ||
                             decayed_right->kind_ == Kind::Boolean)
                          : true)) {
            return eval_primitive_op(opcode, decayed_left, decayed_right, left_is_mutable);
        }
        /// TODO: check mutability for assignment operations
        bool comptime = left.is_comptime() && (right && right.is_comptime());
        const Type* left_type = left.effective_type();
        const Type* right_type = right ? right.effective_type() : nullptr;
        auto it = map_.find({opcode, left_type, right_type});
        if (it != map_.end()) {
            if (auto func_value = it->second->dyn_cast<FunctionValue>(); func_value && comptime) {
                return func_value->invoke(GlobalMemory::pack_array(left, right));
            } else {
                auto func_type = it->second->cast<FunctionType>();
                return Term::prvalue(func_type->return_type_);
            }
        } else {
            if (FunctionObject instantiated = try_instantiate(opcode, left_type, right_type)) {
                map_.insert({{opcode, left_type, right_type}, instantiated});
                if (auto func_value = instantiated->dyn_cast<FunctionValue>()) {
                    if (comptime) {
                        return func_value->invoke(GlobalMemory::pack_array(left, right));
                    } else {
                        return Term::prvalue(func_value->get_type()->return_type_);
                    }
                } else {
                    auto func_type = instantiated->cast<FunctionType>();
                    return Term::prvalue(func_type->return_type_);
                }
            }
            throw UnlocatedProblem::make<OperationNotDefinedError>(
                GetOperatorString(opcode), left->repr(), right ? right->repr() : ""
            );
        }
    }

private:
    auto try_instantiate(OperatorCode opcode, const Type* left, const Type* right) const
        -> FunctionObject {
        // User-defined operator templates
        auto range = operators_.equal_range(opcode);
        for (auto it = range.first; it != range.second; ++it) {
            /// TODO:
        }
        return nullptr;
    }

    [[nodiscard]] auto eval_primitive_op(
        OperatorCode opcode, Term left, Term right, bool left_is_mutable
    ) const -> Term {
        const Type* left_type = left.effective_type();
        Kind left_kind = left->kind_;
        Kind right_kind = right ? right->kind_ : Kind::Unknown;
        bool comptime = left.is_comptime() && (right ? right.is_comptime() : true);
        if (comptime) {
            Value* left_value = left.get_comptime();
            Value* right_value = right ? right.get_comptime() : nullptr;
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::prvalue(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value)
                    );
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term::prvalue(
                        PrimitiveOperations::float_op(opcode, left_value, right_value)
                    );
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                assert(!right);
                if (left_kind == Kind::Integer) {
                    return Term::prvalue(PrimitiveOperations::integer_op(opcode, left_value));
                } else if (left_kind == Kind::Float) {
                    return Term::prvalue(PrimitiveOperations::float_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Comparison:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::prvalue(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value)
                    );
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term::prvalue(
                        PrimitiveOperations::float_op(opcode, left_value, right_value)
                    );
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term::prvalue(
                        PrimitiveOperations::boolean_op(opcode, left_value, right_value)
                    );
                }
                break;
            case OperatorGroup::UnaryLogical:
                assert(!right);
                if (left_kind == Kind::Boolean) {
                    return Term::prvalue(PrimitiveOperations::boolean_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::prvalue(
                        PrimitiveOperations::integer_op(opcode, left_value, right_value)
                    );
                }
                break;
            case OperatorGroup::UnaryBitwise:
                if (left_kind == Kind::Integer) {
                    return Term::prvalue(PrimitiveOperations::integer_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                if (!left_is_mutable) break;
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    (left_kind == Kind::Boolean && right_kind == Kind::Boolean)) {
                    return Term::lvalue(left_type);
                }
                break;
            }
        } else {
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float)) {
                    return Term::prvalue(left_type);
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                if (left_kind == Kind::Integer || left_kind == Kind::Float) {
                    return Term::prvalue(left_type);
                }
                break;
            case OperatorGroup::Comparison:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float)) {
                    return Term::prvalue(&BooleanType::instance);
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term::prvalue(&BooleanType::instance);
                }
                break;
            case OperatorGroup::UnaryLogical:
                if (left_kind == Kind::Boolean) {
                    return Term::prvalue(&BooleanType::instance);
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::prvalue(left_type);
                }
                break;
            case OperatorGroup::UnaryBitwise:
                if (left_kind == Kind::Integer) {
                    return Term::prvalue(left_type);
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                if (!left_is_mutable) break;
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    (left_kind == Kind::Boolean && right_kind == Kind::Boolean)) {
                    return Term::lvalue(left_type);
                }
                break;
            }
        }
        /// TODO: throw error
        return Term::unknown();
    }
};

class MemberAccessHandler : public OperationHandler {
private:
    struct TypeRAII {
        GlobalMemory::Vector<FunctionOverloadDecl> ctor;
        FunctionOverloadDecl dtor;
    };

private:
    GlobalMemory::FlatMap<const Type*, TypeRAII> type_raii_;

public:
    auto register_class(
        const ASTClassDefinition* node,
        const Type* type,
        GlobalMemory::Vector<FunctionOverloadDecl> constructors,
        FunctionOverloadDecl destructor
    ) -> void {
        assert(type->kind_ == Kind::Instance);
        assert(!type_raii_.contains(type));
        type_raii_.insert({type, {.ctor = std::move(constructors), .dtor = destructor}});
    }

    auto eval_access(Term object, std::string_view member) -> Term {
        bool is_mutable = false;
        object = value_decay(object, &is_mutable);
        if (object.is_type()) {
            /// TODO: handle type member access
            return {};
        } else {
            Term result{};
            if (object->kind_ == Kind::Struct) {
                result = struct_access(object, member);
            } else if (object->kind_ == Kind::Instance) {
                result = instance_access(object, member);
            } else {
                throw UnlocatedProblem::make<OperationNotDefinedError>(".", object->repr(), member);
            }
            if (!result) {
                /// TODO: throw member not found error
                throw;
            }
            return is_mutable ? wrap_in_mutable(result) : result;
        }
    }

    auto eval_call(Term callee, std::span<Term> args) -> Term {
        if (callee.is_unknown()) {
            return Term::unknown();
        }
        Term decayed = value_decay(callee);

        GlobalMemory::Vector<FunctionObject> overloads = list_overloads(decayed);
        if (overloads.empty()) {
            if (decayed.is_type()) {
                /// TODO: throw type is not callable error
                throw;
            } else {
                throw UnlocatedProblem::make<OperationNotDefinedError>("call", callee->repr(), "");
            }
        }
        GlobalMemory::Vector<const Type*> arg_types = extract_arg_types(args);
        FunctionObject overload = trivial_overload_resolution(overloads, arg_types);
        if (!overload) {
            /// TODO: throw no matching overload error
            throw;
        }
        if (auto func_value = overload->dyn_cast<FunctionValue>()) {
            return func_value->invoke(args);
        } else {
            auto func_type = overload->cast<FunctionType>();
            return Term::prvalue(func_type->return_type_);
        }
    }

    auto get_func_obj(const Scope* scope, const ASTFunctionDefinition* func_def) -> FunctionObject;

    auto get_func_obj(const Scope* scope, const ASTConstructorDestructorDefinition* ctor_dtor_def)
        -> FunctionObject;

private:
    [[nodiscard]] auto list_overloads(Term func) const -> GlobalMemory::Vector<FunctionObject> {
        if (func.is_type() || func->kind_ == Kind::Overload) {
            return {};
        }
        if (auto intersection_type = func->dyn_cast<IntersectionType>()) {
            return intersection_type->types_ |
                   GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>();
        } else if (auto func_value = func->dyn_cast<FunctionValue>()) {
            return {func_value};
        } else if (auto func_type = func->dyn_cast<FunctionType>()) {
            return {func_type};
        } else {
            return {};
        }
    }

    auto struct_access(Term object, std::string_view member) -> Term {
        if (auto struct_type = object->dyn_cast<StructType>()) {
            auto attr_it = struct_type->fields_.find(member);
            if (attr_it != struct_type->fields_.end()) {
                return Term::forward_like(object, attr_it->second);
            }

        } else {
            auto struct_value = object.get_comptime()->cast<StructValue>();
            auto attr_it = struct_value->type_->fields_.find(member);
            if (attr_it != struct_value->type_->fields_.end()) {
                return Term::forward_like(object, attr_it->second);
            }
        }
        return {};
    }

    auto instance_access(Term object, std::string_view member) -> Term {
        auto find_method = [&](const Scope* scope) -> Term {
            const ScopeValue* value = scope->find(member);
            if (value->get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
                return Term::prvalue(new FunctionOverloadSetValue(scope, value));
            }
            return {};
        };
        if (auto instance_type = object->cast<InstanceType>()) {
            auto attr_it = instance_type->attrs_.find(member);
            if (attr_it != instance_type->attrs_.end()) {
                return Term::forward_like(object, attr_it->second);
            }
            return find_method(instance_type->scope_);
        } else {
            auto instance_value = object.get_comptime()->cast<InstanceValue>();
            auto attr_it = instance_value->attrs_.find(member);
            if (attr_it != instance_value->attrs_.end()) {
                return Term::forward_like(object, attr_it->second);
            }
            return find_method(instance_value->type_->scope_);
        }
    }

    [[nodiscard]] auto resolve_overload(
        std::span<FunctionObject> overloads, std::span<const Type*> arg_types
    ) const -> FunctionObject {
        GlobalMemory::Vector<FunctionObject> best_candidates;
        for (FunctionObject candidate : overloads) {
            const FunctionType* func_type = candidate->dyn_cast<FunctionType>();
            if (!func_type) func_type = candidate->cast<FunctionValue>()->get_type();
            if (func_type->parameters_.size() != arg_types.size()) {
                continue;
            }
            bool satisfies = true;
            for (std::size_t i = 0; i < func_type->parameters_.size(); ++i) {
                if (!func_type->parameters_[i]->assignable_from(arg_types[i])) {
                    satisfies = false;
                    break;
                }
            }
            if (satisfies) {
                best_candidates.push_back(candidate);
            }
        }
        return select_best_candidate(best_candidates);
    }

    [[nodiscard]] auto select_best_candidate(std::span<FunctionObject> candidates) const
        -> FunctionObject {
        /// TODO:
        return candidates.size() ? candidates[0] : nullptr;
    }
};

class TypeChecker final : public MemberAccessHandler {
public:
    class Guard final {
    private:
        TypeChecker& checker_;
        Scope* prev_scope_;

    public:
        Guard(TypeChecker& checker, Scope* scope) noexcept
            : checker_(checker), prev_scope_(std::exchange(checker.current_scope_, scope)) {}
        Guard(TypeChecker& checker, const void* child) noexcept
            : checker_(checker),
              prev_scope_(
                  std::exchange(checker.current_scope_, checker.current_scope_->children_.at(child))
              ) {}
        Guard(const Guard&) = delete;
        Guard(Guard&&) = delete;
        auto operator=(const Guard&) = delete;
        auto operator=(Guard&&) = delete;
        ~Guard() noexcept { checker_.current_scope_ = prev_scope_; }
    };

private:
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, TypeResolution> type_cache_;

public:
public:
    TypeChecker(Scope& root, MemberAccessHandler&& sema) noexcept
        : MemberAccessHandler(std::move(sema)) {
        current_scope_ = &root;
    }

    [[nodiscard]] auto get_self_type() const noexcept -> const Type* {
        const Scope* scope = current_scope_;
        while (!scope->self_type_ && scope->parent_) {
            scope = scope->parent_;
        }
        return current_scope_->self_type_;
    }

    [[nodiscard]] auto is_at_top_level() const noexcept -> bool {
        return current_scope_->parent_ == nullptr;
    }

    auto operator[](std::string_view identifier) const noexcept -> const ScopeValue* {
        auto it = current_scope_->identifiers_.find(identifier);
        if (it != current_scope_->identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    /// TODO: injected class name by template
    auto lookup_type(std::string_view identifier) -> TypeResolution;

    auto lookup_term(std::string_view identifier) -> Term;

    auto lookup_instantiation(std::string_view identifier, std::span<Term> args) -> Term;
};

class TypeContextEvaluator {
private:
    TypeChecker& checker_;
    TypeResolution& out_;
    bool require_complete_;

public:
    TypeContextEvaluator(
        TypeChecker& checker, TypeResolution& out, bool require_complete = true
    ) noexcept
        : checker_(checker), out_(out), require_complete_(require_complete) {}

    void operator()(const ASTExprVariant& expr_variant) {
        std::visit(*this, expr_variant);
        if (!out_.is_sized() && require_complete_) {
            /// TODO:
        }
    }

    void operator()(const ASTExpression* node) { UNREACHABLE(); }

    void operator()(const ASTExplicitTypeExpr* node) { UNREACHABLE(); }

    void operator()(const ASTParenExpr* node) { (*this)(node->inner); }

    void operator()(const ASTConstant* node) {
        /// TODO: literal types
        out_ = {};
    }

    void operator()(const ASTSelfExpr* node) {
        if (!node->is_type) {
            Diagnostic::report(SymbolCategoryMismatchError(node->location, false));
            out_ = TypeRegistry::get_unknown();
        } else {
            out_ = checker_.get_self_type();
            if (!out_.get()) {
                /// TODO: throw not in class error
            }
        }
    }

    void operator()(const ASTIdentifier* node) {
        TypeResolution result = checker_.lookup_type(node->str);
        if (!result.is_sized()) {
            if (require_complete_) {
                Diagnostic::report(CircularTypeDependencyError(node->location));
                out_ = TypeRegistry::get_unknown();
                return;
            }
        }
        out_ = result;
    }

    template <ASTUnaryOpClass Op>
    void operator()(const Op* node) {
        TypeResolution expr_result;
        TypeContextEvaluator{checker_, expr_result, false}(node->expr);
        try {
            out_ = TypeResolution(checker_.eval_type_op(Op::opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
    }

    template <ASTBinaryOpClass Op>
    void operator()(const Op* node) {
        TypeResolution left_result;
        TypeContextEvaluator{checker_, left_result}(node->left);
        TypeResolution right_result;
        TypeContextEvaluator{checker_, right_result}(node->right);
        try {
            out_ = checker_.eval_type_op(Op::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTMemberAccess* node) {
        /// TODO:
    }

    void operator()(const ASTStructInitialization* node) {
        Diagnostic::report(SymbolCategoryMismatchError(node->location, true));
        out_ = TypeRegistry::get_unknown();
    }

    void operator()(const ASTFunctionCall* node) {
        Diagnostic::report(SymbolCategoryMismatchError(node->location, true));
        out_ = TypeRegistry::get_unknown();
    }

    void operator()(const ASTPrimitiveType* node) { out_ = node->type; }

    void operator()(const ASTFunctionType* node) {
        out_ = std::type_identity<FunctionType>();
        bool any_error = false;
        std::span<const Type*> param_types =
            node->parameter_types |
            std::views::transform([&](ASTExprVariant param_expr) -> const Type* {
                TypeResolution param_type;
                TypeContextEvaluator{checker_, param_type}(param_expr);
                return param_type;
            }) |
            GlobalMemory::collect<std::span<const Type*>>();
        if (any_error) {
            out_ = TypeRegistry::get_unknown();
            return;
        }
        TypeResolution return_type;
        TypeContextEvaluator{checker_, return_type}(node->return_type);
        TypeRegistry::get_at<FunctionType>(out_, param_types, return_type);
    }

    void operator()(const ASTStructType* node) {
        out_ = std::type_identity<StructType>();
        GlobalMemory::FlatMap<std::string_view, const Type*> field_map =
            node->fields |
            std::views::transform(
                [&](const ASTFieldDeclaration& decl) -> std::pair<std::string_view, const Type*> {
                    TypeResolution field_type;
                    TypeContextEvaluator{checker_, field_type}(decl.type);
                    return {decl.identifier, field_type};
                }
            ) |
            GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
        try {
            TypeRegistry::get_at<StructType>(out_, field_map);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTMutableType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTReferenceType* node) {
        out_ = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out_, expr_type, node->is_moved);
    }

    void operator()(const ASTPointerType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTClassDefinition* node) {
        out_ = new InstanceType(node->identifier);
        TypeChecker::Guard guard(checker_, node);
        checker_.current_scope_->self_type_ = out_;
        const Type* base = resolve_base(node);
        std::span<const Type*> interfaces = resolve_interfaces(node);
        GlobalMemory::Vector<FunctionObject> constructors = resolve_constructors(node, out_);
        FunctionObject destructor = resolve_destructor(node);
        checker_.register_class(node, out_, std::move(constructors), destructor);
        GlobalMemory::FlatMap<std::string_view, const Type*> attrs = resolve_attrs(node);
        GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> methods =
            resolve_methods(node);
        TypeRegistry::get_at<InstanceType>(
            out_, checker_.current_scope_, node->identifier, base, interfaces, std::move(attrs)
        );
    }

    void operator()(const ASTTemplateInstantiation* node) {
        /// TODO:
    }

    void operator()(const auto* node) { UNREACHABLE(); }

private:
    auto resolve_base(const ASTClassDefinition* node) const noexcept -> const Type* {
        if (node->extends.empty()) {
            return nullptr;
        }
        TypeResolution result;
        try {
            result = checker_.lookup_type(node->extends);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return TypeRegistry::get_unknown();
        }
        const Type* type = result;
        if (type->kind_ != Kind::Instance) {
            Diagnostic::report(TypeMismatchError(node->location, "class", type->repr()));
            return TypeRegistry::get_unknown();
        }
        return type;
    }

    auto resolve_interfaces(const ASTClassDefinition* node) const noexcept
        -> std::span<const Type*> {
        auto get_interface_type = [&](std::string_view interface_name) -> const Type* {
            TypeResolution result;
            try {
                result = checker_.lookup_type(interface_name);
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location);
                return TypeRegistry::get_unknown();
            }
            const Type* type = result;
            if (type->kind_ != Kind::Interface) {
                Diagnostic::report(TypeMismatchError(node->location, "interface", type->repr()));
                return TypeRegistry::get_unknown();
            }
            return type->cast<InterfaceType>();
        };
        return node->implements | std::views::transform(get_interface_type) |
               GlobalMemory::collect<std::span>();
    }

    auto resolve_constructors(const ASTClassDefinition* node, const Type* class_type) const noexcept
        -> GlobalMemory::Vector<FunctionObject> {
        return node->constructors |
               std::views::transform(
                   [&](const ASTConstructorDestructorDefinition* ctor) -> FunctionObject {
                       return checker_.get_func_obj(ctor, class_type);
                   }
               ) |
               GlobalMemory::collect<GlobalMemory::Vector>();
    }

    auto resolve_destructor(const ASTClassDefinition* node) const noexcept -> FunctionObject {
        if (!node->destructor) {
            return nullptr;
        }
        return checker_.get_func_obj(node->destructor, nullptr);
    }

    auto resolve_attrs(const ASTClassDefinition* node) const noexcept
        -> GlobalMemory::FlatMap<std::string_view, const Type*> {
        return node->fields |
               std::views::transform(
                   [&](
                       const ASTDeclaration* field_decl
                   ) -> std::pair<std::string_view, const Type*> {
                       TypeResolution field_type;
                       TypeContextEvaluator{checker_, field_type}(field_decl->declared_type);
                       return {field_decl->identifier, field_type};
                   }
               ) |
               GlobalMemory::collect<GlobalMemory::FlatMap>();
    }

    auto resolve_methods(const ASTClassDefinition* node) const noexcept
        -> GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> {
        GlobalMemory::Vector non_static_functions =
            node->functions | std::views::filter([](const ASTFunctionDefinition* func_def) -> bool {
                return !func_def->is_static;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector>();
        std::ranges::sort(
            non_static_functions,
            [](const ASTFunctionDefinition* a, const ASTFunctionDefinition* b) -> bool {
                return a->identifier < b->identifier;
            }
        );
        std::ranges::unique(
            non_static_functions,
            [](const ASTFunctionDefinition* a, const ASTFunctionDefinition* b) -> bool {
                return a->identifier == b->identifier;
            }
        );
        return non_static_functions |
               std::views::transform(
                   [&](
                       const ASTFunctionDefinition* func_def
                   ) -> std::pair<std::string_view, FunctionOverloadSetValue*> {
                       Term result = checker_.lookup_term(func_def->identifier);
                       return {
                           func_def->identifier,
                           result.get_comptime()->cast<FunctionOverloadSetValue>()
                       };
                   }
               ) |
               GlobalMemory::collect<GlobalMemory::FlatMap>();
    }
};

/// TODO: expected parameter is not working
class ValueContextEvaluator {
private:
    TypeChecker& checker_;
    const Type* expected_;
    bool require_comptime_;

public:
    ValueContextEvaluator(
        TypeChecker& checker, const Type* expected, bool require_comptime
    ) noexcept
        : checker_(checker), expected_(expected), require_comptime_(require_comptime) {}

    auto operator()(const ASTExprVariant& variant) -> TermWithReceiver {
        if (std::visit(
                [](auto node) -> bool {
                    return std::is_convertible_v<decltype(node), const ASTExplicitTypeExpr*>;
                },
                variant
            )) {
            TypeResolution out;
            TypeContextEvaluator{checker_, out}(variant);
            return {.subject = Term::type(out), .receiver = {}};
        }
        return std::visit(*this, variant);
    }

    auto operator()(std::monostate) -> TermWithReceiver { UNREACHABLE(); }

    auto operator()(const ASTExpression* node) -> TermWithReceiver { UNREACHABLE(); }

    auto operator()(const ASTExplicitTypeExpr* node) -> TermWithReceiver { UNREACHABLE(); }

    auto operator()(const ASTParenExpr* node) -> TermWithReceiver { return (*this)(node->inner); }

    auto operator()(const ASTConstant* node) -> TermWithReceiver {
        if (expected_) {
            try {
                Value* typed_value = node->value->resolve_to(expected_);
                return {.subject = Term::prvalue(typed_value), .receiver = {}};
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location);
                return {.subject = Term::unknown(), .receiver = {}};
            }
        } else {
            return {.subject = Term::prvalue(node->value->resolve_to(nullptr)), .receiver = {}};
        }
    }

    auto operator()(const ASTSelfExpr* node) -> TermWithReceiver {
        if (node->is_type) {
            return {.subject = Term::type(checker_.get_self_type()), .receiver = {}};
        } else {
            return {.subject = checker_.lookup_term("self"), .receiver = {}};
        }
    }

    auto operator()(const ASTIdentifier* node) -> TermWithReceiver {
        try {
            Term term = checker_.lookup_term(node->str);
            if (require_comptime_ && !term.is_comptime()) {
                Diagnostic::report(NotConstantExpressionError(node->location));
                return {.subject = Term::unknown(), .receiver = {}};
            }
            return {.subject = term, .receiver = {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return {.subject = Term::unknown(), .receiver = {}};
        }
    }

    template <ASTUnaryOpClass Op>
    auto operator()(const Op* node) -> TermWithReceiver {
        Term expr_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->expr).subject;
        return {.subject = checker_.eval_value_op(Op::opcode, expr_term), .receiver = {}};
    }

    template <ASTBinaryOpClass Op>
    auto operator()(const Op* node) -> TermWithReceiver {
        Term left_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->left).subject;
        Term right_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->right).subject;
        try {
            return {
                .subject = checker_.eval_value_op(Op::opcode, left_term, right_term), .receiver = {}
            };
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return {.subject = Term::unknown(), .receiver = {}};
        }
    }

    auto operator()(const ASTMemberAccess* node) -> TermWithReceiver {
        if (auto identifier = std::get_if<ASTIdentifier*>(&node->target)) {
            return eval_namespace_access(node, (*identifier)->str);
        } else {
            Term subject_term =
                ValueContextEvaluator{checker_, expected_, require_comptime_}(node->target).subject;
            return eval_instance_access(node, subject_term, node->members);
        }
    }

    auto operator()(const ASTFieldInitialization* node) -> Term {
        Term value_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->value).subject;
        if (require_comptime_ && !value_term.is_comptime()) {
            Diagnostic::report(NotConstantExpressionError(node->location));
            return Term::unknown();
        }
        return value_term;
    }

    auto operator()(const ASTStructInitialization* node) -> TermWithReceiver {
        GlobalMemory::FlatMap inits =
            node->field_inits |
            std::views::transform(
                [&](const ASTFieldInitialization& init) -> std::pair<std::string_view, Term> {
                    return {
                        init.identifier,
                        ValueContextEvaluator{checker_, nullptr, require_comptime_}(init.value)
                            .subject
                    };
                }
            ) |
            GlobalMemory::collect<GlobalMemory::FlatMap>();
        try {
            /// TODO: constexpr
            const Type* type = nullptr;
            if (nonnull(node->struct_type)) {
                TypeResolution struct_type;
                TypeContextEvaluator{checker_, struct_type}(node->struct_type);
                type = struct_type;
            } else if (auto* self = checker_.get_self_type()) {
                type = self;
            } else {
                // throw UnlocatedProblem::make<SymbolCategoryMismatchError>(node->location, true);
                throw;
            }
            if (type->dyn_cast<InstanceType>() && checker_.get_self_type() != type) {
                /// TODO: throw not in constructor error
                throw;
            }
            return {.subject = StructType::construct(type, inits), .receiver = {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return {.subject = Term::unknown(), .receiver = {}};
        }
    }

    auto operator()(const ASTFunctionCall* node) -> TermWithReceiver {
        bool any_error = false;
        GlobalMemory::Vector<Term> args_terms =
            node->arguments | std::views::transform([&](ASTExprVariant arg) {
                Term arg_term =
                    ValueContextEvaluator{checker_, nullptr, require_comptime_}(arg).subject;
                any_error |= arg_term.is_unknown();
                return arg_term;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        auto [func, receiver] =
            ValueContextEvaluator{checker_, nullptr, require_comptime_}(node->function);
        if (receiver) {
            args_terms.insert(args_terms.begin(), receiver);
        }
        if (any_error) {
            return {.subject = Term::unknown(), .receiver = {}};
        }
        try {
            return {.subject = checker_.eval_call(func, args_terms), .receiver = {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return {.subject = Term::unknown(), .receiver = {}};
        }
    }

    auto operator()(const ASTTemplateInstantiation* node) -> TermWithReceiver {
        GlobalMemory::Vector<Term> args_terms =
            node->arguments | std::views::transform([&](ASTExprVariant arg) {
                return ValueContextEvaluator{checker_, nullptr, require_comptime_}(arg).subject;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        return {
            .subject = checker_.lookup_instantiation(node->template_identifier, args_terms),
            .receiver = {}
        };
    }

    auto operator()(const auto* node) -> TermWithReceiver { UNREACHABLE(); }

private:
    auto eval_namespace_access(const ASTMemberAccess* node, std::string_view subject)
        -> TermWithReceiver {
        auto [_, subject_value] = checker_.lookup(subject);
        auto scope_ptr = subject_value->get<const Scope*>();
        if (!scope_ptr) {
            return eval_instance_access(node, checker_.lookup_term(subject), node->members);
        }
        std::span<std::string_view> members = node->members;
        while (!members.empty()) {
            std::string_view member = members.front();
            auto next = scope_ptr->find(member);
            if (!next) {
                throw UnlocatedProblem::make<UndeclaredIdentifierError>(member);
                return {.subject = Term::unknown(), .receiver = {}};
            }
            if (auto next_scope = next->get<const Scope*>()) {
                scope_ptr = next_scope;
                members = members.subspan(1);
            } else if (auto next_term = next->get<Term*>()) {
                if (members.size()) {
                    return eval_instance_access(node, *next_term, members);
                } else {
                    return {.subject = *next_term, .receiver = {}};
                }
            } else {
                UNREACHABLE();
            }
        }
        /// TODO: evaluates to namespace is invalid
        throw;
    }

    auto eval_instance_access(
        const ASTMemberAccess* node, Term current_term, std::span<std::string_view> members
    ) -> TermWithReceiver {
        Term receiver = {};
        if (current_term.is_unknown()) {
            return {.subject = Term::unknown(), .receiver = {}};
        }
        for (std::string_view member : members) {
            try {
                receiver = current_term;
                current_term = checker_.eval_access(current_term, member);
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location);
                return {.subject = Term::unknown(), .receiver = {}};
            }
        }
        return {.subject = current_term, .receiver = receiver};
    }
};

class TypeCheckVisitor {
private:
    TypeChecker& checker_;

public:
    TypeCheckVisitor(TypeChecker& checker) noexcept : checker_(checker) {}

    void operator()(const ASTNodeVariant& node) noexcept { std::visit(*this, node); }

    void operator()(std::monostate) noexcept { UNREACHABLE(); }

    void operator()(const auto*) {}

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        for (auto stmt : node->statements) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTLocalBlock* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (auto stmt : node->statements) {
            (*this)(stmt);
        }
    }

    // Expressions
    void operator()(const ASTExpression* node) noexcept { UNREACHABLE(); }

    // Statements
    void operator()(const ASTExpressionStatement* node) noexcept {
        ValueContextEvaluator{checker_, nullptr, false}(node->expr);
    }

    void operator()(const ASTDeclaration* node) noexcept { checker_.lookup(node->identifier); }

    void operator()(const ASTTypeAlias* node) noexcept { checker_.lookup_type(node->identifier); }

    void operator()(const ASTIfStatement* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        ValueContextEvaluator{checker_, &BooleanType::instance, false}(node->condition);
        (*this)(node->if_block);
        if (node->else_block) {
            (*this)(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        if (node->initializer_decl) {
            ValueContextEvaluator{checker_, nullptr, false}(node->initializer_decl);
        } else if (!std::holds_alternative<std::monostate>(node->initializer_expr)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->initializer_expr);
        }
        if (!std::holds_alternative<std::monostate>(node->condition)) {
            ValueContextEvaluator{checker_, &BooleanType::instance, false}(node->condition);
        }
        if (!std::holds_alternative<std::monostate>(node->increment)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->increment);
        }
        (*this)(node->body);
    }

    void operator()(const ASTReturnStatement* node) noexcept {
        if (!std::holds_alternative<std::monostate>(node->expr)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->expr);
        }
    }

    // Functions and classes
    void operator()(const ASTFunctionDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTConstructorDestructorDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        checker_.current_scope_->in_constructor_ = true;
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        TypeResolution class_type =
            checker_.lookup_type(node->identifier);  // trigger self type injection
        TypeChecker::Guard guard(checker_, node);
        checker_.current_scope_->self_type_ = class_type;
        for (auto& field : node->fields) {
            (*this)(field);
        }
        for (auto& ctor : node->constructors) {
            (*this)(ctor);
        }
        if (node->destructor) {
            (*this)(node->destructor);
        }
        for (auto& func : node->functions) {
            (*this)(func);
        }
    }

    void operator()(const ASTNamespaceDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (const auto& item : node->items) {
            (*this)(item);
        }
    }
};

// ========== Implementation ==========

inline auto TemplateHandler::as_checker() noexcept -> TypeChecker& {
    /// NOLINTNEXTLINE(cppcoreguidelines-pro-type-static-cast-downcast)
    return static_cast<TypeChecker&>(*this);
}

inline auto TemplateHandler::instantiate_as_term(const TemplateFamily& family, std::span<Term> args)
    -> Term {
    if (!validate(family.primary, args)) {
        return Term::unknown();
    }
    Scope& inst_scope = specialization_resolution(family, args);
    TypeChecker::Guard guard(as_checker(), &inst_scope);
    return as_checker().lookup_term(family.primary.identifier);
}

inline auto TemplateHandler::instantiate_as_type(const TemplateFamily& family, std::span<Term> args)
    -> TypeResolution {
    if (!validate(family.primary, args)) {
        return TypeRegistry::get_unknown();
    }
    Scope& inst_scope = specialization_resolution(family, args);
    TypeChecker::Guard guard(as_checker(), &inst_scope);
    return as_checker().lookup_type(family.primary.identifier);
}

inline auto TemplateHandler::validate(const ASTTemplateDefinition& primary, std::span<Term> args)
    -> bool {
    if (primary.parameters.size() != args.size()) {
        return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
        const auto& param = primary.parameters[i];
        if (args[i].is_type() == param.is_nttp) {
            return false;
        }
        if (args[i].is_type()) {
            /// TODO: type constraint validation
        } else {
            if (!std::holds_alternative<std::monostate>(param.constraint)) {
                Term constraint_term =
                    ValueContextEvaluator{as_checker(), nullptr, false}(param.constraint).subject;
                if (constraint_term.is_type()) {
                    if (!constraint_term.get_type()->assignable_from(args[i].get_type())) {
                        return false;
                    }
                } else {
                    if (auto satisfies = constraint_term.get_comptime()->dyn_cast<BooleanValue>()) {
                        if (!satisfies) return false;
                    } else {
                        /// invalid constriant
                        throw;
                    }
                }
            }
        }
    }
    return true;
}

inline auto TemplateHandler::specialization_resolution(
    const TemplateFamily& family, std::span<Term> args
) -> Scope& {
    for (const auto& specialization : family.specializations) {
        /// TODO:
    }
    Scope& inst_scope = Scope::make(*as_checker().current_scope_);
    TypeChecker::Guard guard(as_checker(), &inst_scope);
    for (size_t i = 0; i < args.size(); i++) {
        inst_scope.add_template_argument(family.primary.parameters[i].identifier, args[i]);
    }
    return inst_scope;
}

inline auto MemberAccessHandler::get_func_obj(
    const Scope* scope, const ASTFunctionDefinition* func_def
) -> FunctionObject {
    TypeChecker::Guard guard{as_checker(), scope};
    bool any_error = false;
    std::span params = func_def->parameters |
                       std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                           TypeResolution param_type;
                           TypeContextEvaluator{as_checker(), param_type}(param.type);
                           return param_type;
                       }) |
                       GlobalMemory::collect<std::span<const Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    TypeResolution return_type;
    TypeContextEvaluator{as_checker(), return_type}(func_def->return_type);
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline auto MemberAccessHandler::get_func_obj(
    const Scope* scope, const ASTConstructorDestructorDefinition* node
) -> FunctionObject {
    TypeChecker::Guard guard{as_checker(), scope};
    bool any_error = false;
    std::span params = node->parameters |
                       std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                           TypeResolution param_type;
                           TypeContextEvaluator{as_checker(), param_type}(param.type);
                           return param_type;
                       }) |
                       GlobalMemory::collect<std::span<const Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    return TypeRegistry::get<FunctionType>(params, as_checker().get_self_type());
}

inline auto TypeChecker::lookup_type(std::string_view identifier) -> TypeResolution {
    auto [scope, value] = lookup(identifier);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    } else if (auto term = value->get<Term*>()) {
        if (!term->is_type()) {
            throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
        }
        return term->get_type();
    }
    // Check cache
    auto [it_id_cache, inserted] = type_cache_.insert({{scope, identifier}, TypeResolution()});
    if (!inserted) {
        return it_id_cache->second;
    }
    // Cache miss; resolve
    Guard guard(*this, scope);
    if (auto type_alias = value->get<const ASTExprVariant*>()) {
        TypeContextEvaluator{*this, it_id_cache->second}(*type_alias);
    } else if (auto class_def = value->get<const ASTClassDefinition*>()) {
        TypeContextEvaluator{*this, it_id_cache->second}(class_def);
    } else {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    }
    // Ignore incomplete types in cache to prevent type interning bypassed
    if (TypeRegistry::is_type_incomplete(it_id_cache->second)) {
        const Type* incomplete_type = it_id_cache->second;
        type_cache_.erase(it_id_cache);
        return incomplete_type;
    } else {
        return it_id_cache->second;
    }
}

inline auto TypeChecker::lookup_term(std::string_view identifier) -> Term {
    auto [scope, value] = lookup(identifier);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    }
    if (auto term = value->get<Term*>()) {
        return *term;
    } else if (auto alias = value->get<const ASTExprVariant*>()) {
        if (auto it = type_cache_.find({scope, identifier}); it != type_cache_.end()) {
            return Term::type(it->second);
        }
        Guard guard(*this, scope);
        TypeResolution out;
        TypeContextEvaluator{*this, out}(*alias);
        return Term::type(out);
    } else if (auto class_def = value->get<const ASTClassDefinition*>()) {
        return Term::type(lookup_type(class_def->identifier));
    } else if (auto var_init = value->get<const VariableInitialization*>()) {
        if (nonnull(var_init->type)) {
            TypeResolution var_type;
            TypeContextEvaluator{*this, var_type}(var_init->type);
            if (nonnull(var_init->value)) {
                Term init = ValueContextEvaluator{*this, var_type, false}(var_init->value).subject;
                if (!var_type->assignable_from(init.effective_type())) {
                    throw;
                }
            }
            return Term::lvalue(var_type.get());
        } else {
            assert(nonnull(var_init->value));
            return Term::lvalue(
                ValueContextEvaluator{*this, nullptr, false}(var_init->value).subject
            );
        }
    } else if (value->get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
        return Term::prvalue(new FunctionOverloadSetValue(scope, value));
    } else {
        /// TODO: throw
        assert(false);
    }
}

inline auto TypeChecker::lookup_instantiation(std::string_view name, std::span<Term> args) -> Term {
    auto [scope, value] = lookup(name);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(name);
    }
    if (auto temp = value->get<TemplateFamily*>()) {
        Guard guard(*this, scope);
        if (validate_instantiation(temp->primary, args)) {
            Scope& instantiation_scope = specialization_resolution(*temp, args);
            SymbolCollector{instantiation_scope, sema_}(temp->primary.target_node);
            Guard inner_guard(*this, &instantiation_scope);
            TypeCheckVisitor{*this}(temp->primary.target_node);
            return lookup_term(temp->primary.identifier);
        }
    } else {
        throw;
    }
}
