#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "meta.hpp"
#include "object.hpp"

inline constexpr strview constructor_symbol = "!";
inline constexpr strview destructor_symbol = "~";

struct TypeProvider final : public GlobalMemory::MonotonicAllocated {
    enum class Kind {
        Alias,
        Class,
        Enum,
    } kind;
    const ASTNode* node;
};

struct VariableInitialization : public GlobalMemory::MonotonicAllocated {
    bool is_comptime;
    bool is_mutable;
    ASTExprVariant type;
    ASTExprVariant value;
};

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

using ScopeValue = PointerVariant<
    const Object*,                               // type/comptime (from template)
    std::span<const Object*>*,                   // parameter pack (from template)
    TypeProvider*,                               // type provider
    const VariableInitialization*,               // comptime/variable declaration
    GlobalMemory::Vector<FunctionOverloadDef>*,  // function overloads
    TemplateFamily*,                             // template definition
    Scope*>;                                     // namespace

class Scope final : public GlobalMemory::MonotonicAllocated {
    friend class Sema;
    friend class CodeGen;

public:
    static auto root(Scope& std_scope) noexcept -> Scope& {
        auto* scope = new Scope(nullptr, {});
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static auto make(Scope& parent, const ASTNode* origin = nullptr, strview scope_id = {}) noexcept
        -> Scope& {
        auto* scope = new Scope(&parent, scope_id);
        parent.children_.insert({origin, scope});
        return *scope;
    }

private:
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<strview, ScopeValue> identifiers_;

public:
    Scope* parent_ = nullptr;
    strview scope_id_;
    strview self_id_;  // only set for class scopes
    bool is_extern_ = false;
    bool is_instantiating_template_ = false;

private:
    Scope(Scope* parent, strview scope_id) noexcept : parent_(parent), scope_id_(scope_id) {
        if (parent) {
            is_extern_ = parent->is_extern_;
        }
    }

public:
    Scope() noexcept = default;

    void set_template_argument(strview identifier, const Object* obj) noexcept {
        identifiers_[identifier] = obj;
    }

    void set_template_pack(strview identifier, std::span<const Object*> terms) noexcept {
        std::span<const Object*>* ptr = GlobalMemory::alloc<std::span<const Object*>>(terms);
        identifiers_[identifier] = ptr;
    }

    template <typename T>
    void add_type(strview identifier, const T* node) noexcept {
        TypeProvider::Kind kind;
        if constexpr (std::is_same_v<T, ASTTypeAlias>) {
            kind = TypeProvider::Kind::Alias;
        } else if constexpr (std::is_same_v<T, ASTClassDefinition>) {
            kind = TypeProvider::Kind::Class;
        } else {
            /// TODO: support enum definitions as types
            static_assert(false);
        }
        auto [_, inserted] =
            identifiers_.insert({identifier, new TypeProvider{.kind = kind, .node = node}});
        if (!inserted) {
            Diagnostic::error_redeclared_identifier(identifier);
        }
    }

    void add_variable(strview identifier, const VariableInitialization* init) noexcept {
        auto [_, inserted] = identifiers_.insert({identifier, init});
        if (!inserted) {
            Diagnostic::error_redeclared_identifier(identifier);
        }
    }

    void add_function(strview identifier, const auto* func) noexcept {
        if (auto it = identifiers_.find(identifier); it == identifiers_.end()) {
            auto overloads = GlobalMemory::alloc<GlobalMemory::Vector<FunctionOverloadDef>>();
            overloads->push_back(func);
            identifiers_[identifier] = overloads;
        } else {
            auto overloads = it->second.get<GlobalMemory::Vector<FunctionOverloadDef>*>();
            if (!overloads) {
                Diagnostic::error_redeclared_identifier(identifier);
                return;
            }
            overloads->push_back(func);
        }
    }

    void add_template(strview identifier, const ASTTemplateDefinition* definition) noexcept {
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
            Diagnostic::error_redeclared_identifier(identifier);
        }
    }

    void add_template(
        strview identifier, const ASTTemplateSpecialization* specialization
    ) noexcept {
        auto it = identifiers_.find(identifier);
        if (it == identifiers_.end()) {
            Diagnostic::error_undeclared_identifier(identifier);
            return;
        }
        auto family = it->second.get<TemplateFamily*>();
        if (!family) {
            Diagnostic::error_redeclared_identifier(identifier);
            return;
        }
        assert(family->decl_scope != nullptr);
        family->specializations.push_back(specialization);
    }

    void add_meta(strview identifier, MetaFunction func) noexcept {
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

    void add_namespace(strview identifier, Scope& scope) noexcept {
        auto [_, inserted] = identifiers_.insert({identifier, &scope});
        if (!inserted) {
            Diagnostic::error_redeclared_identifier(identifier);
        }
    }

    auto find(strview identifier) const noexcept -> const ScopeValue* {
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
    Scope* std_scope_;
    Scope* current_scope_;

public:
    SymbolCollector(Scope* std_scope, Scope* current_scope) noexcept
        : std_scope_(std_scope), current_scope_(current_scope) {}

    void operator()(const ASTNodeVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(const ASTExprVariant& variant) noexcept { std::visit(*this, variant); }

    void operator()(std::monostate) noexcept { UNREACHABLE(); }

    template <typename T>
    void operator()(const T*) noexcept
        requires(!std::is_pointer_v<T>)
    {}

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        if (!node->scope) {
            if (std_scope_) {
                // module root
                node->scope = &Scope::root(*std_scope_);
            } else {
                // standard library root
                node->scope = current_scope_;
            }
        }
        SymbolCollector root_visitor{std_scope_, node->scope};
        for (auto& child : node->statements) {
            root_visitor(child);
        }
    }

    void operator()(const ASTLocalBlock* node) noexcept {
        Scope& local_scope = Scope::make(*current_scope_, node);
        SymbolCollector local_visitor(std_scope_, &local_scope);
        for (auto& stmt : node->statements) {
            local_visitor(stmt);
        }
    }

    // Struct initialization
    void operator()(const ASTStructInitialization* node) noexcept {
        GlobalMemory::FlatSet<strview> fields;
        for (const auto& init : node->field_inits) {
            auto [_, inserted] = fields.insert(init.identifier);
            if (!inserted) {
                Diagnostic::error_duplicate_attribute(init.location, init.identifier);
                auto prev = std::ranges::find(
                    node->field_inits, init.identifier, &ASTFieldInitialization::identifier
                );
                Diagnostic::error_decl_of_duplicate_attribute(prev->location);
                return;
            }
        }
    }

    // Declarations
    void operator()(const ASTDeclaration* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_variable(
            node->identifier,
            new VariableInitialization{
                .is_comptime = node->is_constant,
                .is_mutable = node->is_mutable,
                .type = node->declared_type,
                .value = node->expr
            }
        );
    }

    void operator()(const ASTTypeAlias* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_type(node->identifier, node);
    }

    // Statements
    void operator()(const ASTIfStatement* node) noexcept {
        Scope& condition_scope = Scope::make(*current_scope_, node);
        SymbolCollector condition_visitor(std_scope_, &condition_scope);
        condition_visitor(node->condition);
        condition_visitor(node->if_block);
        if (!holds_monostate(node->else_block)) {
            condition_visitor(node->else_block);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        Scope& local_scope = Scope::make(*current_scope_, node);
        SymbolCollector local_visitor(std_scope_, &local_scope);
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            local_visitor(*decl);
        } else if (
            auto* expr = std::get_if<ASTExprVariant>(&node->initializer);
            expr && !holds_monostate(*expr)
        ) {
            local_visitor(*expr);
        }
        local_visitor(node->body);
    }

    // Functions
    void operator()(const ASTFunctionDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_function(node->identifier, node);
        Scope& local_scope = Scope::make(*current_scope_, node);
        for (auto& param : node->parameters) {
            Diagnostic::ErrorTrap param_trap{param.location};
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
        SymbolCollector local_visitor(std_scope_, &local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTCtorDtorDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_function(
            node->is_constructor ? constructor_symbol : destructor_symbol, node
        );
        Scope& local_scope = Scope::make(*current_scope_, node);
        for (auto& param : node->parameters) {
            Diagnostic::ErrorTrap param_trap{param.location};
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
        SymbolCollector local_visitor(std_scope_, &local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    void operator()(const ASTOperatorDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_function(GetOperatorString(node->opcode), node);
        Scope& local_scope = Scope::make(*current_scope_, node);
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
            Diagnostic::ErrorTrap right_trap{node->right->location};
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
        SymbolCollector local_visitor(std_scope_, &local_scope);
        for (auto& stmt : node->body) {
            local_visitor(stmt);
        }
    }

    // Classes
    void operator()(const ASTClassDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_type(node->identifier, node);
        Scope& class_scope = Scope::make(*current_scope_, node, node->identifier);
        class_scope.self_id_ = node->identifier;
        SymbolCollector class_visitor(std_scope_, &class_scope);
        for (auto& item : node->scope_items) {
            class_visitor(item);
        }
        for (ASTNodeVariant decl_variant : node->fields) {
            const ASTDeclaration* decl = std::get<const ASTDeclaration*>(decl_variant);
            TypeResolution field_type;
            if (holds_monostate(decl->declared_type)) {
                throw;
            }
        }
    }

    // Enums
    void operator()(const ASTEnumDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        Scope& enum_scope = Scope::make(*current_scope_, node, node->identifier);
        current_scope_->add_namespace(node->identifier, enum_scope);
        SymbolCollector enum_visitor(std_scope_, &enum_scope);
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
        Diagnostic::ErrorTrap trap{node->location};
        Scope& namespace_scope = Scope::make(*current_scope_, node, node->identifier);
        SymbolCollector namespace_visitor{std_scope_, &namespace_scope};
        for (auto& item : node->items) {
            namespace_visitor(item);
        }
        current_scope_->add_namespace(node->identifier, namespace_scope);
    }

    // Templates
    void operator()(const ASTTemplateDefinition* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_template(node->identifier, node);
    }

    void operator()(const ASTTemplateSpecialization* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        current_scope_->add_template(node->identifier, node);
    }

    void operator()(const ASTImportStatement* node) noexcept {
        Diagnostic::ErrorTrap trap{node->location};
        if (node->module_root->scope == nullptr) {
            SymbolCollector module_visitor{std_scope_, nullptr};
            module_visitor(node->module_root);
        }
        current_scope_->add_namespace(node->alias, *node->module_root->scope);
    }
};
