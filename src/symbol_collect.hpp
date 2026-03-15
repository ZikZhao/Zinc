#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"

inline constexpr std::string_view constructor_symbol = "!";
inline constexpr std::string_view destructor_symbol = "~";

using FunctionOverloadDef = PointerVariant<
    const ASTFunctionDefinition*,
    const ASTConstructorDestructorDefinition*,
    const ASTTemplateDefinition*>;

struct TemplateFamily : public GlobalMemory::MonotonicAllocated {
    Scope* decl_scope;
    Scope* pattern_scope;  // used for template specialization pattern and skolemization
    const ASTTemplateDefinition* primary;  // meta function if decl_scope is nullptr
    GlobalMemory::Vector<const ASTTemplateSpecialization*> specializations;
};

struct VariableInitialization : public GlobalMemory::MonotonicAllocated {
    bool is_comptime;
    ASTExprVariant type;
    ASTExprVariant value;
};

using ScopeValue = PointerVariant<
    Term*,                                       // type/comptime (from template)
    const ASTExprVariant*,                       // type alias
    const ASTClassDefinition*,                   // class definition
    const VariableInitialization*,               // comptime/variable declaration
    GlobalMemory::Vector<FunctionOverloadDef>*,  // function overloads
    TemplateFamily*,                             // template definition
    Scope*>;                                     // namespace

class Scope final : public GlobalMemory::MonotonicAllocated {
    friend class Sema;

public:
    static auto root(Scope& std_scope) -> Scope& {
        auto* scope = new Scope(nullptr, nullptr);
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static auto make(
        Scope& parent, const ASTNode* origin = nullptr, const std::string_view* scope_id = nullptr
    ) -> Scope& {
        auto* scope = new Scope(&parent, scope_id);
        parent.children_.insert({origin, scope});
        return *scope;
    }

private:
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;

public:
    Scope* const parent_ = nullptr;
    const std::string_view* scope_id_ = nullptr;
    const Type* self_type_ = nullptr;
    bool in_constructor_ = false;
    bool is_extern_ = false;

private:
    Scope(Scope* parent, const std::string_view* scope_id) noexcept
        : parent_(parent), scope_id_(scope_id) {
        if (parent) {
            is_extern_ = parent->is_extern_;
        }
    }

public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope(Scope&&) = delete;
    auto operator=(const Scope&) -> Scope& = delete;
    auto operator=(Scope&&) -> Scope& = delete;
    ~Scope() noexcept = default;

    void add_template_argument(std::string_view identifier, Term term) noexcept {
        assert(!identifiers_.contains(identifier));
        identifiers_.insert({identifier, new Term(term)});
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

    void add_function(std::string_view identifier, const auto* func) {
        if (auto it = identifiers_.find(identifier); it == identifiers_.end()) {
            auto overloads = GlobalMemory::alloc<GlobalMemory::Vector<FunctionOverloadDef>>();
            overloads->push_back(func);
            identifiers_[identifier] = overloads;
        } else {
            auto overloads = it->second.get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            if (!overloads) {
                throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
            }
            overloads->push_back(func);
        }
    }

    void add_template(std::string_view identifier, const ASTTemplateDefinition* definition) {
        auto [_, inserted] = identifiers_.insert(
            {identifier,
             new TemplateFamily{
                 .decl_scope = this,
                 .pattern_scope = &Scope::make(*this),
                 .primary = definition,
             }}
        );
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
        assert(family->decl_scope != nullptr);
        family->specializations.push_back(specialization);
    }

    void add_meta(std::string_view identifier, MetaFunction func) noexcept {
        auto [_, inserted] = identifiers_.insert(
            {identifier,
             new TemplateFamily{
                 .decl_scope = nullptr,
                 .pattern_scope = nullptr,
                 .primary = std::bit_cast<const ASTTemplateDefinition*>(func),
             }}
        );
        assert(inserted);
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

    void clear() noexcept { identifiers_.clear(); }
};

using OperatorRegistry = GlobalMemory::UnorderedMultiMap<OperatorCode, FunctionOverloadDef>;

class SymbolCollector {
private:
    Scope& current_scope_;
    OperatorRegistry& operators_;

public:
    SymbolCollector(Scope& scope, OperatorRegistry& operators) noexcept
        : current_scope_(scope), operators_(operators) {}

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
        SymbolCollector local_visitor(local_scope, operators_);
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
                new VariableInitialization{
                    .is_comptime = node->is_constant,
                    .type = node->declared_type,
                    .value = node->expr
                }
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
        SymbolCollector condition_visitor(condition_scope, operators_);
        condition_visitor(node->condition);
        condition_visitor(&node->if_block);
        if (node->else_block) {
            condition_visitor(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollector local_visitor(local_scope, operators_);
        if (node->initializer_decl) {
            local_visitor(node->initializer_decl);
        } else if (!std::holds_alternative<std::monostate>(node->initializer_expr)) {
            local_visitor(node->initializer_expr);
        }
        /// TODO: refactor to local scope
        local_visitor(&node->body);
    }

    // Functions
    void operator()(const ASTFunctionDefinition* node) noexcept {
        current_scope_.add_function(node->identifier, node);
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{
                    .is_comptime = false, .type = param.type, .value = std::monostate{}
                }
            );
        }
        SymbolCollector local_visitor(local_scope, operators_);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTConstructorDestructorDefinition* node) noexcept {
        current_scope_.add_function(
            node->is_constructor ? constructor_symbol : destructor_symbol, node
        );
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{
                    .is_comptime = false, .type = param.type, .value = std::monostate{}
                }
            );
        }
        SymbolCollector local_visitor(local_scope, operators_);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    // Classes
    void operator()(const ASTClassDefinition* node) noexcept {
        current_scope_.add_class(node->identifier, node);
        Scope& class_scope = Scope::make(current_scope_, node, &node->identifier);
        SymbolCollector class_visitor(class_scope, operators_);
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
        SymbolCollector namespace_visitor(namespace_scope, operators_);
        for (auto& item : node->items) {
            namespace_visitor(item);
        }
        current_scope_.add_namespace(node->identifier, namespace_scope);
    }

    // Templates
    void operator()(const ASTTemplateDefinition* node) noexcept {
        current_scope_.add_template(node->identifier, node);
    }

    void operator()(const ASTTemplateSpecialization* node) noexcept {
        current_scope_.add_template(node->identifier, node);
    }
};
