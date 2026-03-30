#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"
#include "symbol_collect.hpp"

enum class SymbolKind : std::uint8_t {
    Type = 1,
    Term = 2,
    Function = 3,
    Method = 4,
    Template = 5,
    ParameterPack = 6,
    Namespace = 7,
    Operator = 8,
    PartialInstantiation = 9,
};

struct PointerChain : GlobalMemory::MonotonicAllocated {
    ASTExprVariant base;
    std::span<std::pair<const InstanceType*, const FunctionType*>> pointers;
};

struct BoundMethod {
    Term object;
    Scope* scope;
    const ScopeValue* value;
    PointerChain* self;
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
    std::tuple<OperatorCode, const Type*, const Type*>,
    std::tuple<Scope*, const ASTTemplateDefinition*, std::span<const Object*>>>;

class CodeGenEnvironment {
public:
    struct FunctionDef {
        const Scope* scope;
        ASTNodeVariant node;
        const FunctionType* func_type;
    };

    struct FunctionCall : public GlobalMemory::MonotonicAllocated {
        const FunctionType* func_type;
        const Scope* scope;
        strview identifier;
        PointerChain* self;
    };

    using TableValue = std::variant<
        const Type*,                // type expression
        const Scope*,               // member access
        PointerChain*,              // pointer access
        FunctionCall*,              // function call
        std::span<const Object*>>;  // template instantiation

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
    GlobalMemory::FlatMap<std::pair<const Scope*, strview>, const Value*> constants_;
    GlobalMemory::Vector<FunctionDef> functions_;
    GlobalMemory::Vector<std::pair<Scope*, std::span<const Object*>>> instantiations_;
    GlobalMemory::FlatMap<const Scope*, Table> scope_map_;
    GlobalMemory::Vector<strview> cpp_blocks_;

public:
    auto add_global(const Scope* scope, strview identifier, const Value* value) -> void {
        constants_.insert({{scope, identifier}, value});
    }

    auto add_function_output(
        const Scope* current_scope, ASTNodeVariant node, const FunctionType* func_obj
    ) -> void {
        if (current_scope->is_extern_) return;
        functions_.push_back({current_scope, node, func_obj});
    }

    auto add_instantiation(const Scope* inst_scope, std::span<const Object*> args) -> void {
        instantiations_.push_back({const_cast<Scope*>(inst_scope), args});
    }

    auto add_cpp_block(strview code) -> void { cpp_blocks_.push_back(code); }

    auto map_type(const Scope* current_scope, const ASTNode* node, const Type* type) -> void {
        scope_map_[current_scope][node] = type;
    }

    auto map_member_access(const Scope* current_scope, const ASTNode* node, const Scope* scope)
        -> void {
        scope_map_[current_scope][node] = scope;
    }

    auto map_pointer_access(const Scope* current_scope, const ASTNode* node, PointerChain* chain)
        -> void {
        scope_map_[current_scope][node] = chain;
    }

    auto map_func_call(
        const Scope* current_scope,
        const ASTNode* node,
        const FunctionType* func_type,
        const Scope* func_scope,
        strview identifier,
        PointerChain* self
    ) -> void {
        scope_map_[current_scope][node] = new FunctionCall{
            .func_type = func_type,
            .scope = func_scope,
            .identifier = identifier,
            .self = self,
        };
    }

    auto map_instantiation(
        const Scope* current_scope, const ASTNode* node, std::span<const Object*> args
    ) -> void {
        scope_map_[current_scope][node] = args;
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

        auto fmt = [](SymbolKind kind) -> strview {
            switch (kind) {
            case SymbolKind::Type:
                return "'type'"sv;
            case SymbolKind::Term:
                return "'value'"sv;
            case SymbolKind::Function:
                return "'function'"sv;
            case SymbolKind::Method:
                return "'method'"sv;
            case SymbolKind::Template:
                return "'template'"sv;
            case SymbolKind::ParameterPack:
                return "'parameter pack'"sv;
            case SymbolKind::Namespace:
                return "'namespace'"sv;
            case SymbolKind::Operator:
                return "'operator'"sv;
            default:
                UNREACHABLE();
            }
        };
        GlobalMemory::String expected_str;
        {
            const char* sep = "";
            ((expected_str.append(sep).append(fmt(expected_kinds)), sep = " or "), ...);
        }
        Diagnostic::error_symbol_category_mismatch(
            expected_str, fmt(static_cast<SymbolKind>(symbol.index()))
        );
        return false;
    }

    template <SymbolKind kind>
    static auto get(Symbol& symbol) -> auto& {
        assert(symbol.index() == static_cast<std::uint8_t>(kind));
        return std::get<static_cast<std::uint8_t>(kind)>(symbol);
    }

    template <SymbolKind kind>
    static auto get(const Symbol& symbol) -> const auto& {
        assert(symbol.index() == static_cast<std::uint8_t>(kind));
        return std::get<static_cast<std::uint8_t>(kind)>(symbol);
    }

    template <SymbolKind kind>
    static auto get_if(Symbol& symbol) -> auto* {
        return std::get_if<static_cast<std::uint8_t>(kind)>(&symbol);
    }

    template <SymbolKind kind>
    static auto get_default(Symbol& symbol) -> auto {
        if (auto* ptr = get_if<kind>(symbol)) {
            return *ptr;
        } else {
            return std::remove_cvref_t<decltype(get<kind>(symbol))>{};
        }
    }

    static auto nonnull(Term term) -> Symbol { return term ? Symbol(term) : Symbol{}; }

    static auto nonnull(const Type* type) -> Symbol { return type ? Symbol(type) : Symbol{}; }

    template <typename T>
    static auto args_repr(std::span<const T*> args) -> GlobalMemory::String {
        GlobalMemory::String str = "(";
        strview sep = ""sv;
        for (const T* arg : args) {
            str += sep;
            str += arg->repr();
            sep = ", "sv;
        }
        str += ")";
        return str;
    }

private:
    GlobalMemory::Map<std::pair<const Scope*, const ScopeValue*>, TypeResolution> type_cache_;

public:
    Scope& std_scope_;
    Scope* current_scope_;
    CodeGenEnvironment& codegen_env_;
    std::unique_ptr<TemplateHandler> template_handler_;
    std::unique_ptr<OperationHandler> operation_handler_;
    std::unique_ptr<AccessHandler> access_handler_;
    std::unique_ptr<CallHandler> call_handler_;

public:
    Sema(Scope& std_scope, Scope& root, CodeGenEnvironment& codegen_env) noexcept;

    auto deferred_analysis(Scope& scope, auto variant) noexcept -> void;

    auto get_self_type() noexcept -> const Type* {
        Scope* scope = current_scope_;
        while (scope->self_id_.empty() && scope->parent_) {
            scope = scope->parent_;
        }
        if (scope->self_id_.empty()) {
            throw;  // not in class error
        }
        const ScopeValue* self_value = scope->parent_->find(scope->self_id_);
        assert(self_value);
        return eval_type(*scope->parent_, *self_value).get();
    }

    auto is_at_top_level() const noexcept -> bool { return current_scope_->parent_ == nullptr; }

    auto eval_type(Scope& scope, const ScopeValue& value) noexcept -> TypeResolution;

    auto eval_symbol(Scope& scope, const ScopeValue& value) noexcept -> Symbol;

    auto lookup(strview identifier) -> std::pair<Scope*, const ScopeValue*> {
        Scope* scope = current_scope_;
        while (scope) {
            auto it = scope->identifiers_.find(identifier);
            if (it != scope->identifiers_.end()) {
                // accessing injected class name from template during template instantiation,
                // return the template instead of the class
                if (!(scope->is_instantiating_template_ &&
                      scope->children_.begin()->second->self_id_ == identifier)) {
                    return {scope, &it->second};
                }
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
        return eval_type(*scope, *value);
    }

    auto lookup_symbol(strview identifier) -> Symbol {
        auto [scope, value] = lookup(identifier);
        if (!scope) {
            Diagnostic::error_undeclared_identifier(identifier);
            return {};
        }
        return eval_symbol(*scope, *value);
    }

    auto get_std_symbol(strview identifier) -> Symbol {
        auto [scope, value] = lookup("std");
        auto std_scope = value->get<Scope*>();
        return eval_symbol(*std_scope, *std_scope->find(identifier));
    }

private:
    auto eval_var_init(Scope& scope, const VariableInitialization& init) noexcept -> Symbol;
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
                    if (!left_arg->cast<Value>()->equal_to(right_arg->cast<Value>())) {
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
        if (Object::any_pattern(args)) {
            std::span persisted_args = args | GlobalMemory::collect<std::span>();
            if (std::get_if<const ASTClassDefinition*>(&family.primary->target_node)) {
                return new InstanceType(family.primary->identifier, family.primary, persisted_args);
            }
            // Patterns can only be used for class templates
            throw;
        }
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
        inst_scope->scope_id_ = family.primary->identifier;
        Sema::Guard inner_guard(sema_, *inst_scope);
        std::span persisted_args = args | GlobalMemory::collect<std::span>();
        sema_.codegen_env_.add_instantiation(inst_scope, persisted_args);
        sema_.deferred_analysis(*inst_scope, target);
        const ScopeValue* value = inst_scope->find(family.primary->identifier);
        Symbol result = sema_.eval_symbol(*inst_scope, *value);
        if (holds_monostate(result)) return {};
        if (const Type* type = std::get<static_cast<size_t>(SymbolKind::Type)>(result)) {
            if (auto instance_type = type->dyn_cast<InstanceType>()) {
                instance_type->primary_template_ = family.primary;
                instance_type->template_args_ = persisted_args;
            }
        }
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
        Scope& inst_scope = Scope::make(*sema_.current_scope_, nullptr, primary->identifier);
        for (size_t i = 0; i < args.size(); i++) {
            inst_scope.set_template_argument(primary->parameters[i].identifier, args[i]);
        }
        if (primary->parameters.back().is_variadic) {
            TemplateArgs pack_args = args.subspan(primary->parameters.size() - 1);
            inst_scope.set_template_pack(primary->parameters.back().identifier, pack_args);
        }
        /// TODO: parameter pack
        Sema::Guard inner_guard(sema_, inst_scope);
        std::span persisted_args = args | GlobalMemory::collect<std::span>();
        sema_.codegen_env_.add_instantiation(&inst_scope, persisted_args);
        sema_.deferred_analysis(inst_scope, primary->target_node);
        const ScopeValue* value = inst_scope.find(primary->identifier);
        return sema_.eval_symbol(inst_scope, *value);
    }

    auto inference(
        Scope* scope,
        const ASTTemplateDefinition* primary,
        TemplateArgs explicit_instantiation_args,
        std::span<const Type*> args
    ) noexcept -> const FunctionType*;

private:
    static auto as_symbol(const Object* obj) -> Symbol {
        if (auto type = obj->dyn_type()) {
            return Symbol(type);
        } else {
            return Symbol(Term::of(obj->cast<Value>()));
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
        AutoBindings auto_bindings;
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

private:
    Sema& sema_;

public:
    CallHandler(Sema& sema) noexcept : sema_(sema) {}

    auto get_func_type(Scope* scope, FunctionOverloadDef overload) noexcept -> const FunctionType*;

    auto eval_call(const ASTNode* node, Symbol callee, std::span<const Type*> args_type)
        -> std::pair<Term, const FunctionType*> {
        if (holds_monostate(callee)) return {};
        assert(!std::ranges::contains(args_type, nullptr));
        GlobalMemory::Vector<const Type*> combined_args{args_type.begin(), args_type.end()};
        if (auto* method = Sema::get_if<SymbolKind::Method>(callee)) {
            combined_args.insert(combined_args.begin(), method->object.effective_type());
        }

        Diagnostic::ErrorTrap error_trap{node->location};
        const FunctionType* overload = resolve_overload(callee, combined_args);
        if (!overload) {
            Diagnostic::error_no_matching_overload(
                node->location, Sema::args_repr(std::span{combined_args})
            );
            error_trap.conclude();
            resolve_overload(callee, combined_args);  // for debugging
            return {};
        }

        auto [should_mangle, scope, identifier] = get_mangle_info(callee);
        if (should_mangle) {
            PointerChain* self = Sema::get_if<SymbolKind::Method>(callee)
                                     ? Sema::get<SymbolKind::Method>(callee).self
                                     : nullptr;
            sema_.codegen_env_.map_func_call(
                sema_.current_scope_, node, overload, scope, identifier, self
            );
        }
        return {Term::of(overload->return_type_), overload};
    }

    static auto assignable(const Type* from, const Type* to) noexcept -> bool {
        AutoBindings _;
        return param_rank(to, from, _) != ConversionRank::NoMatch;
    }

    static auto assignable(const Type* from, const Type* to, AutoBindings& auto_bindings) noexcept
        -> bool {
        return param_rank(to, from, auto_bindings) != ConversionRank::NoMatch;
    }

private:
    auto list_normal_overloads(Symbol func) -> GlobalMemory::Vector<const FunctionType*> {
        auto reification = [&](
                               Scope* scope, const ScopeValue* scope_value
                           ) -> GlobalMemory::Vector<const FunctionType*> {
            auto* overloads = scope_value->get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            return *overloads | std::views::filter([](FunctionOverloadDef overload) {
                return !overload.get<const ASTTemplateDefinition*>();
            }) | std::views::transform([this, scope](FunctionOverloadDef overload) {
                return get_func_type(scope, overload);
            }) | GlobalMemory::collect<GlobalMemory::Vector>();
        };
        if (!Sema::expect(
                func,
                SymbolKind::Type,
                SymbolKind::Term,
                SymbolKind::Function,
                SymbolKind::Method,
                SymbolKind::Operator
            )) {
            return {};
        }
        if (auto* callable_type = Sema::get_if<SymbolKind::Type>(func)) {
            if ((*callable_type)->kind_ != Kind::Instance) {
                Diagnostic::error_type_not_callable((*callable_type)->repr());
                return {};
            }
            Scope* scope = (*callable_type)->cast<InstanceType>()->scope_;
            return reification(scope, scope->find(constructor_symbol));
        } else if (auto* callable_term = Sema::get_if<SymbolKind::Term>(func)) {
            const Type* decayed = callable_term->decay();
            if (auto* func_type = decayed->dyn_cast<FunctionType>()) {
                return {func_type};
            }
            Diagnostic::error_value_not_callable(decayed->repr());
            return {};
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(func)) {
            return reification(function->first, function->second);
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(func)) {
            return reification(method->scope, method->value);
        } else if (auto* operator_fn = Sema::get_if<SymbolKind::Operator>(func)) {
            GlobalMemory::Vector<const FunctionType*> result;
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
        UNREACHABLE();
    }

    auto list_template_overloads(
        GlobalMemory::Vector<const FunctionType*>& out, Symbol func, std::span<const Type*> args
    ) -> void {
        auto reification = [&](Scope* scope, const ScopeValue* scope_value) -> void {
            auto* overloads = scope_value->get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            for (FunctionOverloadDef overload : *overloads) {
                if (!overload.get<const ASTTemplateDefinition*>()) continue;
                if (auto func_obj = sema_.template_handler_->inference(
                        scope, overload.get<const ASTTemplateDefinition*>(), {}, args
                    )) {
                    out.push_back(func_obj);
                }
            }
        };
        if (!Sema::expect(
                func,
                SymbolKind::Type,
                SymbolKind::Term,
                SymbolKind::Function,
                SymbolKind::Method,
                SymbolKind::Operator
            )) {
            return;
        }
        if (auto* callable_type = Sema::get_if<SymbolKind::Type>(func)) {
            if ((*callable_type)->kind_ != Kind::Instance) {
                Diagnostic::error_type_not_callable((*callable_type)->repr());
                return;
            }
            Scope* scope = (*callable_type)->cast<InstanceType>()->scope_;
            return reification(scope, scope->find(constructor_symbol));
        } else if (auto* callable_term = Sema::get_if<SymbolKind::Term>(func)) {
            const Type* decayed = callable_term->decay();
            if (auto* func_type = decayed->dyn_cast<FunctionType>()) {
                out.push_back(func_type);
                return;
            }
            return Diagnostic::error_value_not_callable(decayed->repr());
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(func)) {
            return reification(function->first, function->second);
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(func)) {
            return reification(method->scope, method->value);
        } else if (auto* operator_fn = Sema::get_if<SymbolKind::Operator>(func)) {
            bool has_overloads = false;
            const Type* left_type = std::get<1>(*operator_fn);
            if (left_type->kind_ == Kind::Instance) {
                Scope* scope = left_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    has_overloads = true;
                    reification(scope, overloads);
                }
            }
            const Type* right_type = std::get<2>(*operator_fn);
            if (right_type && right_type->kind_ == Kind::Instance && left_type != right_type) {
                Scope* scope = right_type->cast<InstanceType>()->scope_;
                auto overloads = scope->find(GetOperatorString(std::get<0>(*operator_fn)));
                if (overloads) {
                    has_overloads = true;
                    reification(scope, overloads);
                }
            }
            if (!has_overloads) {
                Diagnostic::error_operation_not_defined(
                    GetOperatorString(std::get<0>(*operator_fn)),
                    left_type->repr(),
                    right_type ? right_type->repr() : ""
                );
            }
            return;
        }
        UNREACHABLE();
    }

    auto resolve_overload(Symbol func, std::span<const Type*> args) -> const FunctionType* {
        Diagnostic::ErrorTrap error_trap;
        const FunctionType* best_candidate = nullptr;
        ConversionRank best_rank = ConversionRank::NoMatch;
        auto loop = [&](std::span<const FunctionType*> candidates) -> std::size_t {
            for (std::size_t i = 0; i < candidates.size(); ++i) {
                const FunctionType* candidate = candidates[i];
                ConversionRank rank = ConversionRank::NoMatch;
                if (candidate) {
                    rank = overload_rank(candidate, args);
                }
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
        GlobalMemory::Vector<const FunctionType*> overloads = list_normal_overloads(func);
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
        GlobalMemory::Vector<const FunctionType*> ambiguous_candidates;
        bool incomparable = false;
        if (best_candidate) {
            error_trap.clear();
            for (const FunctionType* candidate : overloads) {
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

    static auto param_rank(const Type* param, const Type* arg, AutoBindings& auto_bindings) noexcept
        -> ConversionRank {
        using enum ConversionRank;
        const Type* decayed_param = Type::decay(param);
        const Type* decayed_arg = Type::decay(arg);
        auto tiebreaker_rank = [&]() -> ConversionRank {
            // the order of variants indicates the rank of conversion, e.g. T -> move &T is
            // better than T -> &T, variants not listed below means no match, e.g. T -> &mut T
            auto* param_ref = param->dyn_cast<ReferenceType>();
            auto* arg_ref = arg->dyn_cast<ReferenceType>();
            if (arg_ref == nullptr) {
                // (rvalue) T -> T / move &T / &T
                if (param_ref) return param_ref->is_moved_ ? Referenced : QualifiedReferenced;
                return Exact;
            } else if (!arg_ref->is_moved_) {
                // (lvalue) &T -> &T / &mut T / T, &mut T -> &mut T / &T / T
                if (param_ref == nullptr) return Copy;
                if (param_ref->is_moved_) return NoMatch;
                return (arg_ref->is_mutable_) ^ (param_ref->is_mutable_) ? Qualified : Exact;
            } else {
                // (xvalue) move &T -> move &T / &mut T / &T / T
                if (!param_ref) return Copy;
                if (param_ref->is_mutable_) return Referenced;
                return param_ref->is_moved_ ? Exact : QualifiedReferenced;
            }
        };
        if (decayed_param == decayed_arg) {
            // qualification conversion
            if (Type::is_mutable(param) && !Type::is_mutable(arg)) return NoMatch;
            return tiebreaker_rank();
        } else if (decayed_param->is_pattern()) {
            // auto type deduction
            if (auto* inst_param = decayed_param->dyn_cast<InstanceType>()) {
                auto* inst_arg = decayed_arg->dyn_cast<InstanceType>();
                if (inst_arg && inst_param->pattern_match(inst_arg, auto_bindings)) {
                    return tiebreaker_rank();
                }
                return NoMatch;
            } else {
                const AutoObject* auto_obj = decayed_param->cast<AutoObject>();
                if (auto it = auto_bindings.find(auto_obj); it != auto_bindings.end()) {
                    if (it->second != decayed_arg) {
                        return NoMatch;
                    }
                } else {
                    auto_bindings[auto_obj] = decayed_arg;
                }
                return tiebreaker_rank();
            }
        } else {
            // pointer conversion
            if (Type::is_mutable(param) && !Type::is_mutable(arg)) return NoMatch;
            if (decayed_param->kind_ == Kind::Pointer && decayed_arg->kind_ == Kind::Nullptr) {
                return Upcast;
            }
            if (!decayed_arg->dyn_cast<PointerType>() || !decayed_param->dyn_cast<PointerType>()) {
                return NoMatch;
            }
            auto param_pointee = decayed_param->cast<PointerType>()->target_type_;
            auto arg_pointee = decayed_arg->cast<PointerType>()->target_type_;
            if (Type::is_mutable(param_pointee) && !Type::is_mutable(arg_pointee)) return NoMatch;
            const Type* decayed_param_pointee = Type::decay(param_pointee);
            const Type* decayed_arg_pointee = Type::decay(arg_pointee);
            if (decayed_param_pointee == decayed_arg_pointee) {
                return tiebreaker_rank();
            } else if (decayed_param_pointee->kind_ == Kind::Void) {
                return Upcast;
            } else {
                const InstanceType* param_class =
                    Type::decay(param_pointee)->dyn_cast<InstanceType>();
                const InstanceType* arg_class = Type::decay(arg_pointee)->dyn_cast<InstanceType>();
                if (param_class && arg_class) {
                    if (static_cast<const Type*>(param_class) == &VoidType::instance) return Upcast;
                    if (!TypeRegistry::is_base(arg_class, param_class)) return NoMatch;
                }
            }
            return NoMatch;
        }
    }

    static auto overload_rank(const FunctionType* func, std::span<const Type*> arg_types)
        -> ConversionRank {
        if (func->parameters_.size() != arg_types.size()) {
            return ConversionRank::NoMatch;
        }
        AutoBindings auto_bindings;
        ConversionRank worst_rank = ConversionRank::Exact;
        for (std::size_t i = 0; i < arg_types.size(); ++i) {
            ConversionRank rank = param_rank(func->parameters_[i], arg_types[i], auto_bindings);
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
        const FunctionType* left, const FunctionType* right, std::span<const Type*> arg_types
    ) -> std::partial_ordering {
        bool left_ever_better = false;
        bool right_ever_better = false;
        AutoBindings auto_bindings;
        for (std::size_t i = 0; i < arg_types.size(); ++i) {
            ConversionRank left_rank =
                param_rank(left->parameters_[i], arg_types[i], auto_bindings);
            ConversionRank right_rank =
                param_rank(right->parameters_[i], arg_types[i], auto_bindings);
            if (left_rank < right_rank) {
                left_ever_better = true;
            } else if (right_rank < left_rank) {
                right_ever_better = true;
            }
        }
        if (left_ever_better && !right_ever_better) {
            return std::partial_ordering::greater;
        } else if (!left_ever_better && right_ever_better) {
            return std::partial_ordering::less;
        } else if (left_ever_better && right_ever_better) {
            return std::partial_ordering::unordered;
        } else {
            return std::partial_ordering::equivalent;
        }
    }

    auto get_mangle_info(Symbol callee) const noexcept -> std::tuple<bool, Scope*, strview> {
        auto get_func_identifier =
            [&](GlobalMemory::Vector<FunctionOverloadDef>& overloads) -> strview {
            FunctionOverloadDef first_overload = overloads[0];
            if (auto* func_def = first_overload.get<const ASTFunctionDefinition*>()) {
                return func_def->identifier;
            } else if (first_overload.get<const ASTCtorDtorDefinition*>()) {
                return constructor_symbol;
            } else if (auto* op_def = first_overload.get<const ASTOperatorDefinition*>()) {
                return GetOperatorString(op_def->opcode);
            } else if (auto* template_def = first_overload.get<const ASTTemplateDefinition*>()) {
                return template_def->identifier;
            } else {
                UNREACHABLE();
            }
        };
        if (auto* type = Sema::get_if<SymbolKind::Type>(callee)) {
            const InstanceType* instance_type = (*type)->cast<InstanceType>();
            return {!instance_type->scope_->is_extern_, instance_type->scope_, constructor_symbol};
        } else if (auto* term = Sema::get_if<SymbolKind::Term>(callee)) {
            const Type* decayed = term->decay();
            if (decayed->dyn_cast<FunctionType>()) {
                return {false, nullptr, {}};
            }
            const InstanceType* instance_type = decayed->dyn_cast<InstanceType>();
            return {
                instance_type->scope_->is_extern_,
                instance_type->scope_,
                GetOperatorString(OperatorCode::Call)
            };
        } else if (auto* function = Sema::get_if<SymbolKind::Function>(callee)) {
            if (function->first->is_extern_) {
                return {false, nullptr, {}};
            }
            return {
                true,
                function->first,
                get_func_identifier(
                    *function->second->get<GlobalMemory::Vector<FunctionOverloadDef>*>()
                )
            };
        } else if (auto* method = Sema::get_if<SymbolKind::Method>(callee)) {
            if (method->scope->is_extern_) {
                return {false, nullptr, {}};
            }
            return {
                true,
                method->scope,
                get_func_identifier(
                    *method->value->get<GlobalMemory::Vector<FunctionOverloadDef>*>()
                )
            };
        } else if (auto* operator_fn = Sema::get_if<SymbolKind::Operator>(callee)) {
            bool both_extern = true;
            const Type* left_type = std::get<1>(*operator_fn);
            if (auto* left_instance = left_type->dyn_cast<InstanceType>()) {
                Scope* scope = left_instance->scope_;
                if (!scope->is_extern_) {
                    both_extern = false;
                }
            }
            const Type* right_type = std::get<2>(*operator_fn);
            if (right_type && right_type->kind_ == Kind::Instance) {
                Scope* scope = right_type->cast<InstanceType>()->scope_;
                if (!scope->is_extern_) {
                    both_extern = false;
                }
            }
            if (both_extern) {
                return {false, nullptr, {}};
            } else {
                return {true, nullptr, GetOperatorString(std::get<0>(*operator_fn))};
            }
        }
        return {false, nullptr, {}};
    }
};

class AccessHandler final : public GlobalMemory::MonotonicAllocated {
private:
    Sema& sema_;

public:
    AccessHandler(Sema& sema) noexcept : sema_(sema) {}

    auto eval_access(const ASTMemberAccess* node) noexcept -> Symbol;

    auto eval_pointer(const ASTPointerAccess* node) noexcept -> Symbol;

    auto eval_deref(const ASTDereference* node) noexcept -> Symbol;

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
            return sema_.eval_symbol(*static_scope, *next);
        }
    }

    auto eval_struct_access(Term object, strview member) -> Term {
        const StructType* struct_type = object.decay()->cast<StructType>();
        auto attr_it = std::ranges::find(
            struct_type->fields_, member, &std::pair<strview, const Type*>::first
        );
        if (attr_it != struct_type->fields_.end()) {
            return Term::of(Type::forward_like(object.effective_type(), attr_it->second));
        }
        Diagnostic::error_member_not_found(member);
        return {};
    }

    auto eval_instance_access(Term object, strview member) -> Symbol {
        const InstanceType* instance_type = object.decay()->cast<InstanceType>();
        auto attr_it = std::ranges::find(
            instance_type->attrs_, member, &std::pair<strview, const Type*>::first
        );
        if (attr_it != instance_type->attrs_.end()) {
            return Term::of(Type::forward_like(object.effective_type(), attr_it->second));
        }
        const ScopeValue* value = instance_type->scope_->find(member);
        if (value && value->get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
            return BoundMethod{object, instance_type->scope_, value};
        }
        Diagnostic::error_member_not_found(member);
        return {};
    }

    auto is_std_indexable_container(const InstanceType* instance_type) const noexcept -> bool {
        if (!instance_type->scope_->is_extern_) {
            return false;
        }
        if (instance_type->primary_template_ == nullptr) {
            auto get = [&](strview name) -> const Type* {
                return Sema::get<SymbolKind::Type>(sema_.get_std_symbol(name));
            };
            return instance_type == get("string") || instance_type == get("string_view");
        } else {
            auto get = [&](strview name) -> const ASTTemplateDefinition* {
                return Sema::get<SymbolKind::Template>(sema_.get_std_symbol(name))->primary;
            };
            return instance_type->primary_template_ == get("vector") ||
                   instance_type->primary_template_ == get("array") ||
                   instance_type->primary_template_ == get("span");
        }
    }
};

class OperationHandler final : public GlobalMemory::MonotonicAllocated {
private:
    template <OperatorGroup G>
    static auto apply_op(OperatorCode opcode, const auto& left, const auto& right) noexcept {
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
            case OperatorCode::LeftShift:
                return left << right;
            case OperatorCode::RightShift:
                return left >> right;
            default:
                UNREACHABLE();
            }
        } else {
            static_assert(false);
        }
    }

    template <OperatorGroup G>
    static auto apply_op(OperatorCode opcode, const auto& value) noexcept {
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

    static auto integer_op(OperatorCode opcode, const Value* left, const Value* right) noexcept
        -> const Value* {
        const IntegerValue* left_int = left->cast<IntegerValue>();
        const IntegerValue* right_int = right->cast<IntegerValue>();
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

    static auto integer_op(OperatorCode opcode, const Value* left) noexcept -> const Value* {
        const IntegerValue* left_int = left->cast<IntegerValue>();
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

    static auto float_op(OperatorCode opcode, const Value* left, const Value* right) noexcept
        -> const Value* {
        const FloatValue* left_float = left->cast<FloatValue>();
        const FloatValue* right_float = right->cast<FloatValue>();
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

    static auto float_op(OperatorCode opcode, const Value* left) noexcept -> const Value* {
        const FloatValue* left_float = left->cast<FloatValue>();
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

    static auto boolean_op(OperatorCode opcode, const Value* left, const Value* right) noexcept
        -> const Value* {
        /// TODO: support equality comparison between booleans
        const BooleanValue* left_bool = left->cast<BooleanValue>();
        const BooleanValue* right_bool = right->cast<BooleanValue>();
        switch (GetOperatorGroup(opcode)) {
        case OperatorGroup::Logical:
            return new BooleanValue(
                apply_op<OperatorGroup::Logical>(opcode, left_bool->value_, right_bool->value_)
            );
        default:
            UNREACHABLE();
        }
    }

    static auto boolean_op(OperatorCode opcode, const Value* left) noexcept -> const Value* {
        const BooleanValue* left_bool = left->cast<BooleanValue>();
        switch (GetOperatorGroup(opcode)) {
        case OperatorGroup::UnaryLogical:
            return new BooleanValue(
                apply_op<OperatorGroup::UnaryLogical>(opcode, left_bool->value_)
            );
        default:
            UNREACHABLE();
        }
    }

public:
    static auto is_primitive(Term operand) noexcept -> bool {
        const Type* decayed = operand.decay();
        return decayed->kind_ == Kind::Nullptr || decayed->kind_ == Kind::Integer ||
               decayed->kind_ == Kind::Float || decayed->kind_ == Kind::Boolean ||
               decayed->kind_ == Kind::Pointer;
    }

private:
    Sema& sema_;

public:
    OperationHandler(Sema& sema) noexcept : sema_(sema) {}

    auto eval_type_op(OperatorCode opcode, const Type* left, const Type* right = nullptr) const
        -> const Type* {
        /// TODO: implement type-level operations
        UNREACHABLE();
    }

    auto eval_primitive_op(OperatorCode opcode, Term left, Term right = {}) const noexcept -> Term {
        bool comptime = left.is_comptime() && (right ? right.is_comptime() : true);
        const Type* decayed_left = left.decay();
        const Type* decayed_right = right.decay();
        const Type* result_type = decayed_left;
        if (result_type == &IntegerType::untyped_instance ||
            result_type == &FloatType::untyped_instance) {
            result_type = decayed_right;
        }
        Kind left_kind = decayed_left->kind_;
        Kind right_kind = decayed_right ? decayed_right->kind_ : Kind::None;
        if (comptime) {
            const Value* left_value = left.get_comptime();
            const Value* right_value = right ? right.get_comptime() : nullptr;
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::of(integer_op(opcode, left_value, right_value));
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term::of(float_op(opcode, left_value, right_value));
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                assert(!right);
                if (!Type::is_mutable(left.effective_type()) &&
                    (opcode == OperatorCode::Increment || opcode == OperatorCode::PostIncrement ||
                     opcode == OperatorCode::Decrement || opcode == OperatorCode::PostDecrement))
                    break;
                if (left_kind == Kind::Integer) {
                    return Term::of(integer_op(opcode, left_value));
                } else if (left_kind == Kind::Float) {
                    return Term::of(float_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Comparison:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::of(integer_op(opcode, left_value, right_value));
                } else if (left_kind == Kind::Float && right_kind == Kind::Float) {
                    return Term::of(float_op(opcode, left_value, right_value));
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term::of(boolean_op(opcode, left_value, right_value));
                }
                break;
            case OperatorGroup::UnaryLogical:
                assert(!right);
                if (left_kind == Kind::Boolean) {
                    return Term::of(boolean_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::of(integer_op(opcode, left_value, right_value));
                }
                break;
            case OperatorGroup::UnaryBitwise:
                if (left_kind == Kind::Integer) {
                    return Term::of(integer_op(opcode, left_value));
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                Diagnostic::error_operation_not_defined(
                    GetOperatorString(opcode),
                    decayed_left->repr(),
                    decayed_right ? decayed_right->repr() : ""
                );
                break;
            }
        } else {
            switch (GetOperatorGroup(opcode)) {
            case OperatorGroup::Arithmetic:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float)) {
                    return Term::of(result_type);
                }
                break;
            case OperatorGroup::UnaryArithmetic:
                assert(!right);
                if (!Type::is_mutable(left.effective_type()) &&
                    (opcode == OperatorCode::Increment || opcode == OperatorCode::PostIncrement ||
                     opcode == OperatorCode::Decrement || opcode == OperatorCode::PostDecrement))
                    break;
                if (left_kind == Kind::Integer || left_kind == Kind::Float) {
                    return Term::of(result_type);
                }
                break;
            case OperatorGroup::Comparison:
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    ((left_kind == Kind::Nullptr || left_kind == Kind::Pointer) &&
                     (right_kind == Kind::Nullptr || right_kind == Kind::Pointer))) {
                    return Term::of(&BooleanType::instance);
                }
                break;
            case OperatorGroup::Logical:
                if (left_kind == Kind::Boolean && right_kind == Kind::Boolean) {
                    return Term::of(&BooleanType::instance);
                }
                break;
            case OperatorGroup::UnaryLogical:
                assert(!right);
                if (left_kind == Kind::Boolean) {
                    return Term::of(&BooleanType::instance);
                }
                break;
            case OperatorGroup::Bitwise:
                if (left_kind == Kind::Integer && right_kind == Kind::Integer) {
                    return Term::of(result_type);
                }
                break;
            case OperatorGroup::UnaryBitwise:
                assert(!right);
                if (left_kind == Kind::Integer) {
                    return Term::of(result_type);
                }
                break;
            case OperatorGroup::Assignment:
                /// TODO:
                if (!Type::is_mutable(left.effective_type())) break;
                if ((left_kind == Kind::Integer && right_kind == Kind::Integer) ||
                    (left_kind == Kind::Float && right_kind == Kind::Float) ||
                    (left_kind == Kind::Boolean && right_kind == Kind::Boolean)) {
                    return Term::lvalue(result_type, true);
                } else {
                    if (decayed_left == decayed_right) {
                        return Term::lvalue(result_type, true);
                    }
                }
                break;
            }
        }
        /// TODO: throw error
        Diagnostic::error_operation_not_defined(
            GetOperatorString(opcode),
            decayed_left->repr(),
            decayed_right ? decayed_right->repr() : ""
        );
        return {};
    }

    auto eval_overloaded_op(
        const ASTNode* node, OperatorCode opcode, Term left, Term right = {}
    ) const noexcept -> std::pair<Term, const FunctionType*> {
        const Type* left_type = left.effective_type();
        const Type* left_base_type = Type::decay(left_type);
        const Type* right_type = right.effective_type();
        const Type* right_base_type = Type::decay(right_type);
        if (opcode == OperatorCode::Assign && left_base_type == right_base_type) {
            if (Type::is_mutable(left_type)) {
                return {Term::lvalue(left_type, true), nullptr};
            }
        }
        if (left_base_type->kind_ != Kind::Instance && right_base_type &&
            right_base_type->kind_ != Kind::Instance) {
            Diagnostic::error_operation_not_defined(
                GetOperatorString(opcode), left_type->repr(), right_type->repr()
            );
            return {};
        }
        std::array<const Type*, 2> args = {
            left.resolve_to_default().effective_type(), right.resolve_to_default().effective_type()
        };
        if (opcode == OperatorCode::PostIncrement || opcode == OperatorCode::PostDecrement) {
            args[1] = &IntegerType::i32_instance;
        }
        return sema_.call_handler_->eval_call(
            node,
            std::tuple{opcode, left_base_type, right_base_type},
            args[1] ? args : std::span(args).subspan(0, 1)
        );
    }

    auto eval_cast(const ASTAs* node, Term operand, const Type* target_type) const noexcept
        -> Term {
        bool convertible = false;
        const Type* source_type = operand.effective_type();
        const Type* decayed_source_type = Type::decay(source_type);
        const Type* decayed_target_type = Type::decay(target_type);
        if (source_type == target_type) {
            sema_.codegen_env_.map_type(sema_.current_scope_, node, target_type);
            return operand;
        }
        if (decayed_source_type == decayed_target_type) {
            if (auto* source_ref = source_type->dyn_cast<ReferenceType>()) {
                if (auto* target_ref = target_type->dyn_cast<ReferenceType>()) {
                    convertible = source_ref->is_moved_ == target_ref->is_moved_ &&
                                  (!Type::is_mutable(target_type) || Type::is_mutable(source_type));
                } else if (Meta::is_fundamental(target_type)) {
                    convertible = true;
                }
            }
        }
        if (decayed_source_type->kind_ == Kind::Integer ||
            decayed_source_type->kind_ == Kind::Float) {
            if (decayed_target_type->kind_ == Kind::Integer ||
                decayed_target_type->kind_ == Kind::Float) {
                convertible = true;
            }
        } else if (decayed_source_type->kind_ == Kind::Nullptr) {
            if (target_type->kind_ == Kind::Pointer) {
                convertible = true;
            }
        } else if (decayed_source_type->kind_ == Kind::Pointer) {
            if (target_type->kind_ == Kind::Pointer) {
                if (target_type->cast<PointerType>()->target_type_ == &VoidType::instance ||
                    decayed_source_type->cast<PointerType>()->target_type_ == &VoidType::instance) {
                    convertible = true;
                } else {
                    if (TypeRegistry::is_reachable(decayed_source_type, target_type)) {
                        convertible = true;
                    }
                }
            }
        }
        if (convertible) {
            sema_.codegen_env_.map_type(sema_.current_scope_, node, target_type);
            return Term::of(target_type);
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
    TypeContextEvaluator(Sema& sema, TypeResolution& out, bool require_sized = true) noexcept
        : sema_(sema), out_(out), require_sized_(require_sized) {}

    void operator()(const ASTExprVariant& expr_variant) noexcept {
        Diagnostic::ErrorTrap trap{ASTNodePtrGetter{}(expr_variant)->location};
        std::visit(*this, expr_variant);
        if (out_.get() && !out_.is_sized() && require_sized_) {
            Diagnostic::error_circular_type_dependency(ASTNodePtrGetter{}(expr_variant)->location);
        }
    }

    void operator()(const ASTTypeAlias* node) noexcept { (*this)(node->type); }

    void operator()(const ASTSelfExpr* node) noexcept {
        if (!node->is_type) {
            Diagnostic::error_symbol_category_mismatch(node->location, "type", "value");
            return;
        }
        out_ = sema_.get_self_type();
    }

    void operator()(const ASTParenExpr* node) noexcept { (*this)(node->inner); }

    void operator()(const ASTConstant* node) noexcept {
        /// TODO: literal types
        throw;
        out_ = {};
    }

    void operator()(const ASTIdentifier* node) noexcept { out_ = sema_.lookup_type(node->str); }

    void operator()(const ASTMemberAccess* node) noexcept {
        Symbol result = sema_.access_handler_->eval_access(node);
        if (auto* type = Sema::get_if<SymbolKind::Type>(result)) {
            out_ = *type;
        }
    }

    void operator()(const ASTUnaryOp* node) noexcept {
        /// TODO: type operation
        // TypeResolution expr_result;
        // TypeContextEvaluator{sema_, expr_result, false}(node->expr);
        // out_ = TypeResolution(sema_.operation_handler_->eval_type_op(node->opcode, expr_result));
    }

    void operator()(const ASTBinaryOp* node) noexcept {
        /// TODO: type operation
        // TypeResolution left_result;
        // TypeContextEvaluator{sema_, left_result}(node->left);
        // TypeResolution right_result;
        // TypeContextEvaluator{sema_, right_result}(node->right);
        // out_ = sema_.operation_handler_->eval_type_op(node->opcode, left_result, right_result);
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
        GlobalMemory::Vector<std::pair<strview, const Type*>> fields;
        for (const ASTFieldDeclaration& decl : node->fields) {
            if (std::ranges::contains(
                    fields, decl.identifier, &std::pair<strview, const Type*>::first
                )) {
                Diagnostic::error_redeclaration(decl.location, decl.identifier);
                has_error = true;
                continue;
            }
            TypeResolution field_type;
            TypeContextEvaluator{sema_, field_type}(decl.type);
            if (!field_type.get()) {
                has_error = true;
            }
            fields.push_back({decl.identifier, field_type.get()});
        }
        if (has_error) {
            out_ = nullptr;
            return;
        }
        TypeRegistry::get_at<StructType>(out_, fields | GlobalMemory::collect<std::span>());
    }

    void operator()(const ASTArrayType* node) noexcept;

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
        TypeRegistry::get_at<ReferenceType>(out_, expr_type, node->is_mutable, node->is_moved);
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
        TypeRegistry::get_at<PointerType>(out_, expr_type, node->is_mutable);
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        out_ = new InstanceType(node->identifier);
        Sema::Guard guard(sema_, node);
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
        GlobalMemory::Vector<std::pair<strview, const Type*>> attrs;
        for (ASTNodeVariant decl_variant : node->fields) {
            const ASTDeclaration* decl = std::get<const ASTDeclaration*>(decl_variant);
            if (decl->declared_static) continue;
            if (std::ranges::contains(
                    attrs, decl->identifier, &std::pair<strview, const Type*>::first
                )) {
                Diagnostic::error_redeclaration(decl->location, decl->identifier);
                has_error = true;
                continue;
            }
            TypeResolution field_type;
            if (holds_monostate(decl->declared_type)) {
                Diagnostic::error_missing_type_annotation(decl->location);
                has_error = true;
                continue;
            }
            TypeContextEvaluator{sema_, field_type}(decl->declared_type);
            has_error |= !field_type.get();
            attrs.push_back({decl->identifier, field_type.get()});
        }
        if (has_error) {
            out_ = nullptr;
            return;
        }
        TypeRegistry::get_at<InstanceType>(
            out_,
            sema_.current_scope_,
            node->identifier,
            base,
            interfaces,
            attrs | GlobalMemory::collect<std::span>()
        );
    }

    void operator()(const ASTEnumDefinition* node) noexcept { out_ = &IntegerType::i32_instance; }

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

private:
    explicit ValueContextEvaluator(
        ValueContextEvaluator& other, const Type* expected = nullptr
    ) noexcept
        : sema_(other.sema_), expected_(expected), require_comptime_(other.require_comptime_) {}

public:
    explicit ValueContextEvaluator(
        Sema& sema, const Type* expected = nullptr, bool require_comptime = false
    ) noexcept
        : sema_(sema), expected_(expected), require_comptime_(require_comptime) {}

    auto operator()(const ASTExprVariant& variant) noexcept -> Symbol {
        Diagnostic::ErrorTrap trap{ASTNodePtrGetter{}(variant)->location};
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
            if (node->value->kind_ != expected_->kind_) {
                if (node->value->kind_ == Kind::Nullptr && expected_->kind_ == Kind::Pointer) {
                    return Term::of(expected_);
                }
                Diagnostic::error_type_mismatch(expected_->repr(), node->value->repr());
                return {};
            } else if (auto* int_type = expected_->dyn_cast<IntegerType>()) {
                IntegerValue* int_value = node->value->cast<IntegerValue>()->resolve_to(int_type);
                return Sema::nonnull(Term::of(int_value));
            } else if (auto* float_type = expected_->dyn_cast<FloatType>()) {
                FloatValue* float_value = node->value->cast<FloatValue>()->resolve_to(float_type);
                return Sema::nonnull(Term::of(float_value));
            } else {
                if (expected_ != node->value->get_type()) {
                    Diagnostic::error_type_mismatch(expected_->repr(), node->value->repr());
                    return {};
                }
                return Term::of(node->value);
            }
        } else {
            return Term::of(node->value);
        }
    }

    auto operator()(const ASTStringConstant* node) noexcept -> Symbol {
        Symbol string_view_symbol = sema_.get_std_symbol("string_view");
        TypeResolution strview_type_res = Sema::get<SymbolKind::Type>(string_view_symbol);
        return Term::of(strview_type_res.get());
    }

    auto operator()(const ASTSelfExpr* node) noexcept -> Symbol {
        if (node->is_type) {
            const Type* self_type = sema_.get_self_type();
            return self_type ? Symbol{self_type} : Symbol{};
        } else {
            Symbol result = sema_.lookup_symbol("self");
            if (!holds_monostate(result)) {
                assert(Sema::get_if<SymbolKind::Term>(result));
            }
            return result;
        }
    }

    auto operator()(const ASTIdentifier* node) noexcept -> Symbol {
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

    auto operator()(const ASTPointerAccess* node) noexcept -> Symbol {
        return sema_.access_handler_->eval_pointer(node);
    }

    auto operator()(const ASTAddressOfExpr* node) noexcept -> Symbol {
        Symbol operand_symbol = ValueContextEvaluator{*this, nullptr}(node->operand);
        if (!Sema::expect(operand_symbol, SymbolKind::Term)) {
            return {};
        }
        Term operand_term = Sema::get<SymbolKind::Term>(operand_symbol);
        return Term::of(
            TypeRegistry::get<PointerType>(
                operand_term.effective_type(), Type::is_mutable(operand_term.effective_type())
            )
        );
    }

    auto operator()(const ASTDereference* node) noexcept -> Symbol {
        return sema_.access_handler_->eval_deref(node);
    }

    auto operator()(const ASTUnaryOp* node) noexcept -> Symbol {
        Symbol expr_symbol = ValueContextEvaluator{*this, nullptr}(node->expr);
        if (!Sema::expect(expr_symbol, SymbolKind::Term)) {
            return {};
        }
        Term expr_term = Sema::get<SymbolKind::Term>(expr_symbol);
        if (OperationHandler::is_primitive(expr_term)) {
            return Sema::nonnull(sema_.operation_handler_->eval_primitive_op(
                node->opcode, Sema::get<SymbolKind::Term>(expr_symbol)
            ));
        } else {
            return Sema::nonnull(
                sema_.operation_handler_->eval_overloaded_op(node, node->opcode, expr_term).first
            );
        }
    }

    auto operator()(const ASTBinaryOp* node) noexcept -> Symbol {
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
            return Sema::nonnull(
                sema_.operation_handler_->eval_primitive_op(node->opcode, left_term, right_term)
            );
        } else {
            auto result = sema_.operation_handler_->eval_overloaded_op(
                node,
                node->opcode,
                Sema::get<SymbolKind::Term>(left_symbol),
                Sema::get<SymbolKind::Term>(right_symbol)
            );
            return Sema::nonnull(result.first);
        }
    }

    auto operator()(const ASTFieldInitialization* node) noexcept -> Symbol {
        Symbol value_symbol = ValueContextEvaluator{*this, nullptr}(node->value);
        if (!Sema::expect(value_symbol, SymbolKind::Term)) {
            return {};
        }
        return value_symbol;
    }

    auto operator()(const ASTStructInitialization* node) noexcept -> Symbol {
        /// TODO: constexpr
        const Type* type = nullptr;
        if (!holds_monostate(node->struct_type)) {
            TypeResolution struct_type;
            TypeContextEvaluator{sema_, struct_type}(node->struct_type);
            if (!struct_type.get()) {
                return {};
            }
            type = struct_type;
            sema_.codegen_env_.map_type(sema_.current_scope_, node, struct_type);
        } else if (auto* self = sema_.get_self_type()) {
            type = self;
            sema_.codegen_env_.map_type(sema_.current_scope_, node, self);
        } else {
            Diagnostic::error_cannot_deduce_struct_type(node->location);
            return {};
        }
        if (type->dyn_cast<InstanceType>() && sema_.get_self_type() != type) {
            Diagnostic::error_construct_instance_out_of_class(node->location, type->repr());
        }
        bool any_error = false;
        GlobalMemory::FlatMap<strview, const Type*> inits;
        for (const ASTFieldInitialization& init : node->field_inits) {
            Symbol symbol = ValueContextEvaluator{*this, nullptr}(init.value);
            if (!Sema::expect(symbol, SymbolKind::Term)) {
                any_error = true;
                continue;
            }
            inits.emplace(
                init.identifier,
                Sema::get<SymbolKind::Term>(symbol).resolve_to_default().effective_type()
            );
        }
        if (any_error || !struct_validate(type, inits)) {
            return {};
        }
        return Term::of(type);
    }

    auto operator()(const ASTArrayInitialization* node) noexcept -> Symbol {
        /// TODO: array element type inference and constexpr
        Symbol array_type_symbol = sema_.get_std_symbol("array"sv);
        const void* array_template = Sema::get<SymbolKind::Template>(array_type_symbol)->primary;
        if (expected_) {
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
                Diagnostic::error_array_initializer_size_mismatch(
                    node->location, static_cast<size_t>(size_value->value_), node->elements.size()
                );
                return {};
            }
            GlobalMemory::Vector<Term> elements;
            elements.reserve(node->elements.size());
            for (const ASTExprVariant& element : node->elements) {
                Symbol element_symbol = ValueContextEvaluator{*this, element_type}(element);
                if (Sema::expect(element_symbol, SymbolKind::Term)) {
                    Term element_term = Sema::get<SymbolKind::Term>(element_symbol);
                    elements.push_back(element_term);
                } else {
                    elements.push_back(Term{});
                }
            }
            return Term::of(expected_);
        } else {
            const Type* element_type = nullptr;
            Symbol first_element_symbol =
                ValueContextEvaluator{*this, nullptr}(node->elements.front());
            if (Sema::expect(first_element_symbol, SymbolKind::Term)) {
                element_type = Sema::get<SymbolKind::Term>(first_element_symbol).decay();
            } else {
                return {};
            }
            for (size_t i = 1; i < node->elements.size(); ++i) {
                Symbol element_symbol =
                    ValueContextEvaluator{*this, element_type}(node->elements[i]);
                if (Sema::expect(element_symbol, SymbolKind::Term)) {
                    Term element_term = Sema::get<SymbolKind::Term>(element_symbol);
                    if (element_term.decay() != element_type) {
                        Diagnostic::error_type_mismatch(
                            element_type->repr(), element_term.effective_type()->repr()
                        );
                        return {};
                    }
                } else {
                    return {};
                }
            }
            std::array<const Object*, 2> template_args = {
                element_type, new IntegerValue(&IntegerType::u64_instance, node->elements.size())
            };
            Symbol result_type_symbol = sema_.template_handler_->instantiate(
                *Sema::get<SymbolKind::Template>(array_type_symbol), template_args
            );
            const Type* result_type = Sema::get_default<SymbolKind::Type>(result_type_symbol);
            return result_type ? Term::of(result_type) : Symbol{};
        }
    }

    auto operator()(const ASTMoveExpr* node) noexcept -> Symbol {
        Symbol inner_symbol = ValueContextEvaluator{*this, nullptr}(node->inner);
        if (!Sema::expect(inner_symbol, SymbolKind::Term)) {
            return {};
        }
        Term inner_term = Sema::get<SymbolKind::Term>(inner_symbol);
        if (auto* ref_type = inner_term.effective_type()->dyn_cast<ReferenceType>()) {
            if (ref_type->is_moved_) {
                Diagnostic::error_double_move(node->location);
                return {};
            }
            return Term::of(TypeRegistry::get<ReferenceType>(ref_type->target_type_, true, true));
        }
        Diagnostic::error_move_non_reference(node->location, inner_term.effective_type()->repr());
        return {};
    }

    auto operator()(const ASTFunctionCall* node) noexcept -> Symbol {
        GlobalMemory::Vector<const Type*> args_type;
        args_type.reserve(node->arguments.size());
        for (const ASTExprVariant& arg : node->arguments) {
            Symbol arg_symbol = ValueContextEvaluator{*this, nullptr}(arg);
            if (Sema::expect(arg_symbol, SymbolKind::Term)) {
                args_type.push_back(
                    Sema::get<SymbolKind::Term>(arg_symbol).resolve_to_default().effective_type()
                );
            } else {
                args_type.push_back(nullptr);
            }
        }
        if (std::ranges::contains(args_type, nullptr)) {
            return {};
        }
        Symbol func_symbol = ValueContextEvaluator{*this, nullptr}(node->function);
        if (holds_monostate(func_symbol)) return {};
        if (auto* partial = Sema::get_if<SymbolKind::PartialInstantiation>(func_symbol)) {
            const FunctionType* func_type = sema_.template_handler_->inference(
                std::get<0>(*partial), std::get<1>(*partial), std::get<2>(*partial), args_type
            );
            if (!func_type) {
                Diagnostic::error_no_matching_overload(
                    node->location, Sema::args_repr(std::span{args_type})
                );
                return {};
            }
            if (!std::get<0>(*partial)->is_extern_) {
                sema_.codegen_env_.map_func_call(
                    sema_.current_scope_,
                    node,
                    func_type,
                    std::get<0>(*partial),
                    std::get<1>(*partial)->identifier,
                    nullptr
                );
            }
            return Term::of(func_type->return_type_);
        }
        return Sema::nonnull(sema_.call_handler_->eval_call(node, func_symbol, args_type).first);
    }

    auto operator()(const ASTTemplateInstantiation* node) noexcept -> Symbol {
        return sema_.template_handler_->instantiate(node);
    }

    auto operator()(const ASTTernaryOp* node) noexcept -> Symbol {
        Symbol condition_symbol = ValueContextEvaluator{*this, nullptr}(node->condition);
        if (!Sema::expect(condition_symbol, SymbolKind::Term)) {
            return {};
        }
        Term condition_term = Sema::get<SymbolKind::Term>(condition_symbol);
        if (condition_term.decay() != &BooleanType::instance) {
            Diagnostic::error_type_mismatch("bool", condition_term.effective_type()->repr());
            return {};
        }
        Symbol then_symbol = (*this)(node->true_expr);
        Symbol else_symbol = (*this)(node->false_expr);
        bool any_error = !Sema::expect(then_symbol, SymbolKind::Term);
        any_error |= !Sema::expect(else_symbol, SymbolKind::Term);
        if (any_error) {
            return {};
        }
        Term then_term = Sema::get<SymbolKind::Term>(then_symbol).resolve_to_default();
        Term else_term = Sema::get<SymbolKind::Term>(else_symbol).resolve_to_default();
        if (then_term.decay() != else_term.decay()) {
            Diagnostic::error_type_mismatch(
                then_term.effective_type()->repr(), else_term.effective_type()->repr()
            );
            return {};
        }
        if (expected_) {
            AutoBindings auto_bindings;
            if (!CallHandler::assignable(then_term.effective_type(), expected_)) {
                Diagnostic::error_type_mismatch(
                    expected_->repr(), then_term.effective_type()->repr()
                );
                return {};
            }
            return Term::of(expected_);
        }
        return Term::of(then_term.effective_type());
    }

    auto operator()(const ASTAs* node) noexcept -> Symbol {
        Symbol expr_symbol = ValueContextEvaluator{*this, nullptr}(node->expr);
        if (!Sema::expect(expr_symbol, SymbolKind::Term)) {
            return {};
        }
        Term expr_term = Sema::get<SymbolKind::Term>(expr_symbol);
        TypeResolution target_type;
        TypeContextEvaluator{sema_, target_type}(node->target_type);
        return Sema::nonnull(sema_.operation_handler_->eval_cast(node, expr_term, target_type));
    }

    auto operator()(const ASTLambda* node) noexcept -> Symbol {
        if (!node->visited) {
            node->visited = true;
            sema_.deferred_analysis(*sema_.current_scope_, node);
        }
        GlobalMemory::Vector<const Type*> param_types;
        param_types.reserve(node->parameters.size());
        for (const ASTFunctionParameter& param : node->parameters) {
            TypeResolution param_type;
            TypeContextEvaluator{sema_, param_type}(param.type);
            param_types.push_back(param_type);
        }
        TypeResolution return_type;
        if (!holds_monostate(node->return_type)) {
            TypeContextEvaluator{sema_, return_type}(node->return_type);
        } else {
            return_type = &VoidType::instance;
        }
        if (std::ranges::contains(param_types, nullptr) || return_type.get() == nullptr) {
            return {};
        }
        const Type* func_type = TypeRegistry::get<FunctionType>(
            param_types | GlobalMemory::collect<std::span>(), return_type
        );
        sema_.codegen_env_.map_type(sema_.current_scope_, node, func_type);
        return Term::of(func_type);
    }

    auto operator()(const auto* node) noexcept -> Symbol { UNREACHABLE(); }

private:
    auto struct_validate(
        const Type* type, GlobalMemory::FlatMap<strview, const Type*> inits
    ) noexcept -> bool {
        GlobalMemory::FlatMap<strview, const Type*> field_types;
        if (auto struct_type = type->dyn_cast<StructType>()) {
            field_types = decltype(field_types)(std::from_range, struct_type->fields_);
        } else if (auto instance_type = type->dyn_cast<InstanceType>()) {
            field_types = decltype(field_types)(std::from_range, instance_type->attrs_);
        } else {
            Diagnostic::error_type_mismatch("struct or class", type->repr());
            return false;
        }
        bool valid = true;
        AutoBindings auto_bindings;
        for (auto [field_name, field_type] : field_types) {
            auto init_it = inits.find(field_name);
            if (init_it == inits.end()) {
                if (!field_type->default_construct()) {
                    Diagnostic::error_uninitialized_attribute(field_name);
                    valid = false;
                }
            } else {
                if (!CallHandler::assignable(init_it->second, field_type)) {
                    Diagnostic::error_type_mismatch(field_type->repr(), init_it->second->repr());
                    valid = false;
                }
                inits.erase(init_it);
            }
        }
        if (!inits.empty()) {
            for (const auto& [id, _] : inits) {
                Diagnostic::error_unrecognized_attribute(id);
            }
            return false;
        }
        return valid;
    }
};

class TypeCheckVisitor {
private:
    Sema& sema_;

public:
    TypeCheckVisitor(Sema& sema) noexcept : sema_(sema) {}

    void operator()(const ASTNodeVariant& node) noexcept {
        Diagnostic::ErrorTrap trap{ASTNodePtrGetter{}(node)->location};
        std::visit(*this, node);
    }

    void operator()(std::monostate) noexcept { UNREACHABLE(); }

    void operator()(const auto*) {}

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        if (node->type_checked) return;
        node->type_checked = true;
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
        bool is_global = sema_.current_scope_->parent_ == nullptr;
        if (is_global && node->is_mutable) {
            Diagnostic::error_mutable_global_variable(node->location);
        }
        TypeResolution type;
        if (!holds_monostate(node->declared_type)) {
            TypeContextEvaluator{sema_, type}(node->declared_type);
        }
        if (!holds_monostate(node->expr)) {
            Symbol init = ValueContextEvaluator{sema_, type.get(), false}(node->expr);
            if (!Sema::expect(init, SymbolKind::Term)) {
                return;
            }
            Term init_term = Sema::get<SymbolKind::Term>(init);
            if (type.get()) {
                if (!CallHandler::assignable(init_term.effective_type(), type.get())) {
                    Diagnostic::error_type_mismatch(
                        type.get()->repr(), init_term.effective_type()->repr()
                    );
                }
            } else {
                init_term = init_term.resolve_to_default();
                type = init_term.decay();
            }
            if (node->is_constant && !init_term.is_comptime()) {
                Diagnostic::error_not_constant_expression(node->location);
            }
            if (is_global && !sema_.current_scope_->is_extern_) {
                sema_.codegen_env_.add_global(
                    sema_.current_scope_, node->identifier, init_term.get_comptime()
                );
            }
        }
        if (node->is_mutable && type.get() && Type::category(type.get()) != ValueCategory::Right) {
            Diagnostic::error_mutable_variable_with_immutable_type(
                node->location, type.get()->repr()
            );
        }
        if (type.get()) {
            sema_.codegen_env_.map_type(sema_.current_scope_, node, type);
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

    void operator()(const ASTSwitchStatement* node) noexcept {
        ValueContextEvaluator{sema_, nullptr, false}(node->condition);
        for (const ASTSwitchCase& switch_case : node->cases) {
            if (!holds_monostate(switch_case.value)) {
                ValueContextEvaluator{sema_, nullptr, false}(switch_case.value);
            }
            (*this)(switch_case.body);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            TypeCheckVisitor{sema_}(*decl);
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
        const FunctionType* func = sema_.call_handler_->get_func_type(sema_.current_scope_, node);
        Sema::Guard guard(sema_, node);
        if (func) {
            sema_.codegen_env_.add_function_output(sema_.current_scope_, node, func);
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTCtorDtorDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        if (node->is_constructor) {
            const FunctionType* func =
                sema_.call_handler_->get_func_type(sema_.current_scope_, node);
            if (func) {
                sema_.codegen_env_.add_function_output(sema_.current_scope_, node, func);
            }
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTOperatorDefinition* node) noexcept {
        const FunctionType* func = sema_.call_handler_->get_func_type(sema_.current_scope_, node);
        Sema::Guard guard(sema_, node);
        if (func) {
            sema_.codegen_env_.add_function_output(sema_.current_scope_, node, func);
        }
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        for (auto& field : node->fields) {
            (*this)(field);
        }
        for (auto& item : node->scope_items) {
            (*this)(item);
        }
    }

    void operator()(const ASTEnumDefinition* node) noexcept {
        Sema::Guard guard(sema_, node);
        for (std::size_t i = 0; i < node->enumerators.size(); ++i) {
            sema_.codegen_env_.add_global(
                sema_.current_scope_,
                node->enumerators[i],
                new IntegerValue(&IntegerType::i32_instance, i)
            );
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

    void operator()(const ASTLambda* node) noexcept {
        GlobalMemory::Vector<const Type*> param_types;
        param_types.reserve(node->parameters.size());
        for (const ASTFunctionParameter& param : node->parameters) {
            TypeResolution param_type;
            TypeContextEvaluator{sema_, param_type}(param.type);
            param_types.push_back(param_type);
        }
        TypeResolution return_type;
        if (!holds_monostate(node->return_type)) {
            TypeContextEvaluator{sema_, return_type}(node->return_type);
        } else {
            return_type = &VoidType::instance;
        }
        if (std::ranges::contains(param_types, nullptr) || return_type.get() == nullptr) {
            return;
        }
        Sema::Guard guard{sema_, node};
        if (auto* expr = std::get_if<ASTExprVariant>(&node->body)) {
            ValueContextEvaluator{sema_, nullptr, false}(*expr);
        } else {
            TypeCheckVisitor{sema_}(std::get<ASTNodeVariant>(node->body));
        }
    }

    void operator()(const ASTThrowStatement* node) noexcept {
        if (!holds_monostate(node->expr)) {
            ValueContextEvaluator{sema_, nullptr, false}(node->expr);
        }
    }

    void operator()(const ASTImportStatement* node) noexcept {
        Sema::Guard guard{sema_, *node->module_root->scope};
        (*this)(node->module_root);
    }

    void operator()(const ASTCppBlock* node) noexcept {
        sema_.codegen_env_.add_cpp_block(node->code);
    }
};

// ========== Implementation ==========

inline Sema::Sema(Scope& std_scope, Scope& root, CodeGenEnvironment& codegen_env) noexcept
    : std_scope_(std_scope),
      current_scope_(&root),
      codegen_env_(codegen_env),
      template_handler_(std::make_unique<TemplateHandler>(*this)),
      operation_handler_(std::make_unique<OperationHandler>(*this)),
      access_handler_(std::make_unique<AccessHandler>(*this)),
      call_handler_(std::make_unique<CallHandler>(*this)) {}

inline auto Sema::deferred_analysis(Scope& scope, auto variant) noexcept -> void {
    scope.is_instantiating_template_ = true;
    SymbolCollector{&std_scope_, &scope}(variant);
    Guard guard(*this, scope);
    TypeCheckVisitor{*this}(variant);
}

inline auto Sema::eval_type(Scope& scope, const ScopeValue& value) noexcept -> TypeResolution {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        }
        Diagnostic::error_symbol_category_mismatch("type", "value");
        return nullptr;
    }
    // Check cache
    auto [it_id_cache, inserted] = type_cache_.insert({{&scope, &value}, TypeResolution()});
    if (!inserted) {
        return it_id_cache->second;
    }
    // Cache miss; resolve
    Guard guard(*this, scope);
    if (auto type_provider = value.get<TypeProvider*>()) {
        TypeContextEvaluator evaluator{*this, it_id_cache->second};
        switch (type_provider->kind) {
        case TypeProvider::Kind::Alias:
            evaluator(static_cast<const ASTTypeAlias*>(type_provider->node));
            break;
        case TypeProvider::Kind::Class:
            evaluator(static_cast<const ASTClassDefinition*>(type_provider->node));
            break;
        case TypeProvider::Kind::Enum:
            evaluator(static_cast<const ASTEnumDefinition*>(type_provider->node));
            break;
        }
    } else {
        Symbol symbol = eval_symbol(scope, value);
        Sema::expect(symbol, SymbolKind::Type);
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

inline auto Sema::eval_symbol(Scope& scope, const ScopeValue& value) noexcept -> Symbol {
    if (auto* object = value.get<const Object*>()) {
        if (auto* type = object->dyn_type()) {
            return type;
        } else {
            return Term::of(object->cast<Value>());
        }
    } else if (auto* pack = value.get<std::span<const Object*>*>()) {
        return *pack;
    } else if (value.get<TypeProvider*>()) {
        TypeResolution out = eval_type(scope, value);
        return out.get() ? Symbol{out.get()} : Symbol{};
    } else if (auto var_init = value.get<const VariableInitialization*>()) {
        return eval_var_init(scope, *var_init);
    } else if (value.get<GlobalMemory::Vector<FunctionOverloadDef>*>()) {
        return std::pair{&scope, &value};
    } else if (auto* template_family = value.get<TemplateFamily*>()) {
        return template_family;
    } else if (auto* scope_ptr = value.get<Scope*>()) {
        return scope_ptr;
    }
    UNREACHABLE();
}

inline auto Sema::eval_var_init(Scope& scope, const VariableInitialization& init) noexcept
    -> Symbol {
    assert(!holds_monostate(init.type) || !holds_monostate(init.value));
    Guard guard(*this, scope);
    TypeResolution type{};
    if (!holds_monostate(init.type)) {
        TypeContextEvaluator{*this, type}(init.type);
        if (!type.get()) return {};
    }
    if (!holds_monostate(init.value)) {
        Symbol init_symbol = ValueContextEvaluator{*this, type.get(), false}(init.value);
        if (!Sema::expect(init_symbol, SymbolKind::Term)) return {};
        Term init_term = std::get<Term>(init_symbol).resolve_to_default();
        if (!init_term) return {};
        if (type.get()) {
            if (!CallHandler::assignable(init_term.effective_type(), type.get())) {
                return {};
            }
            if (init.is_comptime) return init_term;
        } else {
            if (init.is_comptime) return init_term;
            type = init_term.decay();
        }
    }
    if (init.is_mutable) {
        if (Type::category(type.get()) != ValueCategory::Right) {
            return {};
        }
        return Term::lvalue(type.get(), true);
    }
    return Term::lvalue(type.get(), false);
}

inline auto TemplateHandler::inference(
    Scope* scope,
    const ASTTemplateDefinition* primary,
    TemplateArgs explicit_instantiation_args,
    std::span<const Type*> args_type
) noexcept -> const FunctionType* {
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
    if (param_count != args_type.size() && variadic_param.empty()) {
        Diagnostic::error_cannot_infer_template_argument(ASTNodePtrGetter{}(primary)->location);
        return {};
    }
    // prepare auto instances
    std::size_t auto_counts = primary->parameters.size() + args_type.size() - param_count;
    std::span<const AutoObject*> auto_instances = TypeRegistry::get_auto_instances(auto_counts);
    Scope& pattern_scope = Scope::make(*scope);
    for (std::size_t i = 0; i < primary->parameters.size(); i++) {
        const ASTTemplateParameter& param = primary->parameters[i];
        if (param.is_variadic) break;
        pattern_scope.set_template_argument(
            param.identifier, auto_instances[i]->as_object(param.is_nttp)
        );
    }
    // make patterns
    Sema::Guard guard(sema_, pattern_scope);
    const ASTNode* func_node;
    GlobalMemory::Vector<const Type*> patterns;
    if (auto* func_def = std::get_if<const ASTFunctionDefinition*>(&primary->target_node)) {
        func_node = *func_def;
        for (const auto& pattern : (*func_def)->parameters) {
            if (pattern.is_variadic) break;
            Symbol pattern_symbol = ValueContextEvaluator{sema_, nullptr, false}(pattern.type);
            patterns.push_back(Sema::get_default<SymbolKind::Type>(pattern_symbol));
        }
        if (!variadic_param.empty() && args_type.size() >= param_count) {
            for (size_t i = 0; i <= args_type.size() - param_count; i++) {
                const AutoObject* auto_inst = auto_instances[i + primary->parameters.size() - 1];
                pattern_scope.set_template_argument(variadic_param, auto_inst->as_object(false));
                Symbol pattern_symbol = ValueContextEvaluator{
                    sema_, nullptr, false
                }((*func_def)->parameters.back().type);
                patterns.push_back(Sema::get_default<SymbolKind::Type>(pattern_symbol));
            }
        }
    } else if (auto* ctor_def = std::get_if<const ASTCtorDtorDefinition*>(&primary->target_node)) {
        func_node = *ctor_def;
        for (const auto& pattern : (*ctor_def)->parameters) {
            if (pattern.is_variadic) break;
            Symbol pattern_symbol = ValueContextEvaluator{sema_, nullptr, false}(pattern.type);
            patterns.push_back(Sema::get_default<SymbolKind::Type>(pattern_symbol));
        }
        if (!variadic_param.empty() && args_type.size() >= param_count) {
            for (size_t i = 0; i <= args_type.size() - param_count; i++) {
                const AutoObject* auto_inst = auto_instances[i + primary->parameters.size() - 1];
                pattern_scope.set_template_argument(variadic_param, auto_inst->as_object(false));
                Symbol pattern_symbol = ValueContextEvaluator{
                    sema_, nullptr, false
                }((*ctor_def)->parameters.back().type);
                patterns.push_back(Sema::get_default<SymbolKind::Type>(pattern_symbol));
            }
        }
    } else if (auto* op_def = std::get_if<const ASTOperatorDefinition*>(&primary->target_node)) {
        func_node = *op_def;
        Symbol left_pattern_symbol =
            ValueContextEvaluator{sema_, nullptr, false}((*op_def)->left.type);
        patterns.push_back(Sema::get_default<SymbolKind::Type>(left_pattern_symbol));
        if ((*op_def)->right) {
            Symbol right_pattern_symbol =
                ValueContextEvaluator{sema_, nullptr, false}((*op_def)->right->type);
            patterns.push_back(Sema::get_default<SymbolKind::Type>(right_pattern_symbol));
        }
    } else {
        // template cannot appears on another template
        UNREACHABLE();
    }
    // inject explicit instantiation arguments
    AutoBindings auto_bindings;
    if (explicit_instantiation_args.size() > auto_instances.size()) {
        Diagnostic::error_cannot_infer_template_argument(ASTNodePtrGetter{}(primary)->location);
        return {};
    }
    for (size_t i = 0; i < explicit_instantiation_args.size(); i++) {
        auto_bindings[auto_instances[i]] = explicit_instantiation_args[i];
    }
    // pattern match and auto binding
    for (std::size_t i = 0; i < patterns.size(); i++) {
        if (patterns[i] == nullptr) return nullptr;
        if (!CallHandler::assignable(args_type[i], patterns[i], auto_bindings)) {
            Diagnostic::error_candidate_type_mismatch(
                func_node->location, i, patterns[i]->repr(), args_type[i]->repr()
            );
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
        auto [scope, value] = Sema::get<SymbolKind::Function>(func_symbol);
        FunctionOverloadDef overload_def =
            (*value->get<GlobalMemory::Vector<FunctionOverloadDef>*>())[0];
        if (auto* func_def = overload_def.get<const ASTFunctionDefinition*>()) {
            return sema_.call_handler_->get_func_type(scope, func_def);
        } else if (auto* ctor_def = overload_def.get<const ASTCtorDtorDefinition*>()) {
            return sema_.call_handler_->get_func_type(scope, ctor_def);
        } else if (auto* op_def = overload_def.get<const ASTOperatorDefinition*>()) {
            return sema_.call_handler_->get_func_type(scope, op_def);
        } else {
            UNREACHABLE();
        }
    }
    UNREACHABLE();
}

inline auto TemplateHandler::instantiate(const ASTTemplateInstantiation* node) noexcept -> Symbol {
    Symbol template_symbol = ValueContextEvaluator{sema_, nullptr, true}(node->template_expr);
    if (!Sema::expect(template_symbol, SymbolKind::Template, SymbolKind::Function)) {
        return {};
    }
    GlobalMemory::Vector<const Object*> instantiation_args;
    for (const ASTExprVariant& arg : node->arguments) {
        Diagnostic::ErrorTrap trap{ASTNodePtrGetter{}(arg)->location};
        Symbol result = ValueContextEvaluator{sema_, nullptr, true}(arg);
        if (Sema::expect(result, SymbolKind::Type, SymbolKind::Term)) {
            instantiation_args.push_back(
                Sema::get_if<SymbolKind::Type>(result)
                    ? static_cast<const Object*>(Sema::get<SymbolKind::Type>(result))
                    : Sema::get<SymbolKind::Term>(result).get_comptime()
            );
        } else {
            instantiation_args.push_back(nullptr);
        }
    }
    if (std::ranges::contains(instantiation_args, nullptr)) {
        return {};
    }
    sema_.codegen_env_.map_instantiation(
        sema_.current_scope_, node, instantiation_args | GlobalMemory::collect<std::span>()
    );
    if (auto* family = Sema::get_if<SymbolKind::Template>(template_symbol)) {
        // normal template instantiation
        return instantiate(**family, instantiation_args);
    } else {
        // explicit function instantiation
        auto [scope, value] = Sema::get<SymbolKind::Function>(template_symbol);
        const auto& overloads = *value->get<GlobalMemory::Vector<FunctionOverloadDef>*>();
        const ASTTemplateDefinition* primary = nullptr;
        for (const auto& overload : overloads) {
            if (auto* p = overload.get<const ASTTemplateDefinition*>()) {
                if (std::exchange(primary, p) != nullptr) {
                    Diagnostic::error_ambiguous_template_instantiation(node->location);
                    return {};
                }
            }
        }
        if (!primary) {
            Diagnostic::error_symbol_category_mismatch(
                node->location, "template", "function overload set"
            );
            return {};
        }
        return std::tuple{scope, primary, instantiation_args | GlobalMemory::collect<std::span>()};
    }
}

inline auto TemplateHandler::validate(
    const ASTTemplateDefinition& primary, TemplateArgs args
) noexcept -> bool {
    if (primary.parameters.size() != args.size() && !primary.parameters.back().is_variadic) {
        return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
        const auto& param = primary.parameters[std::min(i, primary.parameters.size() - 1)];
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
        std::span<const AutoObject*> auto_instances =
            TypeRegistry::get_auto_instances(best_candidate->parameters.size());
        for (std::size_t i = 0; i < best_candidate->parameters.size(); i++) {
            const ASTTemplateParameter& param = best_candidate->parameters[i];
            inst_scope.set_template_argument(
                param.identifier, best_auto_bindings[auto_instances[i]]
            );
        }
        return {&inst_scope, best_candidate->target_node};
    }
    // last, fallback to primary template
    Scope& inst_scope = Scope::make(*sema_.current_scope_);
    Sema::Guard guard(sema_, inst_scope);
    for (size_t i = 0; i < args.size(); i++) {
        const ASTTemplateParameter& param = family.primary->parameters[i];
        inst_scope.set_template_argument(param.identifier, args[i]);
    }
    return {&inst_scope, family.primary->target_node};
}

inline auto TemplateHandler::get_prototype(
    Scope& pattern_scope, const ASTTemplateSpecialization& specialization
) noexcept -> const SpecializationPrototype& {
    std::span<const AutoObject*> auto_instances =
        TypeRegistry::get_auto_instances(specialization.parameters.size());
    std::span<const SkolemObject*> skolem_objects =
        TypeRegistry::get_skolem_objects(specialization.parameters.size());
    Sema::Guard guard(sema_, pattern_scope);
    auto [it, inserted] = pattern_cache_.insert({&specialization, SpecializationPrototype()});
    if (!inserted) {
        return it->second;
    }
    it->second.patterns = GlobalMemory::alloc_array<const Object*>(specialization.patterns.size());
    it->second.skolems = GlobalMemory::alloc_array<const Object*>(specialization.patterns.size());
    for (size_t i = 0; i < specialization.parameters.size(); i++) {
        const ASTTemplateParameter& param = specialization.parameters[i];
        pattern_scope.set_template_argument(
            param.identifier, auto_instances[i]->as_object(param.is_nttp)
        );
    }
    for (size_t i = 0; i < specialization.patterns.size(); i++) {
        Symbol result = ValueContextEvaluator{sema_, nullptr, true}(specialization.patterns[i]);
        assert(Sema::get_if<SymbolKind::Type>(result));
        it->second.patterns[i] = Sema::get<SymbolKind::Type>(result);
    }
    pattern_scope.clear();
    for (std::size_t i = 0; i < specialization.parameters.size(); i++) {
        const ASTTemplateParameter& param = specialization.parameters[i];
        pattern_scope.set_template_argument(
            param.identifier, skolem_objects[i]->as_object(param.is_nttp)
        );
    }
    for (size_t i = 0; i < specialization.patterns.size(); i++) {
        Symbol result = ValueContextEvaluator{sema_, nullptr, false}(specialization.patterns[i]);
        assert(Sema::get_if<SymbolKind::Type>(result));
        it->second.skolems[i] = Sema::get<SymbolKind::Type>(result);
    }
    pattern_scope.clear();
    return it->second;
}

inline auto CallHandler::get_func_type(Scope* scope, FunctionOverloadDef overload) noexcept
    -> const FunctionType* {
    auto get_param_type = [&](const ASTFunctionParameter& param) -> const Type* {
        TypeResolution type;
        TypeContextEvaluator{sema_, type}(param.type);
        return type;
    };
    Sema::Guard guard{sema_, *scope};
    GlobalMemory::Vector<const Type*> params;
    TypeResolution return_type;
    if (auto* func_def = overload.get<const ASTFunctionDefinition*>()) {
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
    } else if (auto* ctor_def = overload.get<const ASTCtorDtorDefinition*>()) {
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
    if (std::ranges::contains(params, nullptr) || return_type.get() == nullptr) {
        return {};
    }
    return TypeRegistry::get<FunctionType>(
        params | GlobalMemory::collect<std::span>(), return_type
    );
}

inline auto AccessHandler::eval_access(const ASTMemberAccess* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->base);
    if (!Sema::expect(base_symbol, SymbolKind::Type, SymbolKind::Term, SymbolKind::Namespace)) {
        return {};
    }
    if (auto* type = Sema::get_if<SymbolKind::Type>(base_symbol)) {
        if (auto* instance_type = (*type)->dyn_cast<InstanceType>()) {
            Symbol result = eval_static_access(instance_type->scope_, node->member);
            sema_.codegen_env_.map_member_access(sema_.current_scope_, node, instance_type->scope_);
            return result;
        }
    } else if (auto* term = Sema::get_if<SymbolKind::Term>(base_symbol)) {
        const Type* decayed = term->decay();
        if (decayed->kind_ == Kind::Struct) {
            return Sema::nonnull(eval_struct_access(*term, node->member));
        } else if (auto* instance_type = decayed->dyn_cast<InstanceType>()) {
            Symbol result = eval_instance_access(*term, node->member);
            if (auto* method = Sema::get_if<SymbolKind::Method>(result);
                method && !instance_type->scope_->is_extern_) {
                method->self = new PointerChain{{}, node->base, {}};
                sema_.codegen_env_.map_member_access(sema_.current_scope_, node, method->scope);
            }
            return result;
        }
    } else if (auto* namespace_scope = Sema::get_if<SymbolKind::Namespace>(base_symbol)) {
        Symbol result = eval_static_access(*namespace_scope, node->member);
        sema_.codegen_env_.map_member_access(sema_.current_scope_, node, *namespace_scope);
        return result;
    }
    Diagnostic::error_invalid_member_access(node->member);
    return {};
}

inline auto AccessHandler::eval_pointer(const ASTPointerAccess* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->base);
    if (!Sema::expect(base_symbol, SymbolKind::Term)) {
        return {};
    }
    Term base_term = Sema::get<SymbolKind::Term>(base_symbol);
    Term current_term = base_term;
    GlobalMemory::Vector<std::pair<const InstanceType*, const FunctionType*>> dereference_chain;
    while (true) {
        const Type* decayed = current_term.decay();
        if (decayed == nullptr) {
            return {};
        }
        if (auto* pointer_type = decayed->dyn_cast<PointerType>()) {
            Term dereferenced = Term::lvalue(pointer_type->target_type_, pointer_type->is_mutable_);
            const Type* decayed_dereferenced = dereferenced.decay();
            if (decayed_dereferenced->dyn_cast<StructType>()) {
                sema_.codegen_env_.map_pointer_access(
                    sema_.current_scope_,
                    node,
                    new PointerChain{
                        {}, node->base, dereference_chain | GlobalMemory::collect<std::span>()
                    }
                );
                return Sema::nonnull(eval_struct_access(dereferenced, node->member));
            } else if (decayed_dereferenced->dyn_cast<InstanceType>()) {
                Symbol result = eval_instance_access(dereferenced, node->member);
                PointerChain* chain = new PointerChain{
                    {}, node->base, dereference_chain | GlobalMemory::collect<std::span>()
                };
                if (auto* method = Sema::get_if<SymbolKind::Method>(result)) {
                    method->self = chain;
                }
                sema_.codegen_env_.map_pointer_access(sema_.current_scope_, node, chain);
                return result;
            } else {
                Diagnostic::error_invalid_pointer_access(base_term->repr());
                return {};
            }
        } else if (auto* instance_type = decayed->dyn_cast<InstanceType>()) {
            auto result = sema_.operation_handler_->eval_overloaded_op(
                node, OperatorCode::Pointer, current_term
            );
            current_term = result.first;
            dereference_chain.push_back({instance_type, result.second});
        } else {
            Diagnostic::error_invalid_pointer_access(base_term->repr());
            return {};
        }
    }
    return {};
}

inline auto AccessHandler::eval_deref(const ASTDereference* node) noexcept -> Symbol {
    Symbol base_symbol = ValueContextEvaluator{sema_, nullptr, false}(node->operand);
    if (!Sema::expect(base_symbol, SymbolKind::Term)) {
        return {};
    }
    Term base_term = Sema::get<SymbolKind::Term>(base_symbol);
    const Type* decayed = base_term.decay();
    if (auto* pointer_type = decayed->dyn_cast<PointerType>()) {
        return Sema::nonnull(Term::lvalue(pointer_type->target_type_, pointer_type->is_mutable_));
    } else if (decayed->dyn_cast<InstanceType>()) {
        auto result =
            sema_.operation_handler_->eval_overloaded_op(node, OperatorCode::Deref, base_term);
        return Sema::nonnull(result.first);
    } else {
        Diagnostic::error_operation_not_defined(
            GetOperatorString(OperatorCode::Deref), base_term->repr()
        );
        return {};
    }
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
    const Type* base_type = base_term.decay();
    const Type* index_type = index_term.decay();
    std::array<const Type*, 2> args_type{
        base_term.resolve_to_default().effective_type(),
        index_term.resolve_to_default().effective_type()
    };
    if (auto* instance_type = base_type->dyn_cast<InstanceType>();
        instance_type && is_std_indexable_container(instance_type)) {
        // implicitly cast index to usize
        if (index_term.effective_type() == &IntegerType::untyped_instance) {
            index_type = args_type[1] = &IntegerType::u64_instance;
        }
    }
    return Sema::nonnull(
        sema_.call_handler_
            ->eval_call(node, std::tuple{OperatorCode::Index, base_type, index_type}, args_type)
            .first
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
        std::array<const Object*, 2> array_args = {
            element_type.get(), static_cast<const Object*>(length_term.get_comptime())
        };
        Symbol result = sema_.template_handler_->instantiate(
            *std::get<TemplateFamily*>(array_symbol), array_args
        );
        out_ = Sema::get_default<SymbolKind::Type>(result);
    }
}
