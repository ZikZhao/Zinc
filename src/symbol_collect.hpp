#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"

using FunctionOverloadDecl = PointerVariant<
    const ASTFunctionDefinition*,
    const ASTConstructorDestructorDefinition*,
    const ASTTemplateDefinition*>;

struct TemplateFamily : public GlobalMemory::MonotonicAllocated {
    const ASTTemplateDefinition& primary;
    GlobalMemory::Vector<const ASTTemplateSpecialization*> specializations;
};

struct VariableInitialization : public GlobalMemory::MonotonicAllocated {
    ASTExprVariant type;
    ASTExprVariant value;
};

using ScopeValue = PointerVariant<
    Term*,                                        // type/comptime (from template)
    const ASTExprVariant*,                        // type alias
    const ASTClassDefinition*,                    // class definition
    const VariableInitialization*,                // comptime/variable declaration
    GlobalMemory::Vector<FunctionOverloadDecl>*,  // function overloads
    TemplateFamily*,                              // template definition
    const Scope*>;                                // namespace

class Scope final : public GlobalMemory::MonotonicAllocated {
public:
    static auto root(Scope& std_scope) -> Scope& {
        auto* scope = new Scope(nullptr);
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static auto make(Scope& parent, const ASTNode* origin = nullptr) -> Scope& {
        auto* scope = new Scope(&parent);
        parent.children_.insert({origin, scope});
        return *scope;
    }

public:
    Scope* const parent_ = nullptr;
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;
    const Type* self_type_;
    bool in_constructor_;

private:
    Scope(Scope* parent) noexcept : parent_(parent) {}

public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope(Scope&&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;
    auto operator=(Scope&&) -> Scope& = delete;
    ~Scope() noexcept = default;

    void add_template_argument(std::string_view identifier, Term term) {
        auto [_, inserted] = identifiers_.insert({identifier, new Term(term)});
        assert(inserted);
    }

    void add_alias(std::string_view identifier, const ASTExprVariant* expr) {
        auto [_, inserted] = identifiers_.insert({identifier, expr});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_class(std::string_view identifier, const ASTClassDefinition* class_def) {
        auto [_, inserted] = identifiers_.insert({identifier, class_def});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_variable(std::string_view identifier, const VariableInitialization* init) {
        auto [_, inserted] = identifiers_.insert({identifier, init});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_function(std::string_view identifier, const ASTFunctionDefinition* expr) {
        if (auto it = identifiers_.find(identifier); it == identifiers_.end()) {
            auto overloads = GlobalMemory::alloc<GlobalMemory::Vector<FunctionOverloadDecl>>();
            overloads->push_back(expr);
            identifiers_[identifier] = overloads;
        } else {
            auto overloads = it->second.get<GlobalMemory::Vector<FunctionOverloadDecl>*>();
            if (!overloads) {
                throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
            }
            overloads->push_back(expr);
        }
    }

    void add_template(std::string_view identifier, const ASTTemplateDefinition& definition) {
        auto [_, inserted] =
            identifiers_.insert({identifier, new TemplateFamily{.primary = definition}});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_template(
        std::string_view identifier, const ASTTemplateSpecialization* specialization
    ) {
        auto it = identifiers_.find(identifier);
        if (it == identifiers_.end()) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        auto family = it->second.get<TemplateFamily*>();
        if (!family) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
        family->specializations.push_back(specialization);
    }

    void add_namespace(std::string_view identifier, Scope& scope) {
        auto [_, inserted] = identifiers_.insert({identifier, &scope});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    [[nodiscard]] auto find(std::string_view identifier) const noexcept -> const ScopeValue* {
        auto it = identifiers_.find(identifier);
        if (it != identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

class SemaInfrastructure {
protected:
    GlobalMemory::MultiMap<OperatorCode, FunctionOverloadDecl> operators_;

public:
    Scope* current_scope_;

public:
    auto register_operator(OperatorCode opcode, FunctionOverloadDecl operator_def) -> void {
        operators_.insert({opcode, operator_def});
    }

    [[nodiscard]] auto lookup(std::string_view identifier) const noexcept
        -> std::pair<Scope*, const ScopeValue*> {
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

protected:
    auto value_decay(Term term, bool* is_mutable = nullptr) const -> Term {
        if (!term) return term;
        if (term.is_type()) return term;

        if (auto ref_value = term->dyn_cast<ReferenceValue>()) {
            return value_decay(Term::lvalue(ref_value->referenced_value_), is_mutable);
        } else if (auto ref_type = term->dyn_cast<ReferenceType>()) {
            return value_decay(Term::lvalue(ref_type->referenced_type_), is_mutable);
        } else if (auto ptr_value = term->dyn_cast<PointerValue>()) {
            return value_decay(Term::lvalue(ptr_value->pointed_value_), is_mutable);
        } else if (auto ptr_type = term->dyn_cast<PointerType>()) {
            return value_decay(Term::lvalue(ptr_type->pointed_type_), is_mutable);
        } else if (auto mut_type = term->dyn_cast<MutableType>()) {
            if (is_mutable) *is_mutable = true;
            return value_decay(Term::lvalue(mut_type->target_type_), is_mutable);
        } else if (auto mut_value = term->dyn_cast<MutableValue>()) {
            if (is_mutable) *is_mutable = false;
            return value_decay(Term::lvalue(mut_value->value_), is_mutable);
        }
        return term;
    }

    [[nodiscard]] auto wrap_in_mutable(Term term) const -> Term {
        if (auto value = term.get_comptime()) {
            return Term::forward_like(
                term, new MutableValue(TypeRegistry::get<MutableType>(term.effective_type()), value)
            );
        } else {
            return Term::forward_like(term, TypeRegistry::get<MutableType>(term.effective_type()));
        }
    }

    [[nodiscard]] auto apply_category(Term obj) const -> const Type* {
        if (obj.is_type()) {
            /// TODO: throw type is not a valid argument error
            return &UnknownType::instance;
        }
        const Type* type = obj.effective_type();
        if (obj.value_category() == ValueCategory::Left) {
            return TypeRegistry::get<ReferenceType>(type, false);
        } else if (obj.value_category() == ValueCategory::Expiring) {
            return TypeRegistry::get<ReferenceType>(type, true);
        } else {
            return type;
        }
    }

    [[nodiscard]] auto extract_arg_types(std::span<Term> args) const
        -> GlobalMemory::Vector<const Type*> {
        return args | std::views::transform([this](Term arg) -> const Type* {
                   return apply_category(arg);
               }) |
               GlobalMemory::collect<GlobalMemory::Vector<const Type*>>();
    }
};

class SymbolCollector {
private:
    Scope& current_scope_;
    SemaInfrastructure& sema_;

public:
    SymbolCollector(Scope& scope, SemaInfrastructure& sema) noexcept
        : current_scope_(scope), sema_(sema) {}

    void operator()(const ASTNodeVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(const ASTExprVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(std::monostate) noexcept { UNREACHABLE(); }

    void operator()(const auto*) noexcept {}

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        for (auto& child : node->statements) {
            (*this)(child);
        }
    }

    void operator()(const ASTLocalBlock* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollector local_visitor(local_scope, sema_);
        for (auto& stmt : node->statements) {
            local_visitor(stmt);
        }
    }

    // Struct initialization
    void operator()(const ASTStructInitialization* node) noexcept {
        auto fields =
            node->field_inits |
            std::views::transform([&](const ASTFieldInitialization& init) -> std::string_view {
                return init.identifier;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector>();
        std::ranges::sort(fields);
        if (std::ranges::adjacent_find(fields) != fields.end()) {
            throw UnlocatedProblem::make<DuplicateAttributeError>(fields.front());
        }
    }

    // Declarations
    void operator()(const ASTDeclaration* node) noexcept {
        try {
            current_scope_.add_variable(
                node->identifier,
                new VariableInitialization{.type = node->declared_type, .value = node->expr}
            );
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
        }
    }

    void operator()(const ASTTypeAlias* node) noexcept {
        current_scope_.add_alias(node->identifier, &node->type);
    }

    // Statements
    void operator()(const ASTIfStatement* node) noexcept {
        Scope& condition_scope = Scope::make(current_scope_, node);
        SymbolCollector condition_visitor(condition_scope, sema_);
        condition_visitor(node->condition);
        condition_visitor(&node->if_block);
        if (node->else_block) {
            condition_visitor(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollector local_visitor(local_scope, sema_);
        if (node->initializer_decl) {
            local_visitor(node->initializer_decl);
        } else if (!std::holds_alternative<std::monostate>(node->initializer_expr)) {
            local_visitor(node->initializer_expr);
        }
        /// TODO: refactor to local scope
        local_visitor(&node->body);
    }

    // Functions
    void operator()(ASTFunctionDefinition* node) noexcept {
        current_scope_.add_function(node->identifier, node);
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{.type = param.type, .value = std::monostate{}}
            );
        }
        SymbolCollector local_visitor(local_scope, sema_);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTConstructorDestructorDefinition* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{.type = param.type, .value = std::monostate{}}
            );
        }
        SymbolCollector local_visitor(local_scope, sema_);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    // Classes
    void operator()(const ASTClassDefinition* node) noexcept {
        current_scope_.add_class(node->identifier, node);
        Scope& class_scope = Scope::make(current_scope_, node);
        SymbolCollector class_visitor(class_scope, sema_);
        for (auto& ctor : node->constructors) {
            class_visitor(ctor);
        }
        if (node->destructor) {
            class_visitor(node->destructor);
        }
        for (auto& func : node->functions) {
            class_visitor(func);
        }
    }

    // Namespaces
    void operator()(const ASTNamespaceDefinition* node) noexcept {
        Scope& namespace_scope = Scope::make(current_scope_, node);
        SymbolCollector namespace_visitor(namespace_scope, sema_);
        for (auto& item : node->items) {
            namespace_visitor(item);
        }
        current_scope_.add_namespace(node->identifier, namespace_scope);
    }

    // Templates
    void operator()(const ASTTemplateDefinition* node) noexcept {
        current_scope_.add_template(node->identifier, *node);
    }
};
