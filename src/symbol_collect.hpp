#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"

inline constexpr std::string_view constructor_symbol = "!";
inline constexpr std::string_view destructor_symbol = "~";

using FunctionOverloadDef = PointerVariant<
    const ASTFunctionDefinition*,
    const ASTCtorDtorDefinition*,
    const ASTOperatorDefinition*,
    const ASTTemplateDefinition*>;

struct TemplateFamily : public GlobalMemory::MonotonicAllocated {
    Scope* decl_scope;
    Scope* pattern_scope;  // used for template specialization pattern and skolemization
    const ASTTemplateDefinition* primary;  // meta function if decl_scope is nullptr
    GlobalMemory::Vector<const ASTTemplateSpecialization*> specializations;
};

struct VariableInitialization : public GlobalMemory::MonotonicAllocated {
    bool is_comptime;
    bool is_mutable;
    ASTExprVariant type;
    ASTExprVariant value;
};

using ScopeValue = PointerVariant<
    const Object*,                               // type/comptime (from template)
    std::span<const Object*>*,                   // parameter pack (from template)
    const ASTExprVariant*,                       // type alias
    const ASTClassDefinition*,                   // class definition
    const VariableInitialization*,               // comptime/variable declaration
    GlobalMemory::Vector<FunctionOverloadDef>*,  // function overloads
    TemplateFamily*,                             // template definition
    Scope*>;                                     // namespace

class Scope final : public GlobalMemory::MonotonicAllocated {
    friend class Sema;
    friend class CodeGen;

public:
    static auto root(Scope& std_scope) -> Scope& {
        auto* scope = new Scope(nullptr, {});
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static auto make(Scope& parent, const ASTNode* origin = nullptr, std::string_view scope_id = {})
        -> Scope& {
        auto* scope = new Scope(&parent, scope_id);
        parent.children_.insert({origin, scope});
        return *scope;
    }

private:
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;

public:
    Scope* const parent_ = nullptr;
    std::string_view scope_id_;
    const Type* self_type_ = nullptr;
    bool in_constructor_ = false;
    bool is_extern_ = false;

private:
    Scope(Scope* parent, std::string_view scope_id) noexcept
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

    void set_template_argument(std::string_view identifier, const Object* term) noexcept {
        identifiers_[identifier] = term;
    }

    void set_template_pack(std::string_view identifier, std::span<const Object*> terms) noexcept {
        std::span<const Object*>* ptr = GlobalMemory::alloc<std::span<const Object*>>(terms);
        identifiers_[identifier] = ptr;
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
        ASTNodeVariant target = definition->target_node;
        if (std::holds_alternative<const ASTFunctionDefinition*>(target) ||
            std::holds_alternative<const ASTCtorDtorDefinition*>(target) ||
            std::holds_alternative<const ASTOperatorDefinition*>(target)) {
            add_function(identifier, definition);
            return;
        }
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

class SymbolCollector {
private:
    Scope& current_scope_;

public:
    SymbolCollector(Scope& scope) noexcept : current_scope_(scope) {}

    void operator()(const ASTNodeVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(const ASTExprVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(std::monostate) noexcept { UNREACHABLE(); }

    template <typename T>
    void operator()(const T*) noexcept
        requires(!std::is_pointer_v<T>)
    {}

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        for (auto& child : node->statements) {
            (*this)(child);
        }
    }

    void operator()(const ASTLocalBlock* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollector local_visitor(local_scope);
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
                    .is_mutable = node->is_mutable,
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
        SymbolCollector condition_visitor(condition_scope);
        condition_visitor(node->condition);
        condition_visitor(node->if_block);
        if (!holds_monostate(node->else_block)) {
            condition_visitor(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollector local_visitor(local_scope);
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            local_visitor(*decl);
        } else if (
            auto* expr = std::get_if<ASTExprVariant>(&node->initializer);
            expr && !holds_monostate(*expr)
        ) {
            local_visitor(*expr);
        }
        /// TODO: refactor to local scope
        local_visitor(node->body);
    }

    // Functions
    void operator()(const ASTFunctionDefinition* node) noexcept {
        current_scope_.add_function(node->identifier, node);
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{
                    .is_comptime = false,
                    .is_mutable = param.is_mutable,
                    .type = param.type,
                    .value = std::monostate{}
                }
            );
        }
        SymbolCollector local_visitor(local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTCtorDtorDefinition* node) noexcept {
        current_scope_.add_function(
            node->is_constructor ? constructor_symbol : destructor_symbol, node
        );
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters) {
            local_scope.add_variable(
                param.identifier,
                new VariableInitialization{
                    .is_comptime = false,
                    .is_mutable = param.is_mutable,
                    .type = param.type,
                    .value = std::monostate{}
                }
            );
        }
        SymbolCollector local_visitor(local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTOperatorDefinition* node) noexcept {
        current_scope_.add_function(GetOperatorString(node->opcode), node);
        Scope& local_scope = Scope::make(current_scope_, node);
        local_scope.add_variable(
            node->left.identifier,
            new VariableInitialization{
                .is_comptime = node->declared_const,
                .is_mutable = node->left.is_mutable,
                .type = node->left.type,
                .value = std::monostate{}
            }
        );
        if (node->right) {
            local_scope.add_variable(
                node->right->identifier,
                new VariableInitialization{
                    .is_comptime = node->declared_const,
                    .is_mutable = node->right->is_mutable,
                    .type = node->right->type,
                    .value = std::monostate{}
                }
            );
        }
        SymbolCollector local_visitor(local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    // Classes
    void operator()(const ASTClassDefinition* node) noexcept {
        current_scope_.add_class(node->identifier, node);
        Scope& class_scope = Scope::make(current_scope_, node, node->identifier);
        SymbolCollector class_visitor(class_scope);
        for (auto& item : node->scope_items) {
            class_visitor(item);
        }
    }

    // Enums
    void operator()(const ASTEnumDefinition* node) noexcept {
        Scope& enum_scope = Scope::make(current_scope_, node, node->identifier);
        SymbolCollector enum_visitor(enum_scope);
        for (size_t i = 0; i < node->enumerators.size(); ++i) {
            enum_scope.add_variable(
                node->enumerators[i],
                new VariableInitialization{
                    .is_comptime = true,
                    .is_mutable = false,
                    .type = std::monostate{},
                    .value = ASTExprVariant{new ASTConstant{
                        Location{}, new IntegerValue(&IntegerType::untyped_instance, i)
                    }},
                }
            );
        }
    }

    // Namespaces
    void operator()(const ASTNamespaceDefinition* node) noexcept {
        Scope& namespace_scope = Scope::make(current_scope_, node, node->identifier);
        SymbolCollector namespace_visitor(namespace_scope);
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

    void operator()(const ASTImportStatement* node) noexcept {
        if (!node->module_scope) {
            node->module_scope = &Scope::make(current_scope_, node, node->alias);
        }
        current_scope_.add_namespace(node->alias, *node->module_scope);
    }
};
