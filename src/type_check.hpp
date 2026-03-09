#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"
#include "symbol_collect.hpp"

class TypeChecker final {
public:
    class Guard final {
    private:
        TypeChecker& checker_;
        Scope* prev_;

    public:
        Guard(TypeChecker& checker, Scope* scope) noexcept
            : checker_(checker), prev_(std::exchange(checker.current_scope_, scope)) {}
        Guard(TypeChecker& checker, const void* child) noexcept
            : checker_(checker),
              prev_(
                  std::exchange(checker.current_scope_, checker.current_scope_->children_.at(child))
              ) {}
        Guard(const Guard&) = delete;
        Guard(Guard&&) = delete;
        auto operator=(const Guard&) = delete;
        auto operator=(Guard&&) = delete;
        ~Guard() noexcept { checker_.current_scope_ = prev_; }
    };

private:
    Scope* current_scope_;
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, TypeResolution> type_cache_;

public:
    MemberAccessHandler& sema_;

public:
    TypeChecker(Scope& root, MemberAccessHandler& sema) noexcept
        : current_scope_(&root), sema_(sema) {}

    auto current_scope() noexcept -> Scope* { return current_scope_; }

    auto is_at_top_level() const noexcept -> bool { return current_scope_->parent_ == nullptr; }

    auto self_type() const noexcept -> const Type* {
        auto current = current_scope_;
        do {
            if (current->self_type_) {
                return current->self_type_;
            }
            current = current->parent_;
        } while (current);
        return nullptr;
    }

    auto operator[](std::string_view identifier) const noexcept -> const ScopeValue* {
        auto it = current_scope_->identifiers_.find(identifier);
        if (it != current_scope_->identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    auto get_func_obj(const ASTFunctionDefinition* node) -> FunctionObject;

    auto get_func_obj(const ASTConstructorDestructorDefinition* node, const Type* class_type)
        -> FunctionObject;

    auto lookup(std::string_view identifier) const noexcept
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

    /// TODO: injected class name by template
    auto lookup_type(std::string_view identifier) -> TypeResolution;

    auto lookup_term(std::string_view identifier) -> Term;

    auto lookup_instantiation(std::string_view identifier, std::span<Term> args) -> Term;

private:
    auto validate_instantiation(const ASTTemplateDefinition& primary, std::span<Term> args) -> bool;

    auto specialization_resolution(const TemplateFamily& family, std::span<Term> args) -> Scope&;
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

    void operator()(const ASTHiddenTypeExpr* node) { UNREACHABLE(); }

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
            const Type* self_type = checker_.self_type();
            if (!self_type) {
                /// TODO: throw not in class error
            }
            out_ = self_type;
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

    template <typename ASTUnaryOpType>
        requires requires {
            ASTUnaryOpType::opcode;
            std::declval<ASTUnaryOpType>().expr;
        }
    void operator()(const ASTUnaryOpType* node) {
        TypeResolution expr_result;
        TypeContextEvaluator{checker_, expr_result, false}(node->expr);
        try {
            out_ = TypeResolution(checker_.sema_.eval_type_op(ASTUnaryOpType::opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location);
            out_ = TypeRegistry::get_unknown();
        }
    }

    template <typename ASTBinaryOpType>
        requires requires {
            ASTBinaryOpType::opcode;
            std::declval<ASTBinaryOpType>().left;
            std::declval<ASTBinaryOpType>().right;
        }
    void operator()(const ASTBinaryOpType* node) {
        TypeResolution left_result;
        TypeContextEvaluator{checker_, left_result}(node->left);
        TypeResolution right_result;
        TypeContextEvaluator{checker_, right_result}(node->right);
        try {
            out_ = checker_.sema_.eval_type_op(ASTBinaryOpType::opcode, left_result, right_result);
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
        auto* incomplete_class = new InstanceType(node->identifier);
        out_ = incomplete_class;
        TypeChecker::Guard guard(checker_, node);
        assert(checker_.current_scope()->self_type_ == nullptr);
        checker_.current_scope()->self_type_ = incomplete_class;
        const Type* base = resolve_base(node);
        std::span<const Type*> interfaces = resolve_interfaces(node);
        FunctionOverloadSetValue* constructors = resolve_constructors(node, out_);
        FunctionObject destructor = resolve_destructor(node);
        GlobalMemory::FlatMap<std::string_view, const Type*> attrs = resolve_attrs(node);
        GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> methods =
            resolve_methods(node);
        TypeRegistry::get_at<InstanceType>(
            out_,
            checker_.current_scope(),
            node->identifier,
            base,
            interfaces,
            constructors,
            destructor,
            std::move(attrs),
            std::move(methods)
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
        -> FunctionOverloadSetValue* {
        return new FunctionOverloadSetValue(
            node->constructors | std::views::transform([&](const auto& ctor) {
                return checker_.get_func_obj(ctor, class_type);
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>()
        );
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
            return {.subject = Term::type(checker_.self_type()), .receiver = {}};
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

    template <typename ASTUnaryOpType>
        requires requires {
            ASTUnaryOpType::opcode;
            std::declval<ASTUnaryOpType>().expr;
        }
    auto operator()(const ASTUnaryOpType* node) -> TermWithReceiver {
        Term expr_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->expr).subject;
        return {
            .subject = checker_.sema_.eval_value_op(ASTUnaryOpType::opcode, expr_term),
            .receiver = {}
        };
    }

    template <typename ASTBinaryOpType>
        requires requires {
            ASTBinaryOpType::opcode;
            std::declval<ASTBinaryOpType>().left;
            std::declval<ASTBinaryOpType>().right;
        }
    auto operator()(const ASTBinaryOpType* node) -> TermWithReceiver {
        Term left_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->left).subject;
        Term right_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->right).subject;
        try {
            return {
                .subject =
                    checker_.sema_.eval_value_op(ASTBinaryOpType::opcode, left_term, right_term),
                .receiver = {}
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
        // check duplications in advance
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
        GlobalMemory::FlatMap init_types =
            inits |
            std::views::transform([](const auto& init) -> std::pair<std::string_view, const Type*> {
                return {init.first, init.second.effective_type()};
            }) |
            GlobalMemory::collect<GlobalMemory::FlatMap>();
        try {
            TypeResolution struct_type;
            TypeContextEvaluator{checker_, struct_type}(node->struct_type);
            struct_type->cast<StructType>()->validate(init_types);
            return {.subject = Term::prvalue(struct_type.get()), .receiver = {}};
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
        if (func.is_type()) {
            Term instance = Term::lvalue(TypeRegistry::get<MutableType>(func.get_type()));
            args_terms.insert(args_terms.begin(), instance);
        } else if (receiver) {
            args_terms.insert(args_terms.begin(), receiver);
        }
        if (any_error) {
            return {.subject = Term::unknown(), .receiver = {}};
        }
        try {
            return {.subject = checker_.sema_.eval_call(func, args_terms), .receiver = {}};
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
            auto next = (*scope_ptr)[member];
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
                current_term = checker_.sema_.eval_access(current_term, member);
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location);
                return {.subject = Term::unknown(), .receiver = {}};
            }
        }
        return {.subject = current_term, .receiver = receiver};
    }

    // auto eval_struct_initialization(const ASTStructInitialization* node) -> Value* {
    //     GlobalMemory::Vector<std::pair<std::string_view, Value*>> inits =
    //         node->field_inits | std::views::transform([&](const ASTFieldInitialization& init) {
    //             Term value_term =
    //                 ValueContextEvaluator{checker_, nullptr,
    //                 require_comptime_}(init.value).subject;
    //             if (!value_term.is_comptime()) {
    //                 Diagnostic::report(NotConstantExpressionError(init.location));
    //                 return std::pair<std::string_view, Value*>{
    //                     init.identifier, &UnknownValue::instance
    //                 };
    //             }
    //             return std::pair<std::string_view, Value*>{
    //                 init.identifier, value_term.get_comptime()
    //             };
    //         }) |
    //         GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, Value*>>>();
    //     GlobalMemory::Vector<std::pair<std::string_view, const Type*>> types =
    //         inits | std::views::transform([&](const auto& init) {
    //             return std::pair<std::string_view, const Type*>{
    //                 init.first, init.second->get_type()
    //             };
    //         }) |
    //         GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const
    //         Type*>>>();
    //     // try {
    //     //     // struct_type->validate(types);
    //     // } catch (UnlocatedProblem& e) {
    //     //     e.report_at(node->location);
    //     //     return &UnknownValue::instance;
    //     // }
    //     // return new StructValue(
    //     //     struct_type,
    //     //     inits | GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>()
    //     // );
    // }
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
        for (auto& stmt : node->body) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        checker_.lookup_type(node->identifier);  // trigger self type injection
        TypeChecker::Guard guard(checker_, node);
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

inline auto TypeChecker::get_func_obj(const ASTFunctionDefinition* node) -> FunctionObject {
    bool any_error = false;
    std::span params = node->parameters |
                       std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                           TypeResolution param_type;
                           TypeContextEvaluator{*this, param_type}(param.type);
                           return param_type;
                       }) |
                       GlobalMemory::collect<std::span<const Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    TypeResolution return_type;
    TypeContextEvaluator{*this, return_type}(node->return_type);
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline auto TypeChecker::get_func_obj(
    const ASTConstructorDestructorDefinition* node, const Type* class_type
) -> FunctionObject {
    bool any_error = false;
    std::span params = node->parameters |
                       std::views::transform([&](const ASTFunctionParameter& param) -> const Type* {
                           TypeResolution param_type;
                           TypeContextEvaluator{*this, param_type}(param.type);
                           return param_type;
                       }) |
                       GlobalMemory::collect<std::span<const Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    return TypeRegistry::get<FunctionType>(params, class_type);
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
    } else if (auto func = value->get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
        Guard guard(*this, scope);
        GlobalMemory::Vector<FunctionObject> func_vec =
            *func | std::views::transform([&](const FunctionOverloadDecl& func_decl) {
                if (auto func_def = func_decl.get<const ASTFunctionDefinition*>()) {
                    return get_func_obj(func_def);
                } else {
                    /// TODO: handle template functions
                    return FunctionObject{};
                }
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>();
        return Term::prvalue(new FunctionOverloadSetValue(func_vec));
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

inline auto TypeChecker::validate_instantiation(
    const ASTTemplateDefinition& primary, std::span<Term> args
) -> bool {
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
                    ValueContextEvaluator{*this, nullptr, false}(param.constraint).subject;
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

inline auto TypeChecker::specialization_resolution(
    const TemplateFamily& family, std::span<Term> args
) -> Scope& {
    for (const auto& specialization : family.specializations) {
        /// TODO:
    }
    Scope& inst_scope = Scope::make_unlinked(*current_scope_, nullptr);
    Guard guard(*this, &inst_scope);
    for (size_t i = 0; i < args.size(); i++) {
        inst_scope.add_template_argument(family.primary.parameters[i].identifier, args[i]);
    }
    return inst_scope;
}
