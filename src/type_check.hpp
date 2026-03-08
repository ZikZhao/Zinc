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

    Scope* current_scope() noexcept { return current_scope_; }

    bool is_at_top_level() const noexcept { return current_scope_->parent_ == nullptr; }

    const Type* self_type() const noexcept {
        auto current = current_scope_;
        do {
            if (current->self_type_) {
                return current->self_type_;
            }
            current = current->parent_;
        } while (current);
        return nullptr;
    }

    const ScopeValue* operator[](std::string_view identifier) const noexcept {
        auto it = current_scope_->identifiers_.find(identifier);
        if (it != current_scope_->identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::pair<Scope*, const ScopeValue*> lookup(std::string_view identifier) const noexcept {
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
    TypeResolution lookup_type(std::string_view identifier);

    Term lookup_term(std::string_view identifier);

    Term lookup_instantiation(std::string_view identifier, std::span<Term> args);

private:
    bool validate_instantiation(const ASTTemplateDefinition& primary, std::span<Term> args);

    Scope& specialization_resolution(const TemplateFamily& family, std::span<Term> args);
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

    void operator()(const ASTParenExpr* node) { (*this)(node->inner_); }

    void operator()(const ASTConstant* node) {
        /// TODO: literal types
        out_ = {};
    }

    void operator()(const ASTSelfExpr* node) {
        if (!node->is_type_) {
            Diagnostic::report(SymbolCategoryMismatchError(node->location_, false));
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
        TypeResolution result = checker_.lookup_type(node->str_);
        if (!result.is_sized()) {
            if (require_complete_) {
                Diagnostic::report(CircularTypeDependencyError(node->location_));
                out_ = TypeRegistry::get_unknown();
                return;
            }
        }
        out_ = result;
    }

    template <typename ASTUnaryOpType>
        requires requires {
            ASTUnaryOpType::opcode;
            std::declval<ASTUnaryOpType>().expr_;
        }
    void operator()(const ASTUnaryOpType* node) {
        TypeResolution expr_result;
        TypeContextEvaluator{checker_, expr_result, false}(node->expr_);
        try {
            out_ = TypeResolution(checker_.sema_.eval_type_op(ASTUnaryOpType::opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            out_ = TypeRegistry::get_unknown();
        }
    }

    template <typename ASTBinaryOpType>
        requires requires {
            ASTBinaryOpType::opcode;
            std::declval<ASTBinaryOpType>().left_;
            std::declval<ASTBinaryOpType>().right_;
        }
    void operator()(const ASTBinaryOpType* node) {
        TypeResolution left_result;
        TypeContextEvaluator{checker_, left_result}(node->left_);
        TypeResolution right_result;
        TypeContextEvaluator{checker_, right_result}(node->right_);
        try {
            out_ = checker_.sema_.eval_type_op(ASTBinaryOpType::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTMemberAccess* node) {
        /// TODO:
    }

    void operator()(const ASTStructInitialization* node) {
        Diagnostic::report(SymbolCategoryMismatchError(node->location_, true));
        out_ = TypeRegistry::get_unknown();
    }

    void operator()(const ASTFunctionCall* node) {
        Diagnostic::report(SymbolCategoryMismatchError(node->location_, true));
        out_ = TypeRegistry::get_unknown();
    }

    void operator()(const ASTPrimitiveType* node) { out_ = node->type_; }

    void operator()(const ASTFunctionType* node) {
        out_ = std::type_identity<FunctionType>();
        bool any_error = false;
        std::span<const Type*> param_types =
            node->parameter_types_ |
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
        TypeContextEvaluator{checker_, return_type}(node->return_type_);
        TypeRegistry::get_at<FunctionType>(out_, param_types, return_type);
    }

    void operator()(const ASTStructType* node) {
        out_ = std::type_identity<StructType>();
        GlobalMemory::FlatMap<std::string_view, const Type*> field_map =
            node->fields_ |
            std::views::transform(
                [&](const ASTFieldDeclaration& decl) -> std::pair<std::string_view, const Type*> {
                    TypeResolution field_type;
                    TypeContextEvaluator{checker_, field_type}(decl.type_);
                    return {decl.identifier_, field_type};
                }
            ) |
            GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
        try {
            TypeRegistry::get_at<StructType>(out_, field_map);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            out_ = TypeRegistry::get_unknown();
        }
    }

    void operator()(const ASTMutableType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner_);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTReferenceType* node) {
        out_ = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner_);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out_, expr_type, node->is_moved_);
    }

    void operator()(const ASTPointerType* node) {
        out_ = std::type_identity<PointerType>();
        TypeResolution expr_type;
        TypeContextEvaluator{checker_, expr_type, false}(node->inner_);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out_, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out_, expr_type);
    }

    void operator()(const ASTTemplateInstantiation* node) {
        /// TODO:
    }

    void operator()(const auto* node) {
        /// TODO:
    }
};

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

    TermWithReceiver operator()(const ASTExprVariant& variant) {
        if (std::visit(
                [](auto node) -> bool {
                    return std::is_convertible_v<decltype(node), const ASTExplicitTypeExpr*>;
                },
                variant
            )) {
            TypeResolution out;
            TypeContextEvaluator{checker_, out}(variant);
            return TermWithReceiver{Term::type(out), {}};
        }
        return std::visit(*this, variant);
    }

    TermWithReceiver operator()(std::monostate) { UNREACHABLE(); }

    TermWithReceiver operator()(const ASTExpression* node) { UNREACHABLE(); }

    TermWithReceiver operator()(const ASTExplicitTypeExpr* node) { UNREACHABLE(); }

    TermWithReceiver operator()(const ASTParenExpr* node) { return (*this)(node->inner_); }

    TermWithReceiver operator()(const ASTConstant* node) {
        if (expected_) {
            try {
                Value* typed_value = node->value_->resolve_to(expected_);
                return {Term::prvalue(typed_value), {}};
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location_);
                return {Term::unknown(), {}};
            }
        } else {
            return {Term::prvalue(node->value_->resolve_to(nullptr)), {}};
        }
    }

    TermWithReceiver operator()(const ASTSelfExpr* node) {
        if (node->is_type_) {
            return {Term::type(checker_.self_type()), {}};
        } else {
            return {checker_.lookup_term("self"), {}};
        }
    }

    TermWithReceiver operator()(const ASTIdentifier* node) {
        try {
            Term term = checker_.lookup_term(node->str_);
            if (require_comptime_ && !term.is_comptime()) {
                Diagnostic::report(NotConstantExpressionError(node->location_));
                return {Term::unknown(), {}};
            }
            return {term, {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            return {Term::unknown(), {}};
        }
    }

    template <typename ASTUnaryOpType>
        requires requires {
            ASTUnaryOpType::opcode;
            std::declval<ASTUnaryOpType>().expr_;
        }
    TermWithReceiver operator()(const ASTUnaryOpType* node) {
        Term expr_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->expr_).subject;
        return {checker_.sema_.eval_value_op(ASTUnaryOpType::opcode, expr_term), {}};
    }

    template <typename ASTBinaryOpType>
        requires requires {
            ASTBinaryOpType::opcode;
            std::declval<ASTBinaryOpType>().left_;
            std::declval<ASTBinaryOpType>().right_;
        }
    TermWithReceiver operator()(const ASTBinaryOpType* node) {
        Term left_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->left_).subject;
        Term right_term =
            ValueContextEvaluator{checker_, expected_, require_comptime_}(node->right_).subject;
        try {
            return {
                checker_.sema_.eval_value_op(ASTBinaryOpType::opcode, left_term, right_term), {}
            };
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            return {.subject = Term::unknown(), .receiver = {}};
        }
    }

    TermWithReceiver operator()(const ASTMemberAccess* node) {
        if (auto identifier = std::get_if<ASTIdentifier*>(&node->target_)) {
            return eval_namesapce_access(node, (*identifier)->str_);
        } else {
            Term subject_term =
                ValueContextEvaluator{checker_, expected_, require_comptime_}(node->target_)
                    .subject;
            return eval_instance_access(node, subject_term, node->members_);
        }
    }

    TermWithReceiver operator()(const ASTStructInitialization* node) {
        // GlobalMemory::Vector<std::pair<std::string_view, const Type*>> inits =
        //     field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
        //         std::pair<std::string_view, Term> field = init->eval(checker);
        //         return std::pair<std::string_view, const Type*>{
        //             field.first, field.second.effective_type()
        //         };
        //     }) |
        //     GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const
        //     Type*>>>();
        // try {
        //     struct_type->validate(inits);
        // } catch (UnlocatedProblem& e) {
        //     e.report_at(location_);
        // }
    }

    TermWithReceiver operator()(const ASTFunctionCall* node) {
        bool any_error = false;
        GlobalMemory::Vector<Term> args_terms =
            node->arguments_ | std::views::transform([&](ASTExprVariant arg) {
                Term arg_term =
                    ValueContextEvaluator{checker_, nullptr, require_comptime_}(arg).subject;
                any_error |= arg_term.is_unknown();
                return arg_term;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        auto [func, receiver] =
            ValueContextEvaluator{checker_, nullptr, require_comptime_}(node->function_);
        if (func.is_type()) {
            Term instance = Term::lvalue(TypeRegistry::get<MutableType>(func.get_type()));
            args_terms.insert(args_terms.begin(), instance);
        } else if (receiver) {
            args_terms.insert(args_terms.begin(), receiver);
        }
        if (any_error) {
            return {Term::unknown(), {}};
        }
        try {
            return {checker_.sema_.eval_call(func, args_terms), {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
            return {Term::unknown(), {}};
        }
    }

    TermWithReceiver operator()(const ASTTemplateInstantiation* node) {
        GlobalMemory::Vector<Term> args_terms =
            node->arguments_ | std::views::transform([&](ASTExprVariant arg) {
                return ValueContextEvaluator{checker_, nullptr, require_comptime_}(arg).subject;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        return {checker_.lookup_instantiation(node->template_name_, args_terms), {}};
    }

    TermWithReceiver operator()(const auto* node) { UNREACHABLE(); }

private:
    TermWithReceiver eval_namesapce_access(const ASTMemberAccess* node, std::string_view subject) {
        auto [_, subject_value] = checker_.lookup(subject);
        auto scope_ptr = subject_value->get<const Scope*>();
        if (!scope_ptr) {
            return eval_instance_access(node, checker_.lookup_term(subject), node->members_);
        }
        std::span<std::string_view> members = node->members_;
        while (!members.empty()) {
            std::string_view member = members.front();
            auto next = (*scope_ptr)[member];
            if (!next) {
                throw UnlocatedProblem::make<UndeclaredIdentifierError>(member);
                return {Term::unknown(), {}};
            }
            if (auto next_scope = next->get<const Scope*>()) {
                scope_ptr = next_scope;
                members = members.subspan(1);
            } else if (auto next_term = next->get<Term*>()) {
                if (members.size()) {
                    return eval_instance_access(node, *next_term, members);
                } else {
                    return {*next_term, {}};
                }
            } else {
                UNREACHABLE();
            }
        }
        /// TODO: evaluates to namespace is invalid
        throw;
    }

    TermWithReceiver eval_instance_access(
        const ASTMemberAccess* node, Term current_term, std::span<std::string_view> members
    ) {
        Term subject;
        if (current_term.is_unknown()) {
            return {Term::unknown(), {}};
        }
        for (std::string_view member : members) {
            try {
                subject = current_term;
                current_term = checker_.sema_.eval_access(current_term, member);
            } catch (UnlocatedProblem& e) {
                e.report_at(node->location_);
                return {Term::unknown(), {}};
            }
        }
        return {current_term, subject};
    }

    Value* eval_struct_initialization(const ASTStructInitialization* node) {
        GlobalMemory::Vector<std::pair<std::string_view, Value*>> inits =
            node->field_inits_ | std::views::transform([&](const ASTFieldInitialization& init) {
                Term value_term =
                    ValueContextEvaluator{checker_, nullptr, require_comptime_}(init.value_)
                        .subject;
                if (!value_term.is_comptime()) {
                    Diagnostic::report(NotConstantExpressionError(init.location_));
                    return std::pair<std::string_view, Value*>{
                        init.identifier_, &UnknownValue::instance
                    };
                }
                return std::pair<std::string_view, Value*>{
                    init.identifier_, value_term.get_comptime()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, Value*>>>();
        GlobalMemory::Vector<std::pair<std::string_view, const Type*>> types =
            inits | std::views::transform([&](const auto& init) {
                return std::pair<std::string_view, const Type*>{
                    init.first, init.second->get_type()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const Type*>>>();
        // try {
        //     // struct_type->validate(types);
        // } catch (UnlocatedProblem& e) {
        //     e.report_at(node->location_);
        //     return &UnknownValue::instance;
        // }
        // return new StructValue(
        //     struct_type,
        //     inits | GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>()
        // );
    }
};

class TypeCheckVisitor {
private:
    TypeChecker& checker_;

public:
    TypeCheckVisitor(TypeChecker& checker) noexcept : checker_(checker) {}

    void operator()(const ASTNodeVariant& node) noexcept { std::visit(*this, node); }

    // Root and blocks
    void operator()(const ASTRoot* node) noexcept {
        for (auto stmt : node->statements_) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTLocalBlock* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (auto stmt : node->statements_) {
            (*this)(stmt);
        }
    }

    // Expressions
    void operator()(const ASTExpression* node) noexcept { UNREACHABLE(); }

    // Statements
    void operator()(const ASTExpressionStatement* node) noexcept {
        ValueContextEvaluator{checker_, nullptr, false}(node->expr_);
    }

    void operator()(const ASTDeclaration* node) noexcept { checker_.lookup(node->identifier_); }

    void operator()(const ASTTypeAlias* node) noexcept { checker_.lookup_type(node->identifier_); }

    void operator()(const ASTIfStatement* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        ValueContextEvaluator{checker_, &BooleanType::instance, false}(node->condition_);
        (*this)(node->if_block_);
        if (node->else_block_) {
            (*this)(node->else_block_);
        }
    }

    void operator()(const ASTForStatement* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        if (node->initializer_decl_) {
            ValueContextEvaluator{checker_, nullptr, false}(node->initializer_decl_);
        } else if (!std::holds_alternative<std::monostate>(node->initializer_expr_)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->initializer_expr_);
        }
        if (!std::holds_alternative<std::monostate>(node->condition_)) {
            ValueContextEvaluator{checker_, &BooleanType::instance, false}(node->condition_);
        }
        if (!std::holds_alternative<std::monostate>(node->increment_)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->increment_);
        }
        (*this)(node->body_);
    }

    void operator()(const ASTReturnStatement* node) noexcept {
        if (!std::holds_alternative<std::monostate>(node->expr_)) {
            ValueContextEvaluator{checker_, nullptr, false}(node->expr_);
        }
    }

    // Functions and classes
    void operator()(const ASTFunctionDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (auto& stmt : node->body_) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTConstructorDestructorDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (auto& stmt : node->body_) {
            (*this)(stmt);
        }
    }

    void operator()(const ASTClassDefinition* node) noexcept {
        checker_.lookup_type(node->identifier_);  // trigger self type injection
        TypeChecker::Guard guard(checker_, node);
        for (auto& field : node->fields_) {
            (*this)(field);
        }
        for (auto& ctor : node->constructors_) {
            (*this)(ctor);
        }
        if (node->destructor_) {
            (*this)(node->destructor_);
        }
        for (auto& func : node->functions_) {
            (*this)(func);
        }
    }

    void operator()(const ASTNamespaceDefinition* node) noexcept {
        TypeChecker::Guard guard(checker_, node);
        for (const auto& item : node->items_) {
            (*this)(item);
        }
    }
};

inline TypeResolution TypeChecker::lookup_type(std::string_view identifier) {
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

inline Term TypeChecker::lookup_term(std::string_view identifier) {
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
        return Term::type(lookup_type(class_def->identifier_));
    } else if (auto var_init = value->get<const VariableInitialization*>()) {
        ASTExprVariant var_init_expr = *reinterpret_cast<const ASTExprVariant*>(var_init);
        return ValueContextEvaluator{*this, nullptr, false}(var_init_expr).subject;
    } else if (auto func = value->get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
        // Guard guard(*this, scope);
        // GlobalMemory::Vector<FunctionObject> func_vec =
        //     *func | std::views::transform([&](const FunctionOverloadDecl& func_decl) {
        //         if (auto func_def = func_decl.get<const ASTFunctionDefinition*>()) {
        //             return func_def->get_func_obj(*this);
        //         } else {
        //             /// TODO: handle template functions
        //             return FunctionObject{};
        //         }
        //     }) |
        //     GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>();
        // return Term::prvalue(new FunctionOverloadSetValue(func_vec));
    } else {
        /// TODO: throw
        assert(false);
    }
}

inline Term TypeChecker::lookup_instantiation(std::string_view name, std::span<Term> args) {
    auto [scope, value] = lookup(name);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(name);
    }
    if (auto temp = value->get<TemplateFamily*>()) {
        Guard guard(*this, scope);
        if (validate_instantiation(temp->primary_, args)) {
            Scope& instantiation_scope = specialization_resolution(*temp, args);
            SymbolCollector{instantiation_scope, sema_}(temp->primary_.target_node_);
            Guard inner_guard(*this, &instantiation_scope);
            TypeCheckVisitor{*this}(temp->primary_.target_node_);
            return lookup_term(temp->primary_.identifier_);
        }
    } else {
        throw;
    }
}

inline bool TypeChecker::validate_instantiation(
    const ASTTemplateDefinition& primary, std::span<Term> args
) {
    if (primary.parameters_.size() != args.size()) {
        return false;
    }
    for (size_t i = 0; i < args.size(); i++) {
        const auto& param = primary.parameters_[i];
        if (args[i].is_type() == param.is_nttp_) {
            return false;
        }
        if (args[i].is_type()) {
            /// TODO: type constraint validation
        } else {
            if (!std::holds_alternative<std::monostate>(param.constraint_)) {
                Term constraint_term =
                    ValueContextEvaluator{*this, nullptr, false}(param.constraint_).subject;
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

inline Scope& TypeChecker::specialization_resolution(
    const TemplateFamily& family, std::span<Term> args
) {
    for (const auto& specialization : family.specializations_) {
        /// TODO:
    }
    Scope& inst_scope = Scope::make_unlinked(*current_scope_, nullptr);
    Guard guard(*this, &inst_scope);
    for (size_t i = 0; i < args.size(); i++) {
        inst_scope.add_template_argument(family.primary_.parameters_[i].identifier_, args[i]);
    }
    return inst_scope;
}
