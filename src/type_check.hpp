#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"
#include "symbol_collect.hpp"

struct BoundMethod {
    Term object;
    Scope* scope;
    const ScopeValue* value;
};

using Symbol = std::variant<
    std::monostate,
    const Type*,
    Term,
    std::pair<Scope*, const ScopeValue*>,
    BoundMethod,
    TemplateFamily*,
    std::span<const Object*>,
    Scope*,
    std::tuple<OperatorCode, const Type*, const Type*>>;

enum class SymbolKind : std::uint8_t {
    Type = 1,
    Term = 2,
    Function = 3,
    Method = 4,
    Template = 5,
    ParameterPack = 6,
    Namespace = 7,
    Operator = 8,
};

class CodeGenEnvironment {
public:
    struct FunctionDef {
        const Scope* scope;
        ASTNodeVariant node;
        const FunctionType* func_type;
    };

    struct FunctionCall {
        const FunctionType* func_type;
        const Type* self_type;
        bool is_constructor;
        bool do_not_mangle;
    };

    using TableValue = std::variant<
        const Type*,    // type expression
        const Scope*,   // member access
        FunctionCall>;  // function call

    using Table = GlobalMemory::FlatMap<const ASTNode*, TableValue>;

public:
    static auto mangle_path(GlobalMemory::String& mangled, const Scope* scope, strview identifier)
        -> void {
        if (scope->is_extern_) {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(std::back_inserter(mangled), "{}::", current->scope_id_);
                }
            }(scope);
            mangled += identifier;
        } else {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    if (current->scope_id_[0] == '0') {
                        mangled += "_";
                        mangled += current->scope_id_;
                    } else {
                        std::format_to(
                            std::back_inserter(mangled),
                            "_{}{}",
                            current->scope_id_.length(),
                            current->scope_id_
                        );
                    }
                }
            }(scope);
            std::format_to(std::back_inserter(mangled), "_{}{}", identifier.length(), identifier);
        }
    }

public:
    GlobalMemory::Vector<FunctionDef> functions_;
    GlobalMemory::Vector<std::pair<Scope*, std::span<const Object*>>> instantiations_;
    GlobalMemory::FlatMap<const Scope*, Table> scope_map_;

public:
    auto add_function_output(
        const Scope* current_scope, ASTNodeVariant node, const FunctionType* func_obj
    ) -> void {
        if (current_scope->is_extern_) return;
        functions_.push_back({current_scope, node, func_obj});
    }

    auto map_type(const Scope* current_scope, const ASTNode* node, const Type* type) -> void {
        scope_map_[current_scope][node] = type;
    }

    auto map_member_access(const Scope* current_scope, const ASTNode* node, const Scope* scope)
        -> void {
        scope_map_[current_scope][node] = scope;
    }

    auto map_func_call(
        const Scope* current_scope,
        const ASTNode* node,
        const FunctionType* func_type,
        const Type* self_type,
        bool is_constructor,
        bool do_not_mangle
    ) -> void {
        scope_map_[current_scope][node] = FunctionCall{
            .func_type = func_type,
            .self_type = self_type,
            .is_constructor = is_constructor,
            .do_not_mangle = do_not_mangle,
        };
    }

    auto map_instantiation(const Scope* inst_scope, std::span<const Object*> args) -> void {
        instantiations_.push_back({const_cast<Scope*>(inst_scope), args});
    }

    TableValue* find(const Scope* current_scope, const ASTNode* node) {
        auto scope_it = scope_map_.find(current_scope);
        if (scope_it == scope_map_.end()) {
            return nullptr;
        }
        auto& table = scope_it->second;
        auto node_it = table.find(node);
        if (node_it == table.end()) {
            return nullptr;
        }
        return &node_it->second;
    }
};

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
            Diagnostic::error_operation_not_defined(
                GetOperatorString(opcode), left->repr(), right->repr()
            );
            return nullptr;
        }
        left->assign_from(result);
    }
    return left;
}
}  // namespace PrimitiveOperations

class TemplateHandler;
class OperationHandler;
class AccessHandler;
class CallHandler;

class Sema final {
public:
    class Guard final {
    private:
        Sema& sema_;
        Scope* prev_scope_;

    public:
        Guard(Sema& sema, Scope& scope) noexcept
            : sema_(sema), prev_scope_(std::exchange(sema.current_scope_, &scope)) {}
        Guard(Sema& sema, const ASTNode* child) noexcept
            : sema_(sema),
              prev_scope_(
                  std::exchange(sema.current_scope_, sema.current_scope_->children_.at(child))
              ) {}
        ~Guard() noexcept { sema_.current_scope_ = prev_scope_; }
    };

public:
    template <std::same_as<SymbolKind>... Kinds>
    static auto expect(const Symbol& symbol, Kinds... expected_kinds) noexcept -> bool {
        if (symbol.index() == 0) return false;
        if (((symbol.index() == static_cast<std::size_t>(expected_kinds)) || ...)) return true;
        return false;
        // if constexpr (expected_kind == SymbolKind::Type) {
        //     throw UnlocatedProblem::make<TypeExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Runtime) {
        //     throw UnlocatedProblem::make<RuntimeValueExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Comptime) {
        //     throw UnlocatedProblem::make<ComptimeValueExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Function) {
        //     throw UnlocatedProblem::make<FunctionExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Method) {
        //     throw UnlocatedProblem::make<MethodExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Template) {
        //     throw UnlocatedProblem::make<TemplateExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::ParameterPack) {
        //     throw UnlocatedProblem::make<ParameterPackExpectedError>();
        // } else if constexpr (expected_kind == SymbolKind::Namespace) {
        //     throw UnlocatedProblem::make<NamespaceExpectedError>();
        // } else {
        //     static_assert(false);
        // }
    }

    template <SymbolKind kind>
    static auto get(const Symbol& symbol) -> auto& {
        assert(symbol.index() == static_cast<std::uint8_t>(kind));
        return std::get<static_cast<std::uint8_t>(kind)>(symbol);
    }

    template <SymbolKind kind>
    static auto get_if(const Symbol& symbol) -> auto* {
        return std::get_if<static_cast<std::uint8_t>(kind)>(&symbol);
    }

    static auto decay(Term term) -> Term {
        if (!term) return term;
        if (auto ref_type = term->dyn_cast<ReferenceType>()) {
            return decay(Term::lvalue(ref_type->referenced_type_));
        } else if (auto ref_value = term->dyn_cast<ReferenceValue>()) {
            return decay(Term::lvalue(ref_value->referenced_value_));
        } else if (auto mut_type = term->dyn_cast<MutableType>()) {
            return decay(Term::forward_like(term, mut_type->target_type_));
        } else if (auto mut_value = term->dyn_cast<MutableValue>()) {
            return decay(Term::forward_like(term, mut_value->value_));
        }
        return term;
    }

    static auto decay(const Type* type) -> const Type* {
        if (auto ref_type = type->dyn_cast<ReferenceType>()) {
            return decay(ref_type->referenced_type_);
        } else if (auto mut_type = type->dyn_cast<MutableType>()) {
            return decay(mut_type->target_type_);
        }
        return type;
    }

    static auto is_mutable(Term term) -> bool {
        if (!term) return false;
        if (auto ref_type = term->dyn_cast<ReferenceType>()) {
            return is_mutable(Term::lvalue(ref_type->referenced_type_));
        } else if (auto ref_value = term->dyn_cast<ReferenceValue>()) {
            return is_mutable(Term::lvalue(ref_value->referenced_value_));
        } else if (term->dyn_cast<MutableType>() || term->dyn_cast<MutableValue>()) {
            return true;
        }
        return false;
    }

    static auto apply_mutable(Term term, bool is_mutable) -> Term {
        if (!is_mutable) return term;
        if (auto value = term.get_comptime()) {
            return Term::forward_like(
                term, new MutableValue(TypeRegistry::get<MutableType>(term.effective_type()), value)
            );
        } else {
            return Term::forward_like(term, TypeRegistry::get<MutableType>(term.effective_type()));
        }
    }

    static auto apply_value_category(Term obj) -> const Type* {
        const Type* type = obj.effective_type();
        if (obj.value_category() == ValueCategory::Left) {
            if (auto* ref_type = type->dyn_cast<ReferenceType>()) {
                return ref_type;
            }
            return TypeRegistry::get<ReferenceType>(type, false);
        } else if (obj.value_category() == ValueCategory::Expiring) {
            return TypeRegistry::get<ReferenceType>(type, true);
        } else {
            return type;
        }
    }

private:
    GlobalMemory::Map<std::pair<const Scope*, strview>, TypeResolution> type_cache_;

public:
    Scope* current_scope_;
    CodeGenEnvironment& codegen_env_;
    std::unique_ptr<TemplateHandler> template_handler_;
    std::unique_ptr<OperationHandler> operation_handler_;
    std::unique_ptr<AccessHandler> access_handler_;
    std::unique_ptr<CallHandler> call_handler_;

public:
    Sema(Scope& root, CodeGenEnvironment& codegen_env) noexcept;

    auto deferred_analysis(Scope& scope, auto variant) noexcept -> void;

    auto get_self_type() const noexcept -> const Type* {
        const Scope* scope = current_scope_;
        while (!scope->self_type_ && scope->parent_) {
            scope = scope->parent_;
        }
        return scope->self_type_;
    }

    auto is_at_top_level() const noexcept -> bool { return current_scope_->parent_ == nullptr; }

    auto eval_type(strview identifier, Scope& scope, const ScopeValue& value) noexcept
        -> TypeResolution;

    auto eval_symbol(strview identifier, Scope& scope, const ScopeValue& value) noexcept -> Symbol;

    auto lookup(strview identifier) -> std::pair<Scope*, const ScopeValue*> {
        Scope* scope = current_scope_;
        while (scope) {
            auto it = scope->identifiers_.find(identifier);
            if (it != scope->identifiers_.end()) {
                return {scope, &it->second};
            }
            scope = scope->parent_;
        }
        return {nullptr, nullptr};
    }

    /// TODO: injected class name by template
    auto lookup_type(strview identifier) -> TypeResolution {
        auto [scope, value] = lookup(identifier);
        if (!scope) {
            Diagnostic::error_undeclared_identifier(identifier);
            return {};
        }
        return eval_type(identifier, *scope, *value);
    }

    auto lookup_symbol(strview identifier) -> Symbol {
        auto [scope, value] = lookup(identifier);
        if (!scope) {
            Diagnostic::error_undeclared_identifier(identifier);
            return {};
        }
        return eval_symbol(identifier, *scope, *value);
    }

    auto get_std_symbol(strview identifier) -> Symbol {
        auto [scope, value] = lookup("std");
        auto std_scope = value->get<Scope*>();
        return eval_symbol(identifier, *std_scope, *std_scope->find(identifier));
    }
};

class TemplateHandler final : public GlobalMemory::MonotonicAllocated {
private:
    using TemplateArgs = std::span<const Object*>;

    using TemplateCacheKey = std::pair<const ASTTemplateDefinition*, TemplateArgs>;

    struct TemplateKeyHasher {
        auto operator()(const TemplateCacheKey& key) const noexcept -> std::size_t {
            std::size_t result = std::bit_cast<std::size_t>(key.first);
            for (const Object* arg : key.second) {
                if (auto* type = arg->dyn_type()) {
                    result = hash_combine(result, std::bit_cast<std::size_t>(type));
                } else {
                    result = hash_combine(
                        result, std::bit_cast<std::size_t>(arg->cast<Value>()->hash_code())
                    );
                }
            }
            return result;
        }
    };

    struct TemplateKeyComparator {
        auto operator()(const TemplateCacheKey& left, const TemplateCacheKey& right) const noexcept
            -> bool {
            if (left.first != right.first) {
                return false;
            }
            if (left.second.size() != right.second.size()) {
                return false;
            }
            for (size_t i = 0; i < left.second.size(); i++) {
                const Object* left_arg = left.second[i];
                const Object* right_arg = right.second[i];
                if (auto* left_type = left_arg->dyn_type()) {
                    if (left_type != right_arg) {
                        return false;
                    }
                } else {
                    if (left_arg->cast<Value>()->less_compare(right_arg->cast<Value>())) {
                        return false;
                    }
                }
            }
            return true;
        }
    };

    struct SpecializationPrototype {
        std::span<const Object*> patterns;
        std::span<const Object*> skolems;
    };

private:
    Sema& sema_;
    GlobalMemory::
        UnorderedMap<TemplateCacheKey, const Object*, TemplateKeyHasher, TemplateKeyComparator>
            cache_;
    GlobalMemory::UnorderedMap<const ASTTemplateSpecialization*, SpecializationPrototype>
        pattern_cache_;

public:
    TemplateHandler(Sema& sema) noexcept : sema_(sema) {}

    inline auto instantiate(const ASTTemplateInstantiation* node) noexcept -> Symbol;

    inline auto instantiate(TemplateFamily& family, TemplateArgs args) -> Symbol {
        if (auto meta_result = catch_meta_instantiation(family, args)) {
            return {};
        }
        Sema::Guard guard(sema_, *family.decl_scope);
        if (!validate(*family.primary, args)) {
            return {};
        }
        if (auto cache_it = cache_.find({family.primary, args}); cache_it != cache_.end()) {
            return as_symbol(cache_it->second);
        }
        auto [inst_scope, target] = specialization_resolution(family, args);
        Sema::Guard inner_guard(sema_, *inst_scope);
        sema_.codegen_env_.map_instantiation(inst_scope, args);
        sema_.deferred_analysis(*inst_scope, target);
        Symbol result = sema_.lookup_symbol(family.primary->identifier);
        if (const Type* type = std::get<static_cast<size_t>(SymbolKind::Type)>(result)) {
            if (auto instance_type = type->dyn_cast<InstanceType>()) {
                instance_type->primary_template_ = &family.primary;
                instance_type->template_args_ = args;
            }
        }
        std::span persisted_args = args | GlobalMemory::collect<std::span>();
        cache_[{family.primary, persisted_args}] =
            Sema::get_if<SymbolKind::Type>(result)
                ? static_cast<const Object*>(Sema::get<SymbolKind::Type>(result))
                : Sema::get<SymbolKind::Term>(result).get_comptime();
        return result;
    }

    inline auto instantiate(Scope* scope, const ASTTemplateDefinition* primary, TemplateArgs args)
        -> Symbol {
        Sema::Guard guard(sema_, *scope);
        if (!validate(*primary, args)) {
            return {};
        }
        Scope& inst_scope = Scope::make(*sema_.current_scope_);
        for (size_t i = 0; i < primary->parameters.size(); i++) {
            inst_scope.set_template_argument(primary->parameters[i].identifier, args[i]);
        }
        if (primary->parameters.back().is_variadic) {
            TemplateArgs pack_args = args.subspan(primary->parameters.size() - 1);
            inst_scope.set_template_pack(primary->parameters.back().identifier, pack_args);
        }
        /// TODO: parameter pack
        Sema::Guard inner_guard(sema_, inst_scope);
        sema_.codegen_env_.map_instantiation(&inst_scope, args);
        sema_.deferred_analysis(inst_scope, primary->target_node);
        Symbol result = sema_.lookup_symbol(primary->identifier);
        if (auto* type = Sema::get_if<SymbolKind::Type>(result)) {
            if (auto instance_type = (*type)->dyn_cast<InstanceType>()) {
                instance_type->primary_template_ = &primary;
                instance_type->template_args_ = args;
            }
        }
        return result;
    }

    auto inference(
        Scope* scope, const ASTTemplateDefinition* primary, std::span<const Type*> args
    ) noexcept -> FunctionObject;

private:
    static auto as_symbol(const Object* obj) -> Symbol {
        if (auto type = obj->dyn_type()) {
            return Symbol(type);
        } else {
            /// NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
            return Symbol(Term::prvalue(const_cast<Value*>(obj->cast<Value>())));
        }
    }

    auto validate(const ASTTemplateDefinition& primary, TemplateArgs args) noexcept -> bool;

    auto specialization_resolution(TemplateFamily& family, TemplateArgs args) noexcept
        -> std::pair<Scope*, ASTNodeVariant>;

    auto get_prototype(
        Scope& pattern_scope, const ASTTemplateSpecialization& specialization
    ) noexcept -> const SpecializationPrototype&;

    static auto compare_specialization(
        const SpecializationPrototype& lhs, const SpecializationPrototype& rhs
    ) noexcept -> std::partial_ordering {
        bool lhs_better = false;
        bool rhs_better = false;
        for (size_t i = 0; i < lhs.patterns.size(); i++) {
            std::partial_ordering result = compare_pattern(
                {lhs.patterns[i], lhs.skolems[i]}, {rhs.patterns[i], rhs.skolems[i]}
            );
            if (result == std::partial_ordering::less) {
                rhs_better = true;
            } else if (result == std::partial_ordering::greater) {
                lhs_better = true;
            }
        }
        if (lhs_better && !rhs_better) {
            return std::partial_ordering::greater;
        } else if (!lhs_better && rhs_better) {
            return std::partial_ordering::less;
        } else if (!lhs_better && !rhs_better) {
            return std::partial_ordering::equivalent;
        } else {
            return std::partial_ordering::unordered;
        }
    }

    static auto compare_pattern(
        std::pair<const Object*, const Object*> lhs, std::pair<const Object*, const Object*> rhs
    ) noexcept -> std::partial_ordering {
        GlobalMemory::FlatMap<const Object*, const Object*> auto_bindings;
        bool lhs_fits_rhs = rhs.first->pattern_match(lhs.second, auto_bindings);
        bool rhs_fits_lhs = lhs.first->pattern_match(rhs.second, auto_bindings);
        if (lhs_fits_rhs && !rhs_fits_lhs) {
            return std::partial_ordering::greater;
        } else if (!lhs_fits_rhs && rhs_fits_lhs) {
            return std::partial_ordering::less;
        } else if (!lhs_fits_rhs && !rhs_fits_lhs) {
            return std::partial_ordering::unordered;
        } else {
            return std::partial_ordering::equivalent;
        }
    }

    static auto fill_bindings(
        const ASTTemplateSpecialization& specialization, const AutoBindings& bindings, Scope& scope
    ) noexcept -> void {
        auto at = [&](bool is_nttp, std::size_t index) -> const ASTTemplateParameter& {
            size_t this_index = 0;
            for (const auto& param : specialization.parameters) {
                if (is_nttp == param.is_nttp) {
                    if (this_index == index) {
                        return param;
                    }
                    this_index++;
                }
            }
            UNREACHABLE();
        };
        for (auto [auto_inst, value] : bindings) {
            bool is_nttp = false;
            size_t index = 0;
            if (auto auto_type = auto_inst->dyn_cast<AutoType>()) {
                index = auto_type->index_;
            } else {
                auto auto_value = auto_inst->cast<AutoValue>();
                is_nttp = true;
                index = auto_value->index_;
            }
            const ASTTemplateParameter& template_param = at(is_nttp, index);
            scope.set_template_argument(template_param.identifier, value);
        }
    }

    auto catch_meta_instantiation(TemplateFamily& family, TemplateArgs args) noexcept
        -> std::optional<const Object*> {
        if (family.decl_scope != nullptr) {
            return std::nullopt;
        }
        return std::bit_cast<MetaFunction>(family.primary)(args);
    }
};

class CallHandler final : public GlobalMemory::MonotonicAllocated {
private:
    enum class ConversionRank : std::uint8_t {
        Exact = 0,                // exactly the same type
        Qualified = 1,            // requires modifying reference qualification
        Referenced = 1,           // requires modifying reference (lvalue to rvalue or rvalue
                                  // to lvalue)
        QualifiedReferenced = 2,  // requires modifying both reference qualification and reference
        Upcast = 3,               // requires upcasting in class hierarchy, or to void pointer
        Copy = 4,                 // requires implicit copy of the same type
        NoMatch = 5,              // no viable conversion
    };

public:
    static auto get_func_type(FunctionObject func) -> const FunctionType* {
        if (auto func_value = func->dyn_cast<FunctionValue>()) {
            return func_value->get_type();
        } else {
            return func->cast<FunctionType>();
        }
    }

private:
    Sema& sema_;

public:
    CallHandler(Sema& sema) noexcept : sema_(sema) {}

    auto get_func_obj(Scope* scope, FunctionOverloadDef overload) noexcept -> FunctionObject;

    auto eval_call(const ASTNode* node, Symbol callee, std::span<Term> args) -> Term {
        if (holds_monostate(callee)) return {};
        auto transform = [](Term arg) -> const Type* {
            if (auto value = arg.get_comptime()) {
                arg = Term::forward_like(arg, value->resolve_to(nullptr));
            }
            return Sema::apply_value_category(arg);
        };
        GlobalMemory::Vector transformed_args =
            args | std::views::transform(transform) | GlobalMemory::collect<GlobalMemory::Vector>();
        if (auto* method = Sema::get_if<SymbolKind::Method>(callee)) {
            transformed_args.insert(transformed_args.begin(), transform(method->object));
        }
        if (std::ranges::contains(transformed_args, nullptr)) {
            return {};
        }

        FunctionObject overload = resolve_overload(callee, transformed_args);
        if (!overload) {
            /// TODO: throw no matching overload error
            Diagnostic::error_no_matching_overload(node->location);
            return {};
        }

        bool do_not_mangle = false;
        if (auto* callee_term = Sema::get_if<SymbolKind::Term>(callee);
            callee_term && (*callee_term)->kind_ == Kind::Function) {
            do_not_mangle = true;
        } else if (
            auto* callee_func = Sema::get_if<SymbolKind::Function>(callee);
            callee_func && callee_func->first->is_extern_
        ) {
            do_not_mangle = true;
        }
        sema_.codegen_env_.map_func_call(
            sema_.current_scope_,
            node,
            get_func_type(overload),
            Sema::get_if<SymbolKind::Method>(callee)
                ? Sema::get<SymbolKind::Method>(callee).object.effective_type()
                : nullptr,
            Sema::get_if<SymbolKind::Type>(callee),
            do_not_mangle
        );
        if (auto func_value = overload->dyn_cast<FunctionValue>()) {
            // return func_value->invoke(args);
            return {};
        } else {
            auto func_type = overload->cast<FunctionType>();
            return Term::prvalue(func_type->return_type_);
        }
    }

private:
    auto list_normal_overloads(Symbol func) -> GlobalMemory::Vector<FunctionObject> {
        auto reification = [&](
                               Scope* scope, const ScopeValue* scope_value
                           ) -> GlobalMemory::Vector<FunctionObject> {
            auto* overloads = scope_value->get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            return *overloads | std::views::filter([](FunctionOverloadDef overload) {
                return !overload.get<const ASTTemplateDefinition*>();
            }) | std::views::transform([this, scope](FunctionOverloadDef overload) {
                return get_func_obj(scope, overload);
            }) | GlobalMemory::collect<GlobalMemory::Vector>();
        };
        if (auto* callable_type = Sema::get_if<SymbolKind::Type>(func)) {
            if ((*callable_type)->kind_ != Kind::Instance) {
                throw;
                return {};
            }
            Scope* scope = (*callable_type)->cast<InstanceType>()->scope_;
            return reification(scope, scope->find(constructor_symbol));
        } else if (auto* callable_value = Sema::get_if<SymbolKind::Term>(func)) {
            Term decayed = Sema::decay(*callable_value);
            if (auto* func_type = decayed->dyn_cast<FunctionType>()) {
                return {func_type};
            } else if (auto func_value = decayed->dyn_cast<FunctionValue>()) {
                return {func_value};
            }
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(func)) {
            return reification(function->first, function->second);
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(func)) {
            return reification(method->scope, method->value);
        } else if (auto* operator_fn = Sema::get_if<SymbolKind::Operator>(func)) {
            GlobalMemory::Vector<FunctionObject> result;
            const Type* left_type = std::get<1>(*operator_fn);
            if (left_type->kind_ == Kind::Instance) {
                Scope* scope = left_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    result = reification(scope, overloads);
                }
            }
            if (const Type* right_type = std::get<2>(*operator_fn);
                right_type && right_type->kind_ == Kind::Instance && left_type != right_type) {
                Scope* scope = right_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    auto operator_overloads = reification(scope, overloads);
                    result.insert(
                        result.end(), operator_overloads.begin(), operator_overloads.end()
                    );
                }
            }
            return result;
        }
        return {};
    }

    auto list_template_overloads(
        GlobalMemory::Vector<FunctionObject>& out, Symbol func, std::span<const Type*> args
    ) -> void {
        auto reification = [&](Scope* scope, const ScopeValue* scope_value) -> void {
            auto* overloads = scope_value->get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            for (FunctionOverloadDef overload : *overloads) {
                if (!overload.get<const ASTTemplateDefinition*>()) continue;
                if (auto func_obj = sema_.template_handler_->inference(
                        scope, overload.get<const ASTTemplateDefinition*>(), args
                    )) {
                    out.push_back(func_obj);
                }
            }
        };
        if (auto* callable_type = Sema::get_if<SymbolKind::Type>(func)) {
            if ((*callable_type)->kind_ != Kind::Instance) {
                throw;
                return;
            }
            Scope* scope = (*callable_type)->cast<InstanceType>()->scope_;
            reification(scope, scope->find(constructor_symbol));
        } else if (auto* callable_value = Sema::get_if<SymbolKind::Term>(func)) {
            Term decayed = Sema::decay(*callable_value);
            if (auto* func_type = decayed->dyn_cast<FunctionType>()) {
                out.push_back(func_type);
            } else if (auto func_value = decayed->dyn_cast<FunctionValue>()) {
                out.push_back(func_value);
            }
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(func)) {
            return reification(function->first, function->second);
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(func)) {
            return reification(method->scope, method->value);
        } else if (auto* operator_fn = Sema::get_if<SymbolKind::Operator>(func)) {
            GlobalMemory::Vector<FunctionObject> result;
            const Type* left_type = std::get<1>(*operator_fn);
            if (left_type->kind_ == Kind::Instance) {
                Scope* scope = left_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    reification(scope, overloads);
                }
            }
            if (const Type* right_type = std::get<2>(*operator_fn);
                right_type->kind_ == Kind::Instance && left_type != right_type) {
                Scope* scope = right_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    reification(scope, overloads);
                }
            }
        }
    }

    auto resolve_overload(Symbol func, std::span<const Type*> args) -> FunctionObject {
        FunctionObject best_candidate = nullptr;
        ConversionRank best_rank = ConversionRank::NoMatch;
        auto loop = [&](std::span<FunctionObject> candidates) -> std::size_t {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                FunctionObject candidate = candidates[i];
                ConversionRank rank = overload_rank(candidate, args);
                if (rank == ConversionRank::NoMatch) {
                    // remove non-viable candidate
                    std::swap(candidates[i], candidates.back());
                    candidates = candidates.subspan(0, candidates.size() - 1);
                    --i;
                    continue;
                }
                if (best_candidate == nullptr) {
                    best_candidate = candidate;
                    best_rank = rank;
                } else {
                    std::partial_ordering order =
                        overload_partial_order(best_candidate, candidate, args);
                    if (order == std::partial_ordering::less) {
                        best_candidate = candidate;
                        best_rank = rank;
                    }
                }
            }
            return candidates.size();
        };
        // first, find the best non-template overload
        GlobalMemory::Vector<FunctionObject> overloads = list_normal_overloads(func);
        std::size_t normal_count = loop(overloads);
        overloads.resize(normal_count);
        // second, if no exact match, try template overloads
        if (best_rank != ConversionRank::Exact) {
            list_template_overloads(overloads, func, args);
            loop(
                {std::next(overloads.begin(), static_cast<std::ptrdiff_t>(normal_count)),
                 overloads.end()}
            );
        }
        // third, re-iterate through all candidates to check for ambiguity
        GlobalMemory::Vector<FunctionObject> ambiguous_candidates;
        bool incomparable = false;
        if (best_candidate) {
            for (FunctionObject candidate : overloads) {
                if (candidate == best_candidate) continue;
                std::partial_ordering order =
                    overload_partial_order(best_candidate, candidate, args);
                if (order == std::partial_ordering::equivalent) {
                    ambiguous_candidates.push_back(candidate);
                } else if (order == std::partial_ordering::unordered) {
                    ambiguous_candidates.push_back(candidate);
                    incomparable = true;
                }
            }
        }
        if (!incomparable && ambiguous_candidates.size()) {
            // if there are only equivalent candidates, choose nontemplate one if possible
            auto it = std::ranges::find(overloads, ambiguous_candidates[0]);
            if (std::distance(overloads.begin(), it) < static_cast<std::ptrdiff_t>(normal_count)) {
                ambiguous_candidates.clear();
            }
        }
        if (!ambiguous_candidates.empty()) {
            /// TODO: throw ambiguous call error, listing best_candidate and all candidates in
            /// ambiguous_candidates
            return nullptr;
        }
        return best_candidate;
    }

    static auto param_rank(const Type* param, const Type* arg) -> ConversionRank {
        using enum ConversionRank;
        bool is_arg_mutable = false;
        if (auto ref_type = arg->dyn_cast<ReferenceType>()) {
            is_arg_mutable = ref_type->referenced_type_->kind_ == Kind::Mutable;
        }
        auto decayed_param = Sema::decay(param);
        auto decayed_arg = Sema::decay(arg);
        if (decayed_param == decayed_arg) {
            // the order of variants indicates the rank of conversion, e.g. T -> move &T is
            // better than T -> &T, variants not listed below means no match, e.g. T -> &mut T
            if (arg == decayed_arg) {
                // (rvalue) T -> T / move &T / &T
                if (arg == param) return Exact;
                if (auto ref_type = param->dyn_cast<ReferenceType>()) {
                    return ref_type->is_moved_ ? Referenced : QualifiedReferenced;
                }
                return NoMatch;
            } else if (!arg->cast<ReferenceType>()->is_moved_) {
                // (lvalue) &T -> &T / &mut T / T, &mut T -> &mut T / &T / T
                if (auto ref_type = param->dyn_cast<ReferenceType>()) {
                    if (ref_type->is_moved_) return NoMatch;
                    return (is_arg_mutable ^ (ref_type->referenced_type_->kind_ == Kind::Mutable))
                               ? Qualified
                               : Exact;
                } else {
                    return Copy;
                }
            } else {
                // (xvalue) move &T -> move &T / &mut T / &T / T
                if (auto ref_type = param->dyn_cast<ReferenceType>()) {
                    if (ref_type->referenced_type_->kind_ == Kind::Mutable) return Referenced;
                    return ref_type->is_moved_ ? Exact : QualifiedReferenced;
                } else {
                    return Copy;
                }
            }
        } else {
            /// TODO:
            // upcast in class hierarchy, or to void pointer
            if (!decayed_arg->dyn_cast<PointerType>() || !decayed_param->dyn_cast<PointerType>())
                return NoMatch;
            auto param_class =
                decayed_param->cast<PointerType>()->pointed_type_->dyn_cast<InstanceType>();
            auto arg_class =
                decayed_arg->cast<PointerType>()->pointed_type_->dyn_cast<InstanceType>();
            if (!param_class || !arg_class) return NoMatch;
            if (static_cast<const Type*>(param_class) == &VoidType::instance) return Upcast;
            const Type* current_base = arg_class;
            while (current_base) {
                if (current_base == param) {
                    return Upcast;
                }
                if (auto current_instance = current_base->dyn_cast<InstanceType>()) {
                    current_base = current_instance->extends_;
                } else {
                    break;
                }
            }
            return NoMatch;
        }
    }

    static auto overload_rank(FunctionObject func, std::span<const Type*> arg_types)
        -> ConversionRank {
        const FunctionType* func_type = get_func_type(func);
        if (func_type->parameters_.size() != arg_types.size()) {
            return ConversionRank::NoMatch;
        }
        ConversionRank worst_rank = ConversionRank::Exact;
        for (std::size_t i = 0; i < arg_types.size(); ++i) {
            ConversionRank rank = param_rank(func_type->parameters_[i], arg_types[i]);
            if (rank > worst_rank) {
                worst_rank = rank;
                if (rank == ConversionRank::NoMatch) {
                    return ConversionRank::NoMatch;
                }
            }
        }
        return worst_rank;
    }

    static auto overload_partial_order(
        FunctionObject left, FunctionObject right, std::span<const Type*> arg_types
    ) -> std::partial_ordering {
        const FunctionType* left_type = get_func_type(left);
        const FunctionType* right_type = get_func_type(right);
        bool left_ever_better = false;
        bool right_ever_better = false;
        for (std::size_t i = 0; i < arg_types.size(); ++i) {
            ConversionRank left_rank = param_rank(left_type->parameters_[i], arg_types[i]);
            ConversionRank right_rank = param_rank(right_type->parameters_[i], arg_types[i]);
            if (left_rank < right_rank) {
                left_ever_better = true;
            } else if (right_rank < left_rank) {
                right_ever_better = true;
            }
        }
        if (left_ever_better && !right_ever_better) {
            return std::partial_ordering::less;
        } else if (!left_ever_better && right_ever_better) {
            return std::partial_ordering::greater;
        } else if (left_ever_better && right_ever_better) {
            return std::partial_ordering::unordered;
        } else {
            return std::partial_ordering::equivalent;
        }
    }
};

class AccessHandler final : public GlobalMemory::MonotonicAllocated {
private:
    Sema& sema_;

public:
    AccessHandler(Sema& sema) noexcept : sema_(sema) {}

    auto eval_access(const ASTMemberAccess* node) noexcept -> Symbol;

    auto eval_pointer(const ASTPointerAccess* node) noexcept -> Symbol;

    auto eval_index(const ASTIndexAccess* node) noexcept -> Symbol;

private:
    auto eval_static_access(Scope* static_scope, strview member) noexcept -> Symbol {
        auto* next = static_scope->find(member);
        if (!next) {
            Diagnostic::error_undeclared_identifier(member);
            return {};
        }
        if (auto* next_namespace = next->get<Scope*>()) {
            return next_namespace;
        } else if (auto* family = next->get<TemplateFamily*>()) {
            return family;
        } else {
            return sema_.eval_symbol(member, *static_scope, *next);
        }
    }

    auto eval_struct_access(Term object, strview member) -> Term {
        Term decayed = Sema::decay(object);
        bool is_mutable = Sema::is_mutable(object);
        if (auto struct_type = decayed->dyn_cast<StructType>()) {
            auto attr_it = struct_type->fields_.find(member);
            if (attr_it != struct_type->fields_.end()) {
                return Sema::apply_mutable(Term::forward_like(object, attr_it->second), is_mutable);
            }

        } else {
            auto struct_value = decayed.get_comptime()->cast<StructValue>();
            auto attr_it = struct_value->type_->fields_.find(member);
            if (attr_it != struct_value->type_->fields_.end()) {
                return Sema::apply_mutable(Term::forward_like(object, attr_it->second), is_mutable);
            }
        }
        return {};
    }

    auto eval_instance_access(Term object, strview member) -> Symbol {
        Term decayed = Sema::decay(object);
        bool is_mutable = Sema::is_mutable(object);
        auto find_method = [&](Scope* scope) -> Symbol {
            const ScopeValue* value = scope->find(member);
            if (value->get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
                return BoundMethod{object, scope, value};
            }
            return {};
        };
        if (auto instance_type = decayed->cast<InstanceType>()) {
            auto attr_it = instance_type->attrs_.find(member);
            if (attr_it != instance_type->attrs_.end()) {
                return Sema::apply_mutable(Term::forward_like(object, attr_it->second), is_mutable);
            }
            return find_method(instance_type->scope_);
        } else {
            auto instance_value = decayed.get_comptime()->cast<InstanceValue>();
            auto attr_it = instance_value->attrs_.find(member);
            if (attr_it != instance_value->attrs_.end()) {
                return Sema::apply_mutable(Term::forward_like(object, attr_it->second), is_mutable);
            }
            return find_method(instance_value->type_->scope_);
        }
    }
};

class OperationHandler final : public GlobalMemory::MonotonicAllocated {
public:
    static auto is_primitive(Term operand) noexcept -> bool {
        if (operand) {
            Term decayed = Sema::decay(operand);
            return decayed->kind_ == Kind::Integer || decayed->kind_ == Kind::Float ||
                   decayed->kind_ == Kind::Boolean;
        }
        return true;
    }

private:
    Sema& sema_;

public:
    OperationHandler(Sema& sema) noexcept : sema_(sema) {}

    auto eval_type_op(OperatorCode opcode, const Type* left, const Type* right = nullptr) const
        -> const Type* {
        /// TODO: implement type-level operations
        return nullptr;
    }

    auto eval_primitive_op(OperatorCode opcode, Term left, Term right = {}) const noexcept -> Term {
        bool comptime = left.is_comptime() && (right ? right.is_comptime() : true);
        bool left_is_mutable = Sema::is_mutable(left);
        Term decayed_left = Sema::decay(left);
        Term decayed_right = Sema::decay(right);
        const Type* left_type = decayed_left.effective_type();
        Kind left_kind = decayed_left->kind_;
        Kind right_kind = decayed_right ? decayed_right->kind_ : Kind::Unknown;
        if (comptime) {
            Value* left_value = decayed_left.get_comptime();
            Value* right_value = decayed_right ? decayed_right.get_comptime() : nullptr;
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

    auto eval_overloaded_op(
        const ASTNode* node, OperatorCode opcode, Term left, Term right = {}
    ) const noexcept -> Term {
        const Type* left_type = Sema::decay(left.effective_type());
        const Type* right_type = right ? Sema::decay(right.effective_type()) : nullptr;
        if (left_type->kind_ != Kind::Instance && right_type &&
            right_type->kind_ != Kind::Instance) {
            throw;
        }
        std::array<Term, 2> args = {left, right};
        if (opcode == OperatorCode::PostIncrement || opcode == OperatorCode::PostDecrement) {
            args[1] = Term::prvalue(&IntegerValue::zero);
        }
        return sema_.call_handler_->eval_call(
            node,
            std::tuple{opcode, left_type, right_type},
            args[1] ? args : std::span(args).subspan(0, 1)
        );
    }

    auto eval_cast(const ASTAs* node, Term operand, const Type* target_type) const noexcept
        -> Term {
        Term decayed = Sema::decay(operand);
        bool convertible = false;
        const Type* source_type = decayed.effective_type();
        if (source_type == target_type) {
            return operand;
        } else if (operand->kind_ == Kind::Integer || operand->kind_ == Kind::Float) {
            if (target_type->kind_ == Kind::Integer || target_type->kind_ == Kind::Float) {
                convertible = true;
            }
        } else if (operand->kind_ == Kind::Nullptr) {
            if (target_type->kind_ == Kind::Pointer) {
                convertible = true;
            }
        } else if (operand->kind_ == Kind::Pointer) {
            if (target_type->kind_ == Kind::Pointer) {
                if (TypeRegistry::is_reachable(source_type, target_type)) {
                    convertible = true;
                }
            }
        }
        if (convertible) {
            sema_.codegen_env_.map_type(sema_.current_scope_, node, target_type);
            return Term::prvalue(target_type);
        }
        Diagnostic::error_invalid_cast(node->location, source_type->repr(), target_type->repr());
        return {};
    }
};

class TypeContextEvaluator {
private:
    Sema& sema_;
    TypeResolution& out_;
    bool require_sized_;

public:
    TypeContextEvaluator(Sema& sema, TypeResolution& out, bool require_sized_ = true) noexcept
        : sema_(sema), out_(out), require_sized_(require_sized_) {}

    void operator()(const ASTExprVariant& expr_variant) noexcept {
        std::visit(*this, expr_variant);
        if (out_.get() && !out_.is_sized() && require_sized_) {
            Diagnostic::error_circular_type_dependency(ASTNodePtrGetter{}(expr_variant)->location);
        }
    }

    void operator()(const ASTExpression* node) noexcept { UNREACHABLE(); }

    void operator()(const ASTExplicitTypeExpr* node) noexcept { UNREACHABLE(); }

    void operator()(const ASTSelfExpr* node) noexcept {
        if (!node->is_type) {
            Diagnostic::ErrorTrap trap{node->location};
            Diagnostic::error_symbol_category_mismatch(node->location, "type", "value");
        } else {
            out_ = sema_.get_self_type();
            if (!out_.get()) {
                /// TODO: throw not in class error
                throw;
            }
        }
    }

    void operator()(const ASTParenExpr* node) noexcept { (*this)(node->inner); }

    void operator()(const ASTConstant* node) noexcept {
        /// TODO: literal types
        throw;
        out_ = {};
    }

    void operator()(const ASTIdentifier* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        out_ = sema_.lookup_type(node->str);
    }

    void operator()(const ASTMemberAccess* node) noexcept {
        Symbol result = sema_.access_handler_->eval_access(node);
        if (auto* type = Sema::get_if<SymbolKind::Type>(result)) {
            out_ = *type;
        }
    }

    void operator()(const ASTPointerAccess* node) noexcept {
        Symbol result = sema_.access_handler_->eval_pointer(node);
        if (auto* type = Sema::get_if<SymbolKind::Type>(result)) {
            out_ = *type;
        }
    }

    void operator()(const ASTUnaryOp* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        TypeResolution expr_result;
        TypeContextEvaluator{sema_, expr_result, false}(node->expr);
        out_ = TypeResolution(sema_.operation_handler_->eval_type_op(node->opcode, expr_result));
    }

    void operator()(const ASTBinaryOp* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        TypeResolution left_result;
        TypeContextEvaluator{sema_, left_result}(node->left);
        TypeResolution right_result;
        TypeContextEvaluator{sema_, right_result}(node->right);
        out_ = sema_.operation_handler_->eval_type_op(node->opcode, left_result, right_result);
    }

    void operator()(const ASTStructInitialization* node) noexcept {
        Diagnostic::error_symbol_category_mismatch(node->location, "type", "struct initializer");
    }

    void operator()(const ASTFunctionCall* node) noexcept {
        Diagnostic::error_symbol_category_mismatch(node->location, "type", "function call");
    }

    void operator()(const ASTPrimitiveType* node) noexcept { out_ = node->type; }

    void operator()(const ASTStringViewType* node) noexcept {
        Symbol string_view_symbol = sema_.get_std_symbol("string_view");
        out_ = Sema::get<SymbolKind::Type>(string_view_symbol);
    }

    void operator()(const ASTFunctionType* node) noexcept {
        out_ = std::type_identity<FunctionType>();
        GlobalMemory::Vector<const Type*> param_types;
        param_types.reserve(node->parameter_types.size());
        for (ASTExprVariant param_expr : node->parameter_types) {
            TypeResolution param_type;
            TypeContextEvaluator{sema_, param_type}(param_expr);
            param_types.push_back(param_type);
        }
        TypeResolution return_type;
        TypeContextEvaluator{sema_, return_type}(node->return_type);
        if (std::ranges::contains(param_types, nullptr) || return_type.get() == nullptr) {
            out_ = nullptr;
            return;
        }
        TypeRegistry::get_at<FunctionType>(
            out_, param_types | GlobalMemory::collect<std::span>(), return_type
        );
    }

    void operator()(const ASTStructType* node) noexcept {
        out_ = std::type_identity<StructType>();
        bool has_error = false;
        GlobalMemory::FlatMap<strview, const Type*> field_map;
        for (const ASTFieldDeclaration& decl : node->fields) {
            TypeResolution field_type;
            TypeContextEvaluator{sema_, field_type}(decl.type);
            if (!field_type.get()) {
                has_error = true;
            }
            field_map.insert({decl.identifier, field_type.get()});
        }
        if (has_error) {
            out_ = nullptr;
            return;
        }
        TypeRegistry::get_at<StructType>(out_, field_map);
    }

    void operator()(const ASTArrayType* node) noexcept;

    void operator()(const ASTMutableType* node) noexcept {
        out_ = std::type_identity<MutableType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.get()) {
            out_ = nullptr;
            return;
        }
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<MutableType>(out_, expr_type);
    }

    void operator()(const ASTReferenceType* node) noexcept {
        out_ = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.get()) {
            out_ = nullptr;
            return;
        }
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out_, expr_type, node->is_moved);
    }

    void operator()(const ASTPointerType* node) noexcept {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.get()) {
            out_ = nullptr;
            return;
        }
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        out_ = new InstanceType(node->identifier);
        Sema::Guard guard(sema_, node);
        sema_.current_scope_->self_type_ = out_;
        bool has_error = false;
        const Type* base = nullptr;
        if (!holds_monostate(node->extends)) {
            TypeResolution result;
            TypeContextEvaluator{sema_, result}(node->extends);
            base = result.get();
            has_error = !base;
        }
        GlobalMemory::Vector<const Type*> interfaces;
        for (ASTExprVariant interface : node->implements) {
            TypeResolution result;
            TypeContextEvaluator{sema_, result}(interface);
            has_error |= !result.get();
            interfaces.push_back(result.get());
        }
        GlobalMemory::FlatMap<strview, const Type*> attrs;
        for (ASTNodeVariant decl_variant : node->fields) {
            const ASTDeclaration* decl = std::get<const ASTDeclaration*>(decl_variant);
            TypeResolution field_type;
            TypeContextEvaluator{sema_, field_type}(decl->declared_type);
            has_error |= !field_type.get();
            attrs.insert({decl->identifier, field_type.get()});
        }
        if (has_error) {
            out_ = nullptr;
            return;
        }
        TypeRegistry::get_at<InstanceType>(
            out_, sema_.current_scope_, node->identifier, base, interfaces, std::move(attrs)
        );
    }

    void operator()(const ASTTemplateInstantiation* node) noexcept {
        Symbol result = sema_.template_handler_->instantiate(node);
        if (Sema::expect(result, SymbolKind::Type)) {
            out_ = Sema::get<SymbolKind::Type>(result);
        }
    }

    void operator()(const auto* node) noexcept { UNREACHABLE(); }
};

/// TODO: expected parameter is not working
class ValueContextEvaluator {
private:
    Sema& sema_;
    const Type* expected_;
    bool require_comptime_;
    bool template_pattern_mode_;

private:
    explicit ValueContextEvaluator(
        ValueContextEvaluator& other, const Type* expected = nullptr
    ) noexcept
        : sema_(other.sema_),
          expected_(expected),
          require_comptime_(other.require_comptime_),
          template_pattern_mode_(other.template_pattern_mode_) {}

public:
    explicit ValueContextEvaluator(
        Sema& sema,
        const Type* expected = nullptr,
        bool require_comptime = false,
        bool template_pattern_mode = false
    ) noexcept
        : sema_(sema),
          expected_(expected),
          require_comptime_(require_comptime),
          template_pattern_mode_(template_pattern_mode) {}

    auto operator()(const ASTExprVariant& variant) noexcept -> Symbol {
        if (std::visit(
                [](auto node) -> bool {
                    return std::is_convertible_v<decltype(node), const ASTExplicitTypeExpr*>;
                },
                variant
            )) {
            TypeResolution out;
            TypeContextEvaluator{sema_, out}(variant);
            return out.get();
        }
        return std::visit(*this, variant);
    }

    auto operator()(std::monostate) noexcept -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTExpression* node) noexcept -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTExplicitTypeExpr* node) noexcept -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTParenExpr* node) noexcept -> Symbol { return (*this)(node->inner); }

    auto operator()(const ASTConstant* node) noexcept -> Symbol {
        if (expected_) {
            Diagnostic::ErrorTrap trap{node->location};
            Value* typed_value = node->value->resolve_to(expected_);
            return Term::prvalue(typed_value);
        } else {
            return Term::prvalue(node->value);
        }
    }

    auto operator()(const ASTStringConstant* node) noexcept -> Symbol {
        Symbol string_view_symbol = sema_.get_std_symbol("string_view");
        TypeResolution strview_type_res = Sema::get<SymbolKind::Type>(string_view_symbol);
        return Term::prvalue(strview_type_res.get());
    }

    auto operator()(const ASTSelfExpr* node) noexcept -> Symbol {
        if (node->is_type) {
            return sema_.get_self_type();
        } else {
            Symbol result = sema_.lookup_symbol("self");
            assert(Sema::get_if<SymbolKind::Term>(result));
            return result;
        }
    }

    auto operator()(const ASTIdentifier* node) noexcept -> Symbol {
        Diagnostic::ErrorTrap trap{node->location};
        Symbol symbol = sema_.lookup_symbol(node->str);
        if (require_comptime_) {
            if (auto* term = Sema::get_if<SymbolKind::Term>(symbol); term && !term->is_comptime()) {
                Diagnostic::error_not_constant_expression(node->location);
                return {};
            }
        }
        assert(
            Sema::get_if<SymbolKind::Term>(symbol)
                ? Sema::get<SymbolKind::Term>(symbol).get() != nullptr
                : true
        );
        return symbol;
    }

    auto operator()(const ASTMemberAccess* node) noexcept -> Symbol {
        return sema_.access_handler_->eval_access(node);
    }

    auto operator()(const ASTIndexAccess* node) noexcept -> Symbol {
        return sema_.access_handler_->eval_index(node);
    }

    auto operator()(const ASTAddressOfExpr* node) noexcept -> Symbol {
        Symbol operand_symbol = ValueContextEvaluator{*this, nullptr}(node->operand);
        if (!Sema::expect(operand_symbol, SymbolKind::Term)) {
            return {};
        }
        Term operand_term = Sema::get<SymbolKind::Term>(operand_symbol);
        return Term::prvalue(TypeRegistry::get<PointerType>(operand_term.effective_type()));
    }

    auto operator()(const ASTDereference* node) noexcept -> Symbol {
        Symbol operand_symbol = ValueContextEvaluator{*this, nullptr}(node->operand);
        if (!Sema::expect(operand_symbol, SymbolKind::Term)) {
            return {};
        }
        Term operand_term = Sema::get<SymbolKind::Term>(operand_symbol);
        if (auto* pointer_type = operand_term.effective_type()->dyn_cast<PointerType>()) {
            return Term::lvalue(pointer_type->pointed_type_);
        }
        Diagnostic::error_operation_not_defined(
            GetOperatorString(OperatorCode::Deref), operand_term.effective_type()->repr()
        );
        return {};
    }

    auto operator()(const ASTUnaryOp* node) noexcept -> Symbol {
        Symbol expr_symbol = ValueContextEvaluator{*this, nullptr}(node->expr);
        if (!Sema::expect(expr_symbol, SymbolKind::Term)) {
            return {};
        }
        Term expr_term = Sema::get<SymbolKind::Term>(expr_symbol);
        if (OperationHandler::is_primitive(expr_term)) {
            return sema_.operation_handler_->eval_primitive_op(
                node->opcode, Sema::get<SymbolKind::Term>(expr_symbol)
            );
        } else {
            return sema_.operation_handler_->eval_overloaded_op(node, node->opcode, expr_term);
        }
    }

    auto operator()(const ASTBinaryOp* node) noexcept -> Symbol {
        Diagnostic::ErrorTrap trap{node->location};
        Symbol left_symbol = ValueContextEvaluator{*this, nullptr}(node->left);
        Symbol right_symbol = ValueContextEvaluator{*this, nullptr}(node->right);
        bool any_error = !Sema::expect(left_symbol, SymbolKind::Term);
        any_error |= !Sema::expect(right_symbol, SymbolKind::Term);
        if (any_error) {
            return {};
        }
        Term left_term = Sema::get<SymbolKind::Term>(left_symbol);
        Term right_term = Sema::get<SymbolKind::Term>(right_symbol);
        if (OperationHandler::is_primitive(left_term) &&
            OperationHandler::is_primitive(right_term)) {
            return sema_.operation_handler_->eval_primitive_op(node->opcode, left_term, right_term);
        } else {
            return sema_.operation_handler_->eval_overloaded_op(
                node,
                node->opcode,
                Sema::get<SymbolKind::Term>(left_symbol),
                Sema::get<SymbolKind::Term>(right_symbol)
            );
        }
    }

    auto operator()(const ASTFieldInitialization* node) noexcept -> Symbol {
        Diagnostic::ErrorTrap trap{node->location};
        Symbol value_symbol = ValueContextEvaluator{*this, nullptr}(node->value);
        if (!Sema::expect(value_symbol, SymbolKind::Term)) {
            return {};
        }
        return value_symbol;
    }

    auto operator()(const ASTStructInitialization* node) noexcept -> Symbol {
        /// TODO: constexpr
        Diagnostic::ErrorTrap trap{node->location};
        const Type* type = nullptr;
        if (!holds_monostate(node->struct_type)) {
            TypeResolution struct_type;
            TypeContextEvaluator{sema_, struct_type}(node->struct_type);
            type = struct_type;
            sema_.codegen_env_.map_type(sema_.current_scope_, node, struct_type);
        } else if (auto* self = sema_.get_self_type()) {
            type = self;
        }
        if (type->dyn_cast<InstanceType>() && sema_.get_self_type() != type) {
            Diagnostic::error_construct_instance_out_of_class(node->location, type->repr());
        }
        GlobalMemory::FlatMap<strview, Term> inits;
        for (const ASTFieldInitialization& init : node->field_inits) {
            Symbol value_symbol = ValueContextEvaluator{*this, nullptr}(init.value);
            if (!Sema::expect(value_symbol, SymbolKind::Term)) {
                return Term::prvalue(type);
            }
            inits.emplace(init.identifier, Sema::get<SymbolKind::Term>(value_symbol));
        }
        return StructType::construct(type, inits);
    }

    auto operator()(const ASTArrayInitialization* node) noexcept -> Symbol {
        /// TODO: array element type inference and constexpr
        Diagnostic::ErrorTrap trap{node->location};
        if (expected_) {
            Symbol array_type_symbol = sema_.get_std_symbol("array"sv);
            const void* array_template =
                Sema::get<SymbolKind::Template>(array_type_symbol)->primary;
            auto* instance_type = expected_->dyn_cast<InstanceType>();
            if (!instance_type || instance_type->primary_template_ != array_template) {
                Diagnostic::error_symbol_category_mismatch(
                    node->location, expected_->repr(), "array initializer"
                );
                return {};
            }
            const Type* element_type = instance_type->template_args_[0]->cast<Type>();
            const IntegerValue* size_value = instance_type->template_args_[1]->cast<IntegerValue>();
            if (node->elements.size() != static_cast<std::size_t>(size_value->value_)) {
                ;
                Diagnostic::error_array_initializer_size_mismatch(
                    node->location, static_cast<size_t>(size_value->value_), node->elements.size()
                );
                return {};
            }
            GlobalMemory::Vector<Term> elements;
            elements.reserve(node->elements.size());
            for (const ASTExprVariant& element : node->elements) {
                Symbol element_symbol = ValueContextEvaluator{*this, element_type}(element);
                if (!Sema::expect(element_symbol, SymbolKind::Term)) {
                    return {};
                }
                if (holds_monostate(element_symbol)) {
                    elements.push_back(Term{});
                } else {
                    Term element_term = Sema::get<SymbolKind::Term>(element_symbol);
                    elements.push_back(element_term);
                }
            }
            return Term::prvalue(expected_);
        } else {
            assert(false);
        }
    }

    auto operator()(const ASTFunctionCall* node) noexcept -> Symbol {
        GlobalMemory::Vector<Term> args_terms;
        args_terms.reserve(node->arguments.size());
        for (const ASTExprVariant& arg : node->arguments) {
            Symbol arg_symbol = ValueContextEvaluator{*this, nullptr}(arg);
            if (!Sema::expect(arg_symbol, SymbolKind::Term)) {
                return {};
            }
            args_terms.push_back(Sema::get<SymbolKind::Term>(arg_symbol));
        }
        Symbol func_symbol = ValueContextEvaluator{*this, nullptr}(node->function);
        if (holds_monostate(func_symbol)) return {};
        return sema_.call_handler_->eval_call(node, func_symbol, args_terms);
    }

    auto operator()(const ASTTemplateInstantiation* node) noexcept -> Symbol {
        return sema_.template_handler_->instantiate(node);
    }

    auto operator()(const ASTAs* node) noexcept -> Symbol {
        Symbol expr_symbol = ValueContextEvaluator{*this, nullptr}(node->expr);
        if (!Sema::expect(expr_symbol, SymbolKind::Term)) {
            return {};
        }
        Term expr_term = Sema::get<SymbolKind::Term>(expr_symbol);
        TypeResolution target_type;
        TypeContextEvaluator{sema_, target_type}(node->target_type);
        return sema_.operation_handler_->eval_cast(node, expr_term, target_type);
    }

    auto operator()(const ASTLambda* node) noexcept -> Symbol {
        std::span<const Type*> param_types =
            node->parameters | std::views::transform([&](const ASTFunctionParameter& param) {
                TypeResolution param_type;
                TypeContextEvaluator{sema_, param_type}(param.type);
                return param_type.get();
            }) |
            GlobalMemory::collect<std::span>();
        TypeResolution return_type;
        if (!holds_monostate(node->return_type)) {
            TypeContextEvaluator{sema_, return_type}(node->return_type);
        } else {
            return_type = &VoidType::instance;
        }
        const Type* func_type = TypeRegistry::get<FunctionType>(param_types, return_type);
        sema_.codegen_env_.map_type(sema_.current_scope_, node, func_type);
        return Term::prvalue(func_type);
    }

    auto operator()(const auto* node) noexcept -> Symbol { UNREACHABLE(); }
};

class TypeCheckVisitor {
private:
    Sema& sema_;

public:
    TypeCheckVisitor(Sema& sema) noexcept : sema_(sema) {}

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
        Sema::Guard guard(sema_, node);
        for (auto stmt : node->statements) {
            (*this)(stmt);
        }
    }

    // Expressions
    void operator()(const ASTExpression* node) noexcept { UNREACHABLE(); }

    // Statements
    void operator()(const ASTExpressionStatement* node) noexcept {
        ValueContextEvaluator{sema_, nullptr, false}(node->expr);
    }

    void operator()(const ASTDeclaration* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        if (!holds_monostate(node->declared_type)) {
            TypeResolution declared_type;
            TypeContextEvaluator{sema_, declared_type}(node->declared_type);
            sema_.codegen_env_.map_type(sema_.current_scope_, node, declared_type);
        } else if (!std::holds_alternative<std::monostate>(node->expr)) {
            Symbol init = ValueContextEvaluator{sema_, nullptr, false}(node->expr);
            if (!Sema::expect(init, SymbolKind::Term)) {
                return;
            }
            sema_.codegen_env_.map_type(
                sema_.current_scope_, node, std::get<Term>(init).effective_type()
            );
        }
    }

    void operator()(const ASTTypeAlias* node) noexcept { sema_.lookup_type(node->identifier); }

    void operator()(const ASTIfStatement* node) noexcept {
        Sema::Guard guard(sema_, node);
        ValueContextEvaluator{sema_, &BooleanType::instance, false}(node->condition);
        (*this)(node->if_block);
        if (!holds_monostate(node->else_block)) {
            (*this)(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            ValueContextEvaluator{sema_, nullptr, false}(*decl);
        } else if (
            auto* expr = std::get_if<ASTExprVariant>(&node->initializer);
            expr && !holds_monostate(*expr)
        ) {
            ValueContextEvaluator{sema_, nullptr, false}(*expr);
        }
        if (!std::holds_alternative<std::monostate>(node->condition)) {
            ValueContextEvaluator{sema_, &BooleanType::instance, false}(node->condition);
        }
        if (!std::holds_alternative<std::monostate>(node->increment)) {
            ValueContextEvaluator{sema_, nullptr, false}(node->increment);
        }
        (*this)(node->body);
    }

    void operator()(const ASTReturnStatement* node) noexcept {
        if (!std::holds_alternative<std::monostate>(node->expr)) {
            ValueContextEvaluator{sema_, nullptr, false}(node->expr);
        }
    }

    // Functions and classes
    void operator()(const ASTFunctionDefinition* node) noexcept {
        FunctionObject func_obj = sema_.call_handler_->get_func_obj(sema_.current_scope_, node);
        Sema::Guard guard(sema_, node);
        if (func_obj) {
            sema_.codegen_env_.add_function_output(
                sema_.current_scope_, node, CallHandler::get_func_type(func_obj)
            );
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTCtorDtorDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (node->is_constructor) {
            FunctionObject func_obj = sema_.call_handler_->get_func_obj(sema_.current_scope_, node);
            if (func_obj) {
                sema_.codegen_env_.add_function_output(
                    sema_.current_scope_, node, CallHandler::get_func_type(func_obj)
                );
            }
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTOperatorDefinition* node) noexcept {
        FunctionObject func_obj = sema_.call_handler_->get_func_obj(sema_.current_scope_, node);
        Sema::Guard guard(sema_, node);
        if (func_obj) {
            sema_.codegen_env_.add_function_output(
                sema_.current_scope_, node, CallHandler::get_func_type(func_obj)
            );
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        TypeResolution class_type =
            sema_.lookup_type(node->identifier);  // trigger self type injection
        Sema::Guard guard(sema_, node);
        sema_.current_scope_->self_type_ = class_type;
        for (auto& field : node->fields) {
            (*this)(field);
        }
        for (auto& item : node->scope_items) {
            (*this)(item);
        }
    }

    void operator()(const ASTEnumDefinition* node) noexcept {}

    void operator()(const ASTNamespaceDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        for (const auto& item : node->items) {
            (*this)(item);
        }
    }

    void operator()(const ASTStaticAssertStatement* node) noexcept {
        Symbol result = ValueContextEvaluator{sema_, &BooleanType::instance, true}(node->condition);
        if (holds_monostate(result) ||
            !std::get<Term>(result).get_comptime()->cast<BooleanValue>()->value_) {
            // Diagnostic::report(StaticAssertFailedError(node->location, node->message));
            throw;
        }
    }
};

// ========== Implementation ==========

inline Sema::Sema(Scope& root, CodeGenEnvironment& codegen_env) noexcept
    : current_scope_(&root),
      codegen_env_(codegen_env),
      template_handler_(std::make_unique<TemplateHandler>(*this)),
      operation_handler_(std::make_unique<OperationHandler>(*this)),
      access_handler_(std::make_unique<AccessHandler>(*this)),
      call_handler_(std::make_unique<CallHandler>(*this)) {}

inline auto Sema::eval_type(strview identifier, Scope& scope, const ScopeValue& value) noexcept
    -> TypeResolution {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        }
        Diagnostic::error_symbol_category_mismatch("type", "value");
        return nullptr;
    }
    // Check cache
    auto [it_id_cache, inserted] = type_cache_.insert({{&scope, identifier}, TypeResolution()});
    if (!inserted) {
        return it_id_cache->second;
    }
    // Cache miss; resolve
    Guard guard(*this, scope);
    if (auto type_alias = value.get<const ASTExprVariant*>()) {
        TypeContextEvaluator{*this, it_id_cache->second}(*type_alias);
    } else if (auto class_def = value.get<const ASTClassDefinition*>()) {
        TypeContextEvaluator{*this, it_id_cache->second}(class_def);
    } else {
        Diagnostic::error_symbol_category_mismatch("type", "value");
        return nullptr;
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

inline auto Sema::eval_symbol(strview identifier, Scope& scope, const ScopeValue& value) noexcept
    -> Symbol {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        } else {
            return Term::prvalue(const_cast<Value*>(object->cast<Value>()));
        }
    } else if (auto* pack = value.get<std::span<const Object*>*>()) {
        return *pack;
    } else if (value.get<const ASTExprVariant*>() || value.get<const ASTClassDefinition*>()) {
        TypeResolution out = eval_type(identifier, scope, value);
        return out.get() ? Symbol{out.get()} : Symbol{};
    } else if (auto var_init = value.get<const VariableInitialization*>()) {
        TypeResolution type{};
        if (!holds_monostate(var_init->type)) {
            Guard guard(*this, scope);
            TypeContextEvaluator{*this, type}(var_init->type);
            if (!type.get()) return {};
        }
        if (holds_monostate(var_init->value)) {
            return type.get() ? Symbol{Term::lvalue(type.get())} : Symbol{};
        } else {
            Symbol init_symbol = ValueContextEvaluator{*this, type.get(), false}(var_init->value);
            if (!Sema::get_if<SymbolKind::Term>(init_symbol)) return {};
            Term init = Term::lvalue(std::get<Term>(init_symbol));
            assert(init.get());
            if (type.get()) {
                AutoBindings auto_bindings;
                if (!type->assignable_from(init.effective_type(), auto_bindings)) {
                    Diagnostic::error_type_mismatch(type->repr(), init.effective_type()->repr());
                    return {};
                }
                return apply_mutable(Term::lvalue(type.get()), var_init->is_mutable);
            } else {
                return apply_mutable(Term::lvalue(init.effective_type()), var_init->is_mutable);
            }
        }
    } else if (value.get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
        return std::pair{&scope, &value};
    } else if (auto* template_family = value.get<TemplateFamily*>()) {
        return template_family;
    } else if (auto* scope_ptr = value.get<Scope*>()) {
        return scope_ptr;
    }
    UNREACHABLE();
}

inline auto Sema::deferred_analysis(Scope& scope, auto variant) noexcept -> void {
    SymbolCollector{scope}(variant);
    Guard guard(*this, scope);
    TypeCheckVisitor{*this}(variant);
}

inline auto TemplateHandler::inference(
    Scope* scope, const ASTTemplateDefinition* primary, std::span<const Type*> args
) noexcept -> FunctionObject {
    // check if variadic argument count matches
    std::size_t param_count = 0;
    strview variadic_param =
        primary->parameters.back().is_variadic ? primary->parameters.back().identifier : strview{};
    if (auto* func_def = std::get_if<const ASTFunctionDefinition*>(&primary->target_node)) {
        param_count = (*func_def)->parameters.size();
    } else if (auto* ctor_def = std::get_if<const ASTCtorDtorDefinition*>(&primary->target_node)) {
        param_count = (*ctor_def)->parameters.size();
    } else if (auto* op_def = std::get_if<const ASTOperatorDefinition*>(&primary->target_node)) {
        param_count = 1 + static_cast<bool>((*op_def)->right);
    } else {
        UNREACHABLE();
    }
    if (param_count != args.size() && variadic_param.empty()) {
        return {};
    }
    // prepare auto instances
    GlobalMemory::Vector<bool> is_nttp_flags =
        primary->parameters | std::views::transform(&ASTTemplateParameter::is_nttp) |
        GlobalMemory::collect<GlobalMemory::Vector>();
    for (std::size_t i = param_count; i < args.size(); i++) {
        is_nttp_flags.push_back(false);
    }
    GlobalMemory::Vector<const Object*> auto_instances =
        TypeRegistry::get_auto_instances(is_nttp_flags);
    Scope& pattern_scope = Scope::make(*scope);
    for (std::size_t i = 0; i < primary->parameters.size(); i++) {
        const ASTTemplateParameter& param = primary->parameters[i];
        pattern_scope.set_template_argument(param.identifier, auto_instances[i]);
    }
    // make patterns
    Sema::Guard guard(sema_, pattern_scope);
    GlobalMemory::Vector<const Type*> patterns;
    if (auto* func_def = std::get_if<const ASTFunctionDefinition*>(&primary->target_node)) {
        for (const auto& pattern : (*func_def)->parameters) {
            Symbol pattern_symbol =
                ValueContextEvaluator{sema_, nullptr, false, true}(pattern.type);
            patterns.push_back(Sema::get<SymbolKind::Type>(pattern_symbol));
        }
        for (size_t i = param_count; i < args.size(); i++) {
            const Object* auto_inst = auto_instances[i - param_count + primary->parameters.size()];
            pattern_scope.set_template_argument(variadic_param, auto_inst);
            Symbol pattern_symbol = ValueContextEvaluator{
                sema_, nullptr, false, true
            }((*func_def)->parameters.back().type);
            patterns.push_back(Sema::get<SymbolKind::Type>(pattern_symbol));
        }
    } else if (auto* ctor_def = std::get_if<const ASTCtorDtorDefinition*>(&primary->target_node)) {
        for (const auto& pattern : (*ctor_def)->parameters) {
            Symbol pattern_symbol =
                ValueContextEvaluator{sema_, nullptr, false, true}(pattern.type);
            patterns.push_back(Sema::get<SymbolKind::Type>(pattern_symbol));
        }
        for (size_t i = param_count; i < args.size(); i++) {
            const Object* auto_inst = auto_instances[i - param_count + primary->parameters.size()];
            pattern_scope.set_template_argument(variadic_param, auto_inst);
            Symbol pattern_symbol = ValueContextEvaluator{
                sema_, nullptr, false, true
            }((*func_def)->parameters.back().type);
            patterns.push_back(Sema::get<SymbolKind::Type>(pattern_symbol));
        }
    } else if (auto* op_def = std::get_if<const ASTOperatorDefinition*>(&primary->target_node)) {
        Symbol left_pattern_symbol =
            ValueContextEvaluator{sema_, nullptr, false, true}((*op_def)->left.type);
        patterns.push_back(Sema::get<SymbolKind::Type>(left_pattern_symbol));
        if ((*op_def)->right) {
            Symbol right_pattern_symbol =
                ValueContextEvaluator{sema_, nullptr, false, true}((*op_def)->right->type);
            patterns.push_back(Sema::get<SymbolKind::Type>(right_pattern_symbol));
        }
    } else {
        // template cannot appears on another template
        UNREACHABLE();
    }
    // pattern match and auto binding
    AutoBindings auto_bindings;
    for (std::size_t i = 0; i < patterns.size(); i++) {
        if (!patterns[i]->assignable_from(args[i], auto_bindings)) {
            // Diagnostic::report(TemplateArgumentTypeMismatchError(
            //     primary->parameters[i].identifier, args[i]->repr(), patterns[i]->repr()
            // ));
            return {};
        }
    }
    GlobalMemory::Vector<const Object*> instantiation_args;
    for (auto& auto_inst : auto_instances) {
        instantiation_args.push_back(auto_bindings[auto_inst]);
    }
    // instantiate the template with auto bindings
    Symbol func_symbol = instantiate(scope, primary, instantiation_args);
    if (!holds_monostate(func_symbol)) {
        auto [inst_scope, value] = Sema::get<SymbolKind::Function>(func_symbol);
        FunctionOverloadDef overload_def =
            (*value->get<GlobalMemory::Vector<FunctionOverloadDef>*>())[0];
        if (auto* func_def = overload_def.get<const ASTFunctionDefinition*>()) {
            return sema_.call_handler_->get_func_obj(inst_scope, func_def);
        } else if (auto* ctor_def = overload_def.get<const ASTCtorDtorDefinition*>()) {
            return sema_.call_handler_->get_func_obj(inst_scope, ctor_def);
        } else if (auto* op_def = overload_def.get<const ASTOperatorDefinition*>()) {
            return sema_.call_handler_->get_func_obj(inst_scope, op_def);
        } else {
            UNREACHABLE();
        }
    }
    /// TODO: class initialization
    return {};
}

inline auto TemplateHandler::instantiate(const ASTTemplateInstantiation* node) noexcept -> Symbol {
    Symbol template_symbol = ValueContextEvaluator{sema_, nullptr, true}(node->template_expr);
    if (!Sema::expect(template_symbol, SymbolKind::Template, SymbolKind::Function)) {
        return {};
    }
    // std::span arg_terms =
    //     node->arguments | std::views::transform([&](ASTExprVariant arg) -> const Object* {
    //         Symbol result = ValueContextEvaluator{sema_, nullptr, true}(arg);
    //         if (Sema::expect(result, SymbolKind::Type, SymbolKind::Term)) {
    //             return Sema::get_if<SymbolKind::Type>(result)
    //                        ? static_cast<const Object*>(Sema::get<SymbolKind::Type>(result))
    //                        : Sema::get<SymbolKind::Term>(result).get_comptime();
    //         }
    //         return nullptr;
    //     }) |
    //     GlobalMemory::collect<std::span>();
    GlobalMemory::Vector<const Object*> arg_terms;
    for (const ASTExprVariant& arg : node->arguments) {
        Diagnostic::ErrorTrap trap{ASTNodePtrGetter{}(arg)->location};
        Symbol result = ValueContextEvaluator{sema_, nullptr, true}(arg);
        if (Sema::expect(result, SymbolKind::Type, SymbolKind::Term)) {
            arg_terms.push_back(
                Sema::get_if<SymbolKind::Type>(result)
                    ? static_cast<const Object*>(Sema::get<SymbolKind::Type>(result))
                    : Sema::get<SymbolKind::Term>(result).get_comptime()
            );
        } else {
            arg_terms.push_back(nullptr);
        }
    }
    if (std::ranges::contains(arg_terms, nullptr)) {
        return {};
    }
    if (auto* family = Sema::get_if<SymbolKind::Template>(template_symbol)) {
        return instantiate(**family, arg_terms);
    } else {
        // explicit template function instantiation is not supported
        throw;
    }
}

inline auto TemplateHandler::validate(
    const ASTTemplateDefinition& primary, TemplateArgs args
) noexcept -> bool {
    if (primary.parameters.size() != args.size() && !primary.parameters.back().is_variadic) {
        return false;
    }
    for (size_t i = 0; i < primary.parameters.size(); i++) {
        const auto& param = primary.parameters[i];
        if (static_cast<bool>(args[i]->dyn_value()) != param.is_nttp) {
            return false;
        }
        if (param.is_nttp) {
            // if (!holds_monostate(param.constraint)) {
            //     Symbol constraint_term =
            //         ValueContextEvaluator{sema_, nullptr, true}(param.constraint);
            //     if (!Sema::expect(constraint_term, SymbolKind::Type, SymbolKind::Term)) {
            //         return false;
            //     }
            //     Term constraint_term = std::get<Term>(constraint_term);
            //     if (constraint_term.is_type()) {
            //         // constraint is a type, check if the argument's type is assignable to it
            //         if (!constraint_term.get_type()->assignable_from(
            //                 args[i]->template cast<Value>()->get_type()
            //             )) {
            //             return false;
            //         }
            //     } else {
            //         // constraint is a concept, check if the argument satisfies it
            //         if (auto satisfies =
            //         constraint_term.get_comptime()->dyn_cast<BooleanValue>()) {
            //             if (!satisfies) return false;
            //         } else {
            //             /// invalid constraint
            //             throw;
            //         }
            //     }
            // }
        } else {
            /// TODO: type constraint validation
        }
    }
    for (size_t i = primary.parameters.size(); i < args.size(); i++) {
        /// TODO: variadic parameter validation
    }
    return true;
}

inline auto TemplateHandler::specialization_resolution(
    TemplateFamily& family, TemplateArgs args
) noexcept -> std::pair<Scope*, ASTNodeVariant> {
    // first, try to find a matching full specialization
    for (const auto* specialization : family.specializations) {
        if (specialization->parameters.size()) continue;
        const auto& prototype = get_prototype(*family.pattern_scope, *specialization);
        for (size_t i = 0; i < args.size(); i++) {
            static AutoBindings _;
            assert(_.empty());
            if (!prototype.patterns[i]->pattern_match(args[i], _)) {
                goto next_full_specialization;
            }
        }
        return {&Scope::make(*sema_.current_scope_), specialization->target_node};
    next_full_specialization:;
    }
    // second, try to find best matching partial specialization
    std::size_t viable_count = family.specializations.size();
    const ASTTemplateSpecialization* best_candidate = nullptr;
    const SpecializationPrototype* best_candidate_prototype = nullptr;
    AutoBindings best_auto_bindings;
    for (size_t i = 0; i < viable_count; i++) {
        const auto* specialization = family.specializations[i];
        if (specialization->parameters.size() == 0) continue;
        AutoBindings auto_bindings;
        const auto& prototype = get_prototype(*family.pattern_scope, *specialization);
        for (size_t j = 0; j < args.size(); j++) {
            if (!prototype.patterns[j]->pattern_match(args[j], auto_bindings)) {
                viable_count--;
                std::swap(family.specializations[i], family.specializations[viable_count]);
                i--;
                goto next_partial_specialization;
            }
        }
        if (!best_candidate) {
            best_candidate = specialization;
            best_candidate_prototype = &prototype;
            best_auto_bindings = std::move(auto_bindings);
        } else {
            std::partial_ordering better_than_best =
                compare_specialization(*best_candidate_prototype, prototype);
            if (better_than_best == std::partial_ordering::greater) {
                best_candidate = specialization;
                best_candidate_prototype = &prototype;
                best_auto_bindings = std::move(auto_bindings);
            }
        }
    next_partial_specialization:;
    }
    // third, re-iterate through all candidates to check for ambiguity
    GlobalMemory::Vector<const ASTTemplateSpecialization*> ambiguous_candidates;
    if (best_candidate) {
        for (const auto* specialization : std::span(family.specializations.begin(), viable_count)) {
            if (specialization == best_candidate) continue;
            const auto& prototype = get_prototype(*family.pattern_scope, *specialization);
            std::partial_ordering better_than_best =
                compare_specialization(*best_candidate_prototype, prototype);
            if (better_than_best == std::partial_ordering::equivalent ||
                better_than_best == std::partial_ordering::unordered) {
                ambiguous_candidates.push_back(specialization);
            }
        }
    }
    if (!ambiguous_candidates.empty()) {
        // Diagnostic::report(AmbiguousTemplateSpecializationError(
        //     family.primary.identifier, best_candidate->location,
        //     ambiguous_candidates |
        //     std::views::transform(&ASTTemplateSpecialization::location) |
        //         GlobalMemory::collect<GlobalMemory::Vector>()
        // ));
        throw;
        return {nullptr, std::monostate{}};
    }
    if (best_candidate) {
        Scope& inst_scope = Scope::make(*sema_.current_scope_);
        fill_bindings(*best_candidate, best_auto_bindings, inst_scope);
        return {&inst_scope, best_candidate->target_node};
    }
    // last, fallback to primary template
    Scope& inst_scope = Scope::make(*sema_.current_scope_);
    Sema::Guard guard(sema_, inst_scope);
    for (size_t i = 0; i < args.size(); i++) {
        inst_scope.set_template_argument(family.primary->parameters[i].identifier, args[i]);
    }
    return {&inst_scope, family.primary->target_node};
}

inline auto TemplateHandler::get_prototype(
    Scope& pattern_scope, const ASTTemplateSpecialization& specialization
) noexcept -> const SpecializationPrototype& {
    GlobalMemory::Vector<const Object*> auto_instances = TypeRegistry::get_auto_instances(
        specialization.parameters | std::views::transform(&ASTTemplateParameter::is_nttp)
    );
    Sema::Guard guard(sema_, pattern_scope);
    auto [it, inserted] = pattern_cache_.insert({&specialization, SpecializationPrototype()});
    if (!inserted) {
        return it->second;
    }
    it->second.patterns = GlobalMemory::alloc_array<const Object*>(specialization.patterns.size());
    it->second.skolems = GlobalMemory::alloc_array<const Object*>(specialization.patterns.size());
    for (size_t i = 0; i < specialization.parameters.size(); i++) {
        pattern_scope.set_template_argument(
            specialization.parameters[i].identifier, auto_instances[i]
        );
    }
    for (size_t i = 0; i < specialization.patterns.size(); i++) {
        Symbol result =
            ValueContextEvaluator{sema_, nullptr, false, true}(specialization.patterns[i]);
        assert(Sema::get_if<SymbolKind::Type>(result));
        it->second.patterns[i] = Sema::get<SymbolKind::Type>(result);
    }
    pattern_scope.clear();
    for (const auto& param : specialization.parameters) {
        pattern_scope.set_template_argument(
            param.identifier,
            param.is_nttp ? static_cast<const Object*>(&UnknownValue::instance)
                          : &UnknownType::instance
        );
    }
    for (size_t i = 0; i < specialization.patterns.size(); i++) {
        Symbol result =
            ValueContextEvaluator{sema_, nullptr, false, true}(specialization.patterns[i]);
        assert(Sema::get_if<SymbolKind::Type>(result));
        it->second.skolems[i] = Sema::get<SymbolKind::Type>(result);
    }
    pattern_scope.clear();
    return it->second;
}

inline auto CallHandler::get_func_obj(Scope* scope, FunctionOverloadDef overload) noexcept
    -> FunctionObject {
    auto get_param_type = [&](const ASTFunctionParameter& param) -> const Type* {
        TypeResolution type;
        TypeContextEvaluator{sema_, type}(param.type);
        return type;
    };
    Sema::Guard guard{sema_, *scope};
    GlobalMemory::Vector<const Type*> params;
    TypeResolution return_type;
    if (auto func_def = overload.get<const ASTFunctionDefinition*>()) {
        for (const ASTFunctionParameter& param : func_def->parameters) {
            if (param.is_variadic) {
                Symbol pack =
                    ValueContextEvaluator{sema_, nullptr, true}(func_def->parameters.back().type);
                for (const Object* param_type : Sema::get<SymbolKind::ParameterPack>(pack)) {
                    params.push_back(param_type->cast<Type>());
                }
                break;
            }
            params.push_back(get_param_type(param));
        }
        if (!holds_monostate(func_def->return_type)) {
            TypeContextEvaluator{sema_, return_type}(func_def->return_type);
        } else {
            return_type = &VoidType::instance;
        }
    } else if (auto ctor_def = overload.get<const ASTCtorDtorDefinition*>()) {
        for (const ASTFunctionParameter& param : ctor_def->parameters) {
            if (param.is_variadic) {
                Symbol pack =
                    ValueContextEvaluator{sema_, nullptr, true}(ctor_def->parameters.back().type);
                for (const Object* param_type : Sema::get<SymbolKind::ParameterPack>(pack)) {
                    params.push_back(param_type->cast<Type>());
                }
                break;
            }
            params.push_back(get_param_type(param));
        }
        return_type = sema_.get_self_type();
    } else if (auto* op_def = overload.get<const ASTOperatorDefinition*>()) {
        TypeResolution left_type;
        TypeContextEvaluator{sema_, left_type}(op_def->left.type);
        params.push_back(left_type.get());
        if (op_def->right) {
            TypeResolution right_type;
            TypeContextEvaluator{sema_, right_type}(op_def->right->type);
            params.push_back(right_type.get());
        }
        TypeContextEvaluator{sema_, return_type}(op_def->return_type);
    } else {
        UNREACHABLE();
    }
    /// TODO: handle constexpr functions
    if (std::ranges::contains(params, nullptr)) {
        return {};
    }
    return TypeRegistry::get<FunctionType>(
        params | GlobalMemory::collect<std::span>(), return_type
    );
}

inline auto AccessHandler::eval_access(const ASTMemberAccess* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->base);
    if (auto* type = Sema::get_if<SymbolKind::Type>(base_symbol)) {
        if (auto* instance_type = (*type)->dyn_cast<InstanceType>()) {
            Symbol result = eval_static_access(instance_type->scope_, node->member);
            sema_.codegen_env_.map_member_access(sema_.current_scope_, node, instance_type->scope_);
            return result;
        }
    } else if (auto* term = Sema::get_if<SymbolKind::Term>(base_symbol)) {
        Term decayed = Sema::decay(*term);
        if (decayed->kind_ == Kind::Struct) {
            return eval_struct_access(*term, node->member);
        } else if (decayed->kind_ == Kind::Instance) {
            return eval_instance_access(*term, node->member);
        }
    } else if (auto* namespace_scope = Sema::get_if<SymbolKind::Namespace>(base_symbol)) {
        Symbol result = eval_static_access(*namespace_scope, node->member);
        sema_.codegen_env_.map_member_access(sema_.current_scope_, node, *namespace_scope);
        return result;
    }
    return {};
}

inline auto AccessHandler::eval_pointer(const ASTPointerAccess* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->base);
    if (!Sema::expect(base_symbol, SymbolKind::Term)) {
        return {};
    }
    Term base_term = Sema::get<SymbolKind::Term>(base_symbol);
    Term decayed = Sema::decay(base_term);
    if (auto* pointer_type = decayed.effective_type()->dyn_cast<PointerType>()) {
        Term dereferenced;
        if (auto* value = decayed.get_comptime()) {
            dereferenced = Term::lvalue(value->cast<PointerValue>()->pointed_value_);
        } else {
            dereferenced = Term::lvalue(pointer_type->pointed_type_);
        }
        if (pointer_type->pointed_type_->dyn_cast<StructType>()) {
            return eval_struct_access(dereferenced, node->member);
        } else if (pointer_type->pointed_type_->dyn_cast<InstanceType>()) {
            return eval_instance_access(dereferenced, node->member);
        }
    } else if (decayed.effective_type()->dyn_cast<InstanceType>()) {
        return sema_.operation_handler_->eval_overloaded_op(node, OperatorCode::Pointer, base_term);
    }
    return {};
}

inline auto AccessHandler::eval_index(const ASTIndexAccess* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->base);
    Symbol index_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->index);
    bool has_error = !Sema::expect(base_symbol, SymbolKind::Term);
    has_error |= !Sema::expect(index_symbol, SymbolKind::Term);
    if (has_error) {
        return {};
    }
    Term base_term = Sema::get<SymbolKind::Term>(base_symbol);
    Term index_term = Sema::get<SymbolKind::Term>(index_symbol);
    const Type* base_type = Sema::decay(base_term.effective_type());
    const Type* index_type = Sema::decay(index_term.effective_type());
    std::array<Term, 2> arg_terms{base_term, index_term};
    if (auto* instance_type = base_type->dyn_cast<InstanceType>();
        instance_type && instance_type->scope_->is_extern_) {
        if (instance_type->identifier_ == "array" || instance_type->identifier_ == "span" ||
            instance_type->identifier_ == "string" || instance_type->identifier_ == "string_view") {
            // implicitly cast index to usize
            if (index_type == &IntegerType::untyped_instance) {
                arg_terms[1] = Term::prvalue(
                    arg_terms[1].get_comptime()->resolve_to(&IntegerType::u64_instance)
                );
            }
        }
    }
    return sema_.call_handler_->eval_call(
        node, std::tuple{OperatorCode::Index, base_type, index_type}, arg_terms
    );
}

inline auto TypeContextEvaluator::operator()(const ASTArrayType* node) noexcept -> void {
    TypeResolution element_type;
    TypeContextEvaluator{sema_, element_type}(node->element_type);
    if (element_type.get() == nullptr) return;

    if (holds_monostate(node->length)) {
        Symbol span_symbol = sema_.get_std_symbol("span");
        std::span array_args = GlobalMemory::pack_array<const Object*>(element_type.get());
        Symbol result = sema_.template_handler_->instantiate(
            *std::get<TemplateFamily*>(span_symbol), array_args
        );
        out_ = Sema::get<SymbolKind::Type>(result);
        return;
    }

    Symbol length_symbol = ValueContextEvaluator{sema_, nullptr, true}(node->length);
    if (Sema::expect(length_symbol, SymbolKind::Term)) {
        Term length_term = std::get<Term>(length_symbol);
        if (!length_term.is_comptime()) {
            Diagnostic::error_not_constant_expression(ASTNodePtrGetter{}(node->length)->location);
            return;
        } else if (!length_term.get_comptime()->dyn_cast<IntegerValue>()) {
            Diagnostic::error_symbol_category_mismatch(
                "integer", length_term.effective_type()->repr()
            );
            return;
        }
        Symbol array_symbol = sema_.get_std_symbol("array");
        std::span array_args = GlobalMemory::pack_array<const Object*>(
            element_type.get(), static_cast<const Object*>(length_term.get_comptime())
        );
        Symbol result = sema_.template_handler_->instantiate(
            *std::get<TemplateFamily*>(array_symbol), array_args
        );
        out_ = Sema::get<SymbolKind::Type>(result);
        return;
    }
    out_ = TypeRegistry::get_unknown();
}
