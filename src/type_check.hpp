#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"
#include "symbol_collect.hpp"

using Symbol = std::variant<
    std::monostate,
    const Type*,
    Term,
    std::pair<Scope*, const ScopeValue*>,
    std::pair<Term, std::pair<Scope*, const ScopeValue*>>,
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
        bool is_operator_overload;
    };

    struct AccessChain {
        const Scope* scope;
        std::size_t end_of_static_part;
        std::span<const Object*> instantiation_args;
    };

    using TableValue = std::variant<
        const Type*,   // type expression
        FunctionCall,  // function call
        AccessChain>;  // member access

    using Table = GlobalMemory::FlatMap<const ASTNode*, TableValue>;

public:
    static auto mangle_path(
        GlobalMemory::String& mangled, const Scope* scope, std::string_view identifier
    ) -> void {
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
        functions_.push_back({current_scope, node, func_obj});
    }

    auto map_type(const Scope* current_scope, const ASTNode* node, const Type* type) -> void {
        scope_map_[current_scope][node] = type;
    }

    auto map_access_chain(
        const Scope* current_scope,
        const ASTNode* node,
        const Scope* scope,
        std::size_t end_of_static_part,
        std::span<const Object*> instantiation_args
    ) -> void {
        scope_map_[current_scope][node] = AccessChain{
            .scope = scope,
            .end_of_static_part = end_of_static_part,
            .instantiation_args = instantiation_args
        };
    }

    auto map_function_call(
        const Scope* current_scope,
        const ASTNode* node,
        const FunctionType* func_type,
        const Type* self_type,
        bool is_constructor,
        bool is_operator_overload
    ) -> void {
        scope_map_[current_scope][node] = FunctionCall{
            .func_type = func_type,
            .self_type = self_type,
            .is_constructor = is_constructor,
            .is_operator_overload = is_operator_overload
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
            throw UnlocatedProblem::make<OperationNotDefinedError>("", left->repr(), right->repr());
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
        std::size_t postfix_length_;

    public:
        Guard(Sema& sema, Scope& scope) noexcept
            : sema_(sema), prev_scope_(std::exchange(sema.current_scope_, &scope)) {}
        Guard(Sema& sema, const ASTNode* child, std::string_view postfix = "") noexcept
            : sema_(sema),
              prev_scope_(
                  std::exchange(sema.current_scope_, sema.current_scope_->children_.at(child))
              ),
              postfix_length_(postfix.length()) {
            if (!postfix.empty()) {
                sema.qualified_path_ += postfix;
            }
        }
        Guard(const Guard&) = delete;
        Guard(Guard&&) = delete;
        auto operator=(const Guard&) = delete;
        auto operator=(Guard&&) = delete;
        ~Guard() noexcept {
            sema_.current_scope_ = prev_scope_;
            sema_.qualified_path_.resize(sema_.qualified_path_.size() - postfix_length_);
        }
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

    [[nodiscard]] static auto apply_mutable(Term term) -> Term {
        if (auto value = term.get_comptime()) {
            return Term::forward_like(
                term, new MutableValue(TypeRegistry::get<MutableType>(term.effective_type()), value)
            );
        } else {
            return Term::forward_like(term, TypeRegistry::get<MutableType>(term.effective_type()));
        }
    }

    [[nodiscard]] static auto apply_value_category(Term obj) -> const Type* {
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
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, TypeResolution> type_cache_;

public:
    Scope* current_scope_;
    GlobalMemory::String qualified_path_;
    CodeGenEnvironment& codegen_env_;
    std::unique_ptr<TemplateHandler> template_handler_;
    std::unique_ptr<OperationHandler> operation_handler_;
    std::unique_ptr<AccessHandler> access_handler_;
    std::unique_ptr<CallHandler> call_handler_;

public:
    Sema(Scope& root, CodeGenEnvironment& codegen_env) noexcept;

    auto deferred_analysis(Scope& scope, auto variant) noexcept -> void;

    [[nodiscard]] auto get_self_type() const noexcept -> const Type* {
        const Scope* scope = current_scope_;
        while (!scope->self_type_ && scope->parent_) {
            scope = scope->parent_;
        }
        return scope->self_type_;
    }

    [[nodiscard]] auto is_at_top_level() const noexcept -> bool {
        return current_scope_->parent_ == nullptr;
    }

    auto eval_type(std::string_view identifier, Scope& scope, const ScopeValue& value) noexcept
        -> TypeResolution;

    auto eval_symbol(std::string_view identifier, Scope& scope, const ScopeValue& value) noexcept
        -> Symbol;

    auto lookup(std::string_view identifier) -> std::pair<Scope*, const ScopeValue*> {
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
    auto lookup_type(std::string_view identifier) -> TypeResolution {
        auto [scope, value] = lookup(identifier);
        if (!scope) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        return eval_type(identifier, *scope, *value);
    }

    auto lookup_symbol(std::string_view identifier) -> Symbol {
        auto [scope, value] = lookup(identifier);
        if (!scope) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        return eval_symbol(identifier, *scope, *value);
    }
};

class TemplateHandler final : public GlobalMemory::MonotonicAllocated {
private:
    struct TemplateArgumentComparator {
        auto operator()(const Object* left, const Object* right) noexcept {
            if (auto* left_type = left->dyn_type()) {
                return left_type == right->cast<Type>();
            } else {
                /// TODO: value equality
                return false;
            }
        }
    };

    struct SpecializationPrototype {
        std::span<const Object*> patterns;
        std::span<const Object*> skolems;
    };

    using TemplateArgs = std::span<const Object*>;

    using AutoBindings = GlobalMemory::FlatMap<const Object*, const Object*>;

private:
    Sema& sema_;
    // GlobalMemory::UnorderedMap<std::pair<const ASTTemplateDefinition*, TemplateArgs>, const
    // Object*>
    //     instantiations_;
    GlobalMemory::UnorderedMap<const ASTTemplateSpecialization*, SpecializationPrototype>
        pattern_cache_;

public:
    TemplateHandler(Sema& sema) noexcept : sema_(sema) {}

    [[nodiscard]] auto is_template_about_type(const TemplateFamily& family) const noexcept -> bool;

    inline auto instantiate(TemplateFamily& family, TemplateArgs args) -> Symbol {
        if (auto meta_result = catch_meta_instantiation(family, args)) {
            return {};
        }
        Sema::Guard guard(sema_, *family.decl_scope);
        if (!validate(*family.primary, args)) {
            return {};
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
        return result;
    }

    auto template_inference(TemplateFamily& family, std::span<Term> args) -> std::span<Term>;

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

    [[nodiscard]] static auto compare_specialization(
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

    [[nodiscard]] static auto compare_pattern(
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
            scope.add_template_argument(template_param.identifier, value);
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

    [[nodiscard]] auto eval_primitive_op(
        OperatorCode opcode, Term left, Term right = {}
    ) const noexcept -> Term {
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
    ) const noexcept -> Term;
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
            transformed_args.insert(transformed_args.begin(), transform(method->first));
        }

        FunctionObject overload = resolve_overload(callee, transformed_args);
        if (!overload) {
            /// TODO: throw no matching overload error
            throw;
        }
        sema_.codegen_env_.map_function_call(
            sema_.current_scope_,
            node,
            get_func_type(overload),
            Sema::get_if<SymbolKind::Method>(callee)
                ? Sema::get<SymbolKind::Method>(callee).first.effective_type()
                : nullptr,
            Sema::get_if<SymbolKind::Type>(callee),
            Sema::get_if<SymbolKind::Term>(callee)
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
    [[nodiscard]] auto list_normal_overloads(Symbol func) -> GlobalMemory::Vector<FunctionObject> {
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
            if (auto* intersection_type = decayed->dyn_cast<IntersectionType>()) {
                return intersection_type->types_ |
                       GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>();
            } else if (auto* func_type = decayed->dyn_cast<FunctionType>()) {
                return {func_type};
            } else if (auto func_value = decayed->dyn_cast<FunctionValue>()) {
                return {func_value};
            }
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(func)) {
            return reification(function->first, function->second);
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(func)) {
            return reification(method->second.first, method->second.second);
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
                right_type->kind_ == Kind::Instance && left_type != right_type) {
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
        GlobalMemory::Vector<FunctionObject>& overloads, Symbol func, std::span<const Type*> args
    ) -> void {}

    [[nodiscard]] auto resolve_overload(Symbol func, std::span<const Type*> args)
        -> FunctionObject {
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

    [[nodiscard]] static auto overload_rank(FunctionObject func, std::span<const Type*> arg_types)
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

    [[nodiscard]] static auto overload_partial_order(
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

    auto eval_access(const ASTAccessChain& node) noexcept -> Symbol;

private:
    auto eval_next_static_access(
        Symbol& base, std::string_view member, const Scope*& base_scope
    ) noexcept -> bool {
        if (auto* namespace_scope = Sema::get_if<SymbolKind::Namespace>(base)) {
            auto next = (*namespace_scope)->find(member);
            if (!next) {
                throw UnlocatedProblem::make<UndeclaredIdentifierError>(member);
            } else if (auto* next_namespace = next->get<Scope*>()) {
                base = next_namespace;
                base_scope = next_namespace;
            } else if (auto* family = next->get<TemplateFamily*>()) {
                base = family;
                base_scope = next_namespace;
            } else {
                base = sema_.eval_symbol(member, **namespace_scope, *next);
            }
            return true;
        } else {
            return false;
        }
    }

    auto eval_next_access(Symbol& base, Term& self, std::string_view member) noexcept -> bool {
        if (auto* type = Sema::get_if<SymbolKind::Type>(base)) {
            // class static scope access
            if ((*type)->kind_ != Kind::Instance) {
                throw;
            }
            auto instance_type = (*type)->cast<InstanceType>();
            auto next = instance_type->scope_->find(member);
            if (!next) {
                throw;
            } else if (auto* namespace_scope = next->get<Scope*>()) {
                base = namespace_scope;
            } else {
                base = sema_.eval_symbol(member, *instance_type->scope_, *next);
            }
        } else if (auto* term = Sema::get_if<SymbolKind::Term>(base)) {
            // value access
            Term decayed = Sema::decay(*term);
            Term result;
            if (decayed->kind_ == Kind::Struct) {
                result = struct_access(decayed, member);
            } else if (decayed->kind_ == Kind::Instance) {
                result = instance_access(decayed, member);
            } else {
                throw UnlocatedProblem::make<OperationNotDefinedError>(
                    ".", decayed->repr(), member
                );
            }
            if (!result) {
                /// TODO: throw member not found error
                throw;
            }
            self = *term;
            base = sema_.is_mutable(*term) ? sema_.apply_mutable(result) : result;

            return true;
        } else {
            return false;
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
        auto find_method = [&](Scope* scope) -> Term {
            const ScopeValue* value = scope->find(member);
            if (value->get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
                return Term::prvalue(
                    new FunctionOverloadSetValue(scope, opaque_cast<const OpaqueScopeValue*>(value))
                );
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
};

class TypeContextEvaluator {
private:
    Sema& sema_;
    TypeResolution& out_;
    bool require_complete_;

public:
    TypeContextEvaluator(Sema& sema, TypeResolution& out, bool require_complete = true) noexcept
        : sema_(sema), out_(out), require_complete_(require_complete) {}

    void operator()(const ASTExprVariant& expr_variant) {
        std::visit(*this, expr_variant);
        if (!out_.is_sized() && require_complete_) {
            /// TODO:
        }
    }

    void operator()(const ASTExpression* node) { UNREACHABLE(); }

    void operator()(const ASTExplicitTypeExpr* node) { UNREACHABLE(); }

    void operator()(const ASTSelfExpr* node) {
        if (!node->is_type) {
            Diagnostic::report(SymbolCategoryMismatchError(node->location, false));
            out_ = TypeRegistry::get_unknown();
        } else {
            out_ = sema_.get_self_type();
            if (!out_.get()) {
                /// TODO: throw not in class error
            }
        }
    }

    void operator()(const ASTParenExpr* node) { (*this)(node->inner); }

    void operator()(const ASTConstant* node) {
        /// TODO: literal types
        out_ = {};
    }

    void operator()(const ASTIdentifier* node) {
        TypeResolution result = sema_.lookup_type(node->str);
        if (!result.is_sized()) {
            if (require_complete_) {
                Diagnostic::report(CircularTypeDependencyError(node->location));
                out_ = TypeRegistry::get_unknown();
                return;
            }
        }
        out_ = result;
    }

    void operator()(const ASTAccessChain* node) {
        Symbol result = sema_.access_handler_->eval_access(*node);
        if (auto* type = Sema::get_if<SymbolKind::Type>(result)) {
            out_ = *type;
        } else {
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTUnaryOp* node) {
        TypeResolution expr_result;
        TypeContextEvaluator{sema_, expr_result, false}(node->expr);
        try {
            out_ =
                TypeResolution(sema_.operation_handler_->eval_type_op(node->opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTBinaryOp* node) {
        TypeResolution left_result;
        TypeContextEvaluator{sema_, left_result}(node->left);
        TypeResolution right_result;
        TypeContextEvaluator{sema_, right_result}(node->right);
        try {
            out_ = sema_.operation_handler_->eval_type_op(node->opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
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
                TypeContextEvaluator{sema_, param_type}(param_expr);
                return param_type;
            }) |
            GlobalMemory::collect<std::span<const Type*>>();
        if (any_error) {
            out_ = TypeRegistry::get_unknown();
            return;
        }
        TypeResolution return_type;
        TypeContextEvaluator{sema_, return_type}(node->return_type);
        TypeRegistry::get_at<FunctionType>(out_, param_types, return_type);
    }

    void operator()(const ASTStructType* node) {
        out_ = std::type_identity<StructType>();
        GlobalMemory::FlatMap<std::string_view, const Type*> field_map =
            node->fields |
            std::views::transform(
                [&](const ASTFieldDeclaration& decl) -> std::pair<std::string_view, const Type*> {
                    TypeResolution field_type;
                    TypeContextEvaluator{sema_, field_type}(decl.type);
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

    void operator()(const ASTArrayType* node);

    void operator()(const ASTMutableType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTReferenceType* node) {
        out_ = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out_, expr_type, node->is_moved);
    }

    void operator()(const ASTPointerType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{sema_, expr_type, false}(node->inner);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTClassDefinition* node) {
        out_ = new InstanceType(node->identifier);
        Sema::Guard guard(sema_, node);
        sema_.current_scope_->self_type_ = out_;
        const Type* base = resolve_base(node);
        std::span<const Type*> interfaces = resolve_interfaces(node);
        GlobalMemory::FlatMap<std::string_view, const Type*> attrs = resolve_attrs(node);
        TypeRegistry::get_at<InstanceType>(
            out_, sema_.current_scope_, node->identifier, base, interfaces, std::move(attrs)
        );
    }

    void operator()(const auto* node) { UNREACHABLE(); }

private:
    auto resolve_base(const ASTClassDefinition* node) const noexcept -> const Type* {
        if (node->extends.empty()) {
            return nullptr;
        }
        TypeResolution result;
        try {
            result = sema_.lookup_type(node->extends);
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
                result = sema_.lookup_type(interface_name);
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

    auto resolve_attrs(const ASTClassDefinition* node) const noexcept
        -> GlobalMemory::FlatMap<std::string_view, const Type*> {
        return node->fields |
               std::views::transform(
                   [&](
                       const ASTDeclaration* field_decl
                   ) -> std::pair<std::string_view, const Type*> {
                       TypeResolution field_type;
                       TypeContextEvaluator{sema_, field_type}(field_decl->declared_type);
                       return {field_decl->identifier, field_type};
                   }
               ) |
               GlobalMemory::collect<GlobalMemory::FlatMap>();
    }
};

/// TODO: expected parameter is not working
class ValueContextEvaluator {
private:
    Sema& sema_;
    const Type* expected_;
    bool require_comptime_;
    bool template_pattern_mode_;

private:
    ValueContextEvaluator(ValueContextEvaluator& other, const Type* expected) noexcept
        : sema_(other.sema_),
          expected_(expected),
          require_comptime_(other.require_comptime_),
          template_pattern_mode_(other.template_pattern_mode_) {}

public:
    ValueContextEvaluator(
        Sema& sema, const Type* expected, bool require_comptime, bool template_pattern_mode = false
    ) noexcept
        : sema_(sema),
          expected_(expected),
          require_comptime_(require_comptime),
          template_pattern_mode_(template_pattern_mode) {}

    auto operator()(const ASTExprVariant& variant) -> Symbol {
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

    auto operator()(std::monostate) -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTExpression* node) -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTExplicitTypeExpr* node) -> Symbol { UNREACHABLE(); }

    auto operator()(const ASTParenExpr* node) -> Symbol { return (*this)(node->inner); }

    auto operator()(const ASTConstant* node) -> Symbol {
        if (expected_) {
            try {
                Value* typed_value = node->value->resolve_to(expected_);
                return Term::prvalue(typed_value);
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location);
                return Term::unknown();
            }
        } else {
            return Term::prvalue(node->value->resolve_to(nullptr));
        }
    }

    auto operator()(const ASTStringConstant* node) -> Symbol {
        auto [scope, value] = sema_.lookup("std");
        Scope* std_scope = value->get<Scope*>();
        const ScopeValue* string_view_scope_value = std_scope->find("string_view");
        assert(
            string_view_scope_value && string_view_scope_value->get<const ASTClassDefinition*>()
        );
        TypeResolution strview_type_res =
            sema_.eval_type("string_view", *std_scope, *string_view_scope_value);
        return Term::prvalue(strview_type_res.get());
    }

    auto operator()(const ASTSelfExpr* node) -> Symbol {
        if (node->is_type) {
            return sema_.get_self_type();
        } else {
            Symbol result = sema_.lookup_symbol("self");
            assert(Sema::get_if<SymbolKind::Term>(result));
            return result;
        }
    }

    auto operator()(const ASTIdentifier* node) -> Symbol {
        try {
            Symbol symbol = sema_.lookup_symbol(node->str);
            if (require_comptime_) {
                if (auto* term = Sema::get_if<SymbolKind::Term>(symbol); !term->is_comptime()) {
                    Diagnostic::report(NotConstantExpressionError(node->location));
                    return {};
                }
            }
            return symbol;
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            return {};
        }
    }

    auto operator()(const ASTAccessChain* node) -> Symbol {
        return sema_.access_handler_->eval_access(*node);
    }

    auto operator()(const ASTUnaryOp* node) -> Symbol {
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

    auto operator()(const ASTBinaryOp* node) -> Symbol {
        Symbol left_symbol = ValueContextEvaluator{*this, nullptr}(node->left);
        Symbol right_symbol = ValueContextEvaluator{*this, nullptr}(node->right);
        bool any_error = false;
        any_error |= !Sema::expect(left_symbol, SymbolKind::Term);
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

    auto operator()(const ASTFieldInitialization* node) -> Symbol {
        Symbol value_symbol = ValueContextEvaluator{*this, nullptr}(node->value);
        if (!Sema::expect(value_symbol, SymbolKind::Term)) {
            return {};
        }
        return value_symbol;
    }

    auto operator()(const ASTStructInitialization* node) -> Symbol {
        /// TODO: constexpr
        const Type* type = nullptr;
        if (nonnull(node->struct_type)) {
            TypeResolution struct_type;
            TypeContextEvaluator{sema_, struct_type}(node->struct_type);
            type = struct_type;
        } else if (auto* self = sema_.get_self_type()) {
            type = self;
        } else {
            // throw UnlocatedProblem::make<SymbolCategoryMismatchError>(node->location,
            // true);
            throw;
        }
        if (type->dyn_cast<InstanceType>() && sema_.get_self_type() != type) {
            /// TODO: throw not in constructor error
            throw;
        }
        GlobalMemory::FlatMap<std::string_view, Term> inits;
        for (const ASTFieldInitialization& init : node->field_inits) {
            Symbol value_symbol = ValueContextEvaluator{*this, nullptr}(init.value);
            if (!Sema::expect(value_symbol, SymbolKind::Term)) {
                return Term::prvalue(type);
            }
            inits.emplace(init.identifier, Sema::get<SymbolKind::Term>(value_symbol));
        }
        return StructType::construct(type, inits);
    }

    auto operator()(const ASTFunctionCall* node) -> Symbol {
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
        return sema_.call_handler_->eval_call(node, func_symbol, args_terms);
    }

    auto operator()(const auto* node) -> Symbol { UNREACHABLE(); }
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
        if (nonnull(node->declared_type)) {
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
        if (node->else_block) {
            (*this)(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (node->initializer_decl) {
            ValueContextEvaluator{sema_, nullptr, false}(node->initializer_decl);
        } else if (!std::holds_alternative<std::monostate>(node->initializer_expr)) {
            ValueContextEvaluator{sema_, nullptr, false}(node->initializer_expr);
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
        sema_.codegen_env_.add_function_output(
            sema_.current_scope_, node, CallHandler::get_func_type(func_obj)
        );
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTConstructorDestructorDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (node->is_constructor) {
            FunctionObject func_obj = sema_.call_handler_->get_func_obj(sema_.current_scope_, node);
            sema_.codegen_env_.add_function_output(
                sema_.current_scope_, node, CallHandler::get_func_type(func_obj)
            );
            sema_.current_scope_->in_constructor_ = true;
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

inline auto Sema::deferred_analysis(Scope& scope, auto variant) noexcept -> void {
    SymbolCollector{scope}(variant);
    Guard guard(*this, scope);
    TypeCheckVisitor{*this}(variant);
}

inline auto TemplateHandler::validate(
    const ASTTemplateDefinition& primary, TemplateArgs args
) noexcept -> bool {
    if (primary.parameters.size() != args.size()) {
        return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
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
        inst_scope.add_template_argument(family.primary->parameters[i].identifier, args[i]);
    }
    return {&inst_scope, family.primary->target_node};
}

inline auto TemplateHandler::get_prototype(
    Scope& pattern_scope, const ASTTemplateSpecialization& specialization
) noexcept -> const SpecializationPrototype& {
    GlobalMemory::Vector<Object*> auto_instances = TypeRegistry::get_auto_instances(
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
        pattern_scope.add_template_argument(
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
        pattern_scope.add_template_argument(
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

inline auto OperationHandler::eval_overloaded_op(
    const ASTNode* node, OperatorCode opcode, Term left, Term right
) const noexcept -> Term {
    const Type* left_type = Sema::decay(left.effective_type());
    const Type* right_type = right ? Sema::decay(right.effective_type()) : nullptr;
    if (left_type->kind_ != Kind::Instance && right_type && right_type->kind_ != Kind::Instance) {
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

inline auto AccessHandler::eval_access(const ASTAccessChain& node) noexcept -> Symbol {
    const Scope* base_scope;
    Symbol base_symbol;
    Term self;
    // first eval base
    if (auto* identifier_node = std::get_if<const ASTIdentifier*>(&node.base)) {
        auto [scope, value] = sema_.lookup((*identifier_node)->str);
        if (!scope) {
            Diagnostic::report(
                UndeclaredIdentifierError((*identifier_node)->location, (*identifier_node)->str)
            );
            base_symbol = Term::unknown();
        }
        base_scope = scope;
        if (auto* namespace_scope = value->get<Scope*>()) {
            base_symbol = namespace_scope;
        } else if (auto* family = value->get<TemplateFamily*>()) {
            base_symbol = family;
        } else {
            base_symbol = sema_.lookup_symbol((*identifier_node)->str);
        }
    } else {
        base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node.base);
    }
    // second process static accesses
    std::span<std::string_view> remaining = node.members;
    while (!remaining.empty()) {
        if (!eval_next_static_access(base_symbol, remaining.front(), base_scope)) {
            break;
        }
        remaining = remaining.subspan(1);
    }
    // third process non-static accesses
    for (std::string_view member : remaining) {
        if (!eval_next_access(base_symbol, self, member)) {
            return {};
        }
    }
    if (std::holds_alternative<Scope*>(base_symbol)) {
        throw;
    }
    std::span<const Object*> arg_terms;
    if (!node.instantiation_args.empty()) {
        if (!std::holds_alternative<TemplateFamily*>(base_symbol)) {
            throw;
        }
        arg_terms =
            node.instantiation_args |
            std::views::transform([&](ASTExprVariant arg) -> const Object* {
                Symbol result = ValueContextEvaluator{sema_, nullptr, true}(arg);
                if (Sema::expect(result, SymbolKind::Type, SymbolKind::Term)) {
                    return Sema::get_if<SymbolKind::Type>(result)
                               ? static_cast<const Object*>(Sema::get<SymbolKind::Type>(result))
                               : Sema::get<SymbolKind::Term>(result).get_comptime();
                }
                return nullptr;
            }) |
            GlobalMemory::collect<std::span>();
        return sema_.template_handler_->instantiate(
            *std::get<TemplateFamily*>(base_symbol), arg_terms
        );
    }
    sema_.codegen_env_.map_access_chain(
        sema_.current_scope_, &node, base_scope, node.members.size() - remaining.size(), arg_terms
    );
    return base_symbol;
}

inline auto CallHandler::get_func_obj(Scope* scope, FunctionOverloadDef overload) noexcept
    -> FunctionObject {
    Sema::Guard guard{sema_, *scope};
    bool any_error = false;
    std::span<const Type*> params;
    TypeResolution return_type;
    if (auto func_def = overload.get<const ASTFunctionDefinition*>()) {
        params = func_def->parameters |
                 std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                     TypeResolution param_type;
                     TypeContextEvaluator{sema_, param_type}(param.type);
                     return param_type;
                 }) |
                 GlobalMemory::collect<std::span<const Type*>>();
        if (nonnull(func_def->return_type)) {
            TypeContextEvaluator{sema_, return_type}(func_def->return_type);
        } else {
            return_type = &VoidType::instance;
        }
    } else if (auto ctor_def = overload.get<const ASTConstructorDestructorDefinition*>()) {
        params = ctor_def->parameters |
                 std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                     TypeResolution param_type;
                     TypeContextEvaluator{sema_, param_type}(param.type);
                     return param_type;
                 }) |
                 GlobalMemory::collect<std::span<const Type*>>();
        return_type = sema_.get_self_type();
    } else if (auto* operator_def = overload.get<const ASTOperatorOverloadDefinition*>()) {
        TypeResolution left_type;
        TypeResolution right_type;
        TypeContextEvaluator{sema_, left_type}(operator_def->left.type);
        TypeContextEvaluator{sema_, return_type}(operator_def->return_type);
        if (operator_def->right) {
            TypeContextEvaluator{sema_, right_type}(operator_def->right->type);
            params = GlobalMemory::pack_array(left_type.get(), right_type.get());
        } else {
            params = GlobalMemory::pack_array(left_type.get());
        }
    } else {
        UNREACHABLE();
    }
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline auto Sema::eval_type(
    std::string_view identifier, Scope& scope, const ScopeValue& value
) noexcept -> TypeResolution {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        }
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
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

inline auto Sema::eval_symbol(
    std::string_view identifier, Scope& scope, const ScopeValue& value
) noexcept -> Symbol {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        } else {
            return Term::prvalue(const_cast<Value*>(object->cast<Value>()));
        }
    } else if (value.get<const ASTExprVariant*>() || value.get<const ASTClassDefinition*>()) {
        TypeResolution out = eval_type(identifier, scope, value);
        return out.get();
    } else if (auto var_init = value.get<const VariableInitialization*>()) {
        Term init{};
        if (!holds_monostate(var_init->value)) {
            Symbol init_symbol = ValueContextEvaluator{*this, nullptr, false}(var_init->value);
            if (!Sema::get_if<SymbolKind::Term>(init_symbol)) return {};
            init = Term::lvalue(std::get<Term>(init_symbol));
        }
        if (holds_monostate(var_init->type)) return init;
        TypeResolution type;
        TypeContextEvaluator{*this, type}(var_init->type);
        if (init && !type->assignable_from(init.effective_type())) return {};
        return Term::lvalue(type.get());
    } else if (value.get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
        return std::pair{&scope, &value};
    } else if (auto* template_family = value.get<TemplateFamily*>()) {
        return template_family;
    } else if (auto* scope_ptr = value.get<Scope*>()) {
        return scope_ptr;
    } else {
        /// TODO: throw
        assert(false);
    }
}

inline auto TypeContextEvaluator::operator()(const ASTArrayType* node) -> void {
    out_ = std::type_identity<ArrayType>();
    TypeResolution element_type;
    TypeContextEvaluator{sema_, element_type}(node->element_type);
    if (!element_type.is_sized()) {
        TypeRegistry::add_ref_dependency(out_, element_type);
    }
    if (std::holds_alternative<std::monostate>(node->length)) {
        TypeRegistry::get_at<ArrayType>(out_, element_type, nullptr);
        return;
    }

    Symbol length_symbol = ValueContextEvaluator{sema_, nullptr, true}(node->length);
    if (Sema::expect(length_symbol, SymbolKind::Term)) {
        Term length_term = std::get<Term>(length_symbol);
        if (!length_term.is_comptime() || !length_term.get_comptime()->dyn_cast<IntegerValue>()) {
            throw;
        }
        TypeRegistry::get_at<ArrayType>(
            out_, element_type, length_term.get_comptime()->cast<IntegerValue>()
        );
    }
    out_ = TypeRegistry::get_unknown();
}
