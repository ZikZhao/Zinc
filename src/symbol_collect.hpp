#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"

using FunctionOverloadDecl =
    PointerVariant<const ASTFunctionDefinition*, const ASTTemplateDefinition*>;

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
    friend class TypeChecker;

public:
    static auto root(Scope& std_scope, const ASTNode* root) -> Scope& {
        Scope* scope = new Scope(nullptr, root);
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static auto make(Scope& parent, const ASTNode* origin) -> Scope& {
        Scope* scope = new Scope(&parent, origin);
        parent.children_.insert({origin, scope});
        return *scope;
    }

    static auto make_unlinked(Scope& parent, const ASTNode* origin) -> Scope& {
        Scope* scope = new Scope(&parent, origin);
        return *scope;
    }

private:
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;

public:
    Scope* const parent_ = nullptr;
    const ASTNode* const origin_ = nullptr;
    const Type* self_type_ = nullptr;

private:
    Scope(Scope* parent, const ASTNode* origin) noexcept : parent_(parent), origin_(origin) {}

public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

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

    auto operator[](std::string_view identifier) const noexcept -> const ScopeValue* {
        auto it = identifiers_.find(identifier);
        if (it != identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

class SymbolCollector {
private:
    Scope& current_scope_;
    MemberAccessHandler& sema_;

public:
    SymbolCollector(Scope& scope, MemberAccessHandler& sema) noexcept
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
        if (current_scope_.self_type_ == nullptr ||
            (node->parameters.size() ? node->parameters[0].identifier != "self" : true)) {
            node->is_static = true;
        }
        if (node->is_static && current_scope_.parent_ == nullptr && node->identifier == "main") {
            node->is_main = true;
        }
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
        class_scope.self_type_ = reinterpret_cast<const Type*>(1);
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
        class_scope.self_type_ = nullptr;
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
