#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

class Transpiler;

class ASTNode;
class ASTRoot;
class ASTLocalBlock;
class ASTExpression;
class ASTConstant;
class ASTIdentifier;
template <typename Op>
class ASTUnaryOp;
template <typename Op>
class ASTBinaryOp;
class ASTFunctionCall;
class ASTPrimitiveType;
class ASTStructType;
class ASTDeclaration;
class ASTFieldDeclaration;
class ASTTypeAlias;
class ASTIfStatement;
class ASTForStatement;
class ASTContinueStatement;
class ASTBreakStatement;
class ASTReturnStatement;
class ASTFunctionParameter;
class ASTFunctionSignature;
class ASTFunctionDefinition;
class ASTClassDefinition;
class ASTTemplateDefinition;

using ScopeValue = PointerVariant<
    const ASTExpression*,                                // type alias
    Term*,                                               // constant/variable
    GlobalMemory::Vector<const ASTFunctionSignature*>*,  // function overloads
    const ASTTemplateDefinition*>;                       // template definition

class Scope final : public GlobalMemory::MemoryManaged {
    friend class TypeChecker;

public:
    static Scope& create(const void* owner, Scope& parent, std::string_view name = "") {
        Scope* scope = new Scope(parent, name);
        Scope& ref = *scope;
        parent.children_.insert({owner, scope});
        return ref;
    }

    static Scope& create_hidden(Scope& parent) { return *new Scope(parent, ""); }

private:
    Scope* parent_ = nullptr;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;
    GlobalMemory::FlatMap<const void*, Scope*> children_;

public:
    std::string_view prefix_;

private:
    Scope(Scope& parent, std::string_view name) noexcept : parent_(&parent) {
        if (name.empty()) {
            prefix_ = parent.prefix_;
        } else {
            prefix_ = GlobalMemory::format_view("{}{}::", parent.prefix_, name);
        }
    }

public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    Scope* parent() const noexcept { return parent_; }

    void add_type(std::string_view identifier, const ASTExpression* expr) {
        auto [_, inserted] = identifiers_.insert({identifier, expr});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void set_variable(std::string_view identifier, Term term) {
        auto [_, inserted] = identifiers_.insert({identifier, new Term(term)});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_function(std::string_view identifier, const ASTFunctionSignature* expr) {
        if (!identifiers_.contains(identifier)) {
            auto overloads =
                GlobalMemory::alloc<GlobalMemory::Vector<const ASTFunctionSignature*>>();
            overloads->push_back(expr);
            identifiers_[identifier] = overloads;
        } else {
            auto it = identifiers_.find(identifier);
            if (it != identifiers_.end() &&
                !it->second.get<GlobalMemory::Vector<const ASTFunctionSignature*>*>()) {
                throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
            }
        }
    }

    void add_template(std::string_view identifier, const ASTTemplateDefinition* definition) {
        auto [_, inserted] = identifiers_.insert({identifier, definition});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    ScopeValue& operator[](std::string_view identifier) noexcept {
        assert(identifiers_.contains(identifier));
        return identifiers_.at(identifier);
    }
};

class TypeChecker final {
private:
    Scope* current_scope_;
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, TypeResolution> id_cache_;
    // GlobalMemory::FlatMap<std::pair<const Scope*, const ASTExpression*>, Type*> ptr_cache_;

public:
    OperationHandler& ops_;

public:
    TypeChecker(Scope& root, OperationHandler& ops) noexcept : current_scope_(&root), ops_(ops) {}

    void add_variable(std::string_view identifier, Term term) {
        current_scope_->set_variable(identifier, term);
    }

    void enter(const void* child) noexcept { current_scope_ = current_scope_->children_.at(child); }

    void exit() noexcept { current_scope_ = current_scope_->parent_; }

    Scope* current_scope() noexcept { return current_scope_; }

    std::pair<const Scope*, const ScopeValue*> lookup(std::string_view identifier) const noexcept {
        Scope* scope = current_scope_;
        while (scope) {
            auto it = scope->identifiers_.find(identifier);
            if (it != scope->identifiers_.end()) {
                return {scope, &it->second};
            }
            scope = scope->parent();
        }
        return {nullptr, nullptr};
    }

    TypeResolution lookup_type(std::string_view identifier) {
        return lookup_type_in(identifier, *current_scope_);
    }

    Term lookup_term(std::string_view identifier) {
        return lookup_term_in(identifier, *current_scope_);
    }

    bool is_at_top_level() const noexcept { return current_scope_->parent_ == nullptr; }

private:
    TypeResolution lookup_type_in(std::string_view identifier, Scope& scope);

    Term lookup_term_in(std::string_view identifier, Scope& scope);
};

class ASTNode : public GlobalMemory::MemoryManaged {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
    virtual ~ASTNode() noexcept = default;
    virtual void collect_symbols(Scope& scope, OperationHandler& ops) {}
    virtual void check_types(TypeChecker& checker) {}
    virtual void transpile(Transpiler& transpiler, Cursor& cursor) const { UNREACHABLE(); };
};

class ASTTemplateTarget {
public:
    virtual ~ASTTemplateTarget() noexcept = default;
    virtual ASTNode* as_node() noexcept = 0;
    virtual std::string_view get_template_name() const noexcept = 0;
};

class ASTRoot final : public ASTNode {
public:
    ComparableSpan<ASTNode*> statements_;
    ASTRoot(const Location& loc, ComparableSpan<ASTNode*> statements) noexcept;
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        for (auto& child : statements_) {
            child->collect_symbols(scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        for (auto& child : statements_) {
            child->check_types(checker);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTLocalBlock final : public ASTNode {
public:
    ComparableSpan<ASTNode*> statements_;
    ASTLocalBlock(const Location& loc, ComparableSpan<ASTNode*> statements) noexcept
        : ASTNode(loc), statements_(statements) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        Scope& local_scope = Scope::create(this, scope);
        for (auto& stmt : statements_) {
            stmt->collect_symbols(local_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(this);
        for (auto& stmt : statements_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}
    void eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete = true
    ) const noexcept {
        // if (Type*& cached = checker.query_type(const_cast<ASTExpression*>(this))) {
        //     return cached;
        // } else {
        //     cached = eval_type_impl(checker, require_complete);
        //     return cached;
        // }
        eval_type_impl(checker, out, require_complete);
        assert(require_complete ? out.is_sized() : true);
    }
    virtual Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_const
    ) const noexcept = 0;
    void check_types(TypeChecker& checker) final {
        std::ignore = eval_term(checker, nullptr, false);
    }

protected:
    virtual void eval_type_impl(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept = 0;
};

class ASTExplicitTypeExpr : public ASTExpression {
public:
    ASTExplicitTypeExpr(const Location& loc) noexcept : ASTExpression(loc) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_const
    ) const noexcept final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return Term::unknown();
    }
};

/// A hidden type expression that does not appear in source code
class ASTHiddenTypeExpr : public ASTExplicitTypeExpr {
public:
    ASTHiddenTypeExpr() noexcept : ASTExplicitTypeExpr({}) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTConstant final : public ASTExpression {
public:
    Value* value_;

public:
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_const
    ) const noexcept final {
        if (expected) {
            try {
                Value* typed_value = value_->resolve_to(expected);
                return Term(typed_value, Term::Category::CompRValue);
            } catch (UnlocatedProblem& e) {
                e.report_at(location_);
                return Term::unknown();
            }
        }
        return Term(value_, Term::Category::CompRValue);
    }
    void resolve_type(const Type* target_type) { value_ = value_->resolve_to(target_type); }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        /// TODO: return literal type
        out = {};
    }
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view str_;

public:
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), str_(name) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        try {
            Term term = checker.lookup_term(str_);
            if (expected_comptime && !term.is_comptime()) {
                Diagnostic::report(NotConstantExpressionError(location_));
                return Term::unknown();
            }
            return term;
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return Term::unknown();
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        TypeResolution result = checker.lookup_type(str_);
        if (!result.is_sized()) {
            if (require_complete) {
                Diagnostic::report(CircularTypeDependencyError(location_));
                out = TypeRegistry::get_unknown();
                return;
            }
        }
        out = result;
    }
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    ASTExpression* const expr_;

public:
    ASTUnaryOp(const Location& loc, ASTExpression* expr) noexcept
        : ASTExpression(loc), expr_(expr) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        Term expr_term = expr_->eval_term(checker, expected, expected_comptime);
        return checker.ops_.eval_value_op(Op::opcode, expr_term);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        TypeResolution expr_result;
        expr_->eval_type(checker, expr_result);
        try {
            out = TypeResolution(checker.ops_.eval_type_op(Op::opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            out = TypeRegistry::get_unknown();
        }
    }
};

template <typename Op>
class ASTBinaryOp final : public ASTExpression {
public:
    ASTExpression* const left_;
    ASTExpression* const right_;

public:
    ASTBinaryOp(const Location& loc, ASTExpression* left, ASTExpression* right) noexcept
        : ASTExpression(loc), left_(left), right_(right) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        Term left_term = left_->eval_term(checker, expected, expected_comptime);
        Term right_term = right_->eval_term(checker, expected, expected_comptime);
        try {
            return checker.ops_.eval_value_op(Op::opcode, left_term, right_term);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return Term::unknown();
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        TypeResolution left_result;
        left_->eval_type(checker, left_result);
        TypeResolution right_result;
        right_->eval_type(checker, right_result);
        try {
            out = checker.ops_.eval_type_op(Op::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            out = TypeRegistry::get_unknown();
        }
    }
};

using ASTAddOp = ASTBinaryOp<OperatorFunctors::Add>;
using ASTSubtractOp = ASTBinaryOp<OperatorFunctors::Subtract>;
using ASTNegateOp = ASTUnaryOp<OperatorFunctors::Negate>;
using ASTMultiplyOp = ASTBinaryOp<OperatorFunctors::Multiply>;
using ASTDivideOp = ASTBinaryOp<OperatorFunctors::Divide>;
using ASTRemainderOp = ASTBinaryOp<OperatorFunctors::Remainder>;
using ASTIncrementOp = ASTUnaryOp<OperatorFunctors::Increment>;
using ASTDecrementOp = ASTUnaryOp<OperatorFunctors::Decrement>;

using ASTEqualOp = ASTBinaryOp<OperatorFunctors::Equal>;
using ASTNotEqualOp = ASTBinaryOp<OperatorFunctors::NotEqual>;
using ASTLessThanOp = ASTBinaryOp<OperatorFunctors::LessThan>;
using ASTLessEqualOp = ASTBinaryOp<OperatorFunctors::LessEqual>;
using ASTGreaterThanOp = ASTBinaryOp<OperatorFunctors::GreaterThan>;
using ASTGreaterEqualOp = ASTBinaryOp<OperatorFunctors::GreaterEqual>;

using ASTLogicalAndOp = ASTBinaryOp<OperatorFunctors::LogicalAnd>;
using ASTLogicalOrOp = ASTBinaryOp<OperatorFunctors::LogicalOr>;
using ASTLogicalNotOp = ASTUnaryOp<OperatorFunctors::LogicalNot>;

using ASTBitwiseAndOp = ASTBinaryOp<OperatorFunctors::BitwiseAnd>;
using ASTBitwiseOrOp = ASTBinaryOp<OperatorFunctors::BitwiseOr>;
using ASTBitwiseXorOp = ASTBinaryOp<OperatorFunctors::BitwiseXor>;
using ASTBitwiseNotOp = ASTUnaryOp<OperatorFunctors::BitwiseNot>;
using ASTLeftShiftOp = ASTBinaryOp<OperatorFunctors::LeftShift>;
using ASTRightShiftOp = ASTBinaryOp<OperatorFunctors::RightShift>;

using ASTAssignOp = ASTBinaryOp<OperatorFunctors::Assign>;
using ASTAddAssignOp = ASTBinaryOp<OperatorFunctors::AddAssign>;
using ASTSubtractAssignOp = ASTBinaryOp<OperatorFunctors::SubtractAssign>;
using ASTMultiplyAssignOp = ASTBinaryOp<OperatorFunctors::MultiplyAssign>;
using ASTDivideAssignOp = ASTBinaryOp<OperatorFunctors::DivideAssign>;
using ASTRemainderAssignOp = ASTBinaryOp<OperatorFunctors::RemainderAssign>;
using ASTLogicalAndAssignOp = ASTBinaryOp<OperatorFunctors::LogicalAndAssign>;
using ASTLogicalOrAssignOp = ASTBinaryOp<OperatorFunctors::LogicalOrAssign>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>;
using ASTLeftShiftAssignOp = ASTBinaryOp<OperatorFunctors::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorFunctors::RightShiftAssign>;

class ASTMemberAccess final : public ASTExpression {
public:
    ASTExpression* target_;
    std::string_view field_;

public:
    ASTMemberAccess(const Location& loc, ASTExpression* target, std::string_view field) noexcept
        : ASTExpression(loc), target_(target), field_(field) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        Term term = target_->eval_term(checker, nullptr, false);
        if (auto value = term.get_comptime()) {
            Value* member_value = value->member(field_);
            if (!member_value) {
                Diagnostic::report(AttributeError(location_, field_));
            }
            return member_value ? Term(member_value, Term::Category::CompRValue) : Term::unknown();
        } else {
            const Type* type = term->cast<Type>();
            const Type* member_type = type->member(field_);
            if (!member_type) {
                Diagnostic::report(AttributeError(location_, field_));
            }
            return member_type ? Term(member_type, Term::Category::Type) : Term::unknown();
        }
    }

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        // TODO
    };
};

class ASTFieldInitialization final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* value_;

public:
    ASTFieldInitialization(
        const Location& loc, std::string_view identifier, ASTExpression* value
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), value_(value) {}
    std::pair<std::string_view, Term> eval(TypeChecker& checker) const noexcept {
        Term value_term = value_->eval_term(checker, nullptr, false);
        if (value_term.is_type()) {
            Diagnostic::report(SymbolCategoryMismatchError(location_, true));
            return {identifier_, Term::unknown()};
        }
        return {identifier_, value_term};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTStructInitialization final : public ASTExpression {
public:
    ASTExpression* struct_type_;
    ComparableSpan<ASTFieldInitialization*> field_inits_;

public:
    ASTStructInitialization(
        const Location& loc,
        ASTExpression* struct_type,
        ComparableSpan<ASTFieldInitialization*> field_inits
    ) noexcept
        : ASTExpression(loc), struct_type_(struct_type), field_inits_(field_inits) {}

    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        TypeResolution struct_type_res;
        struct_type_->eval_type(checker, struct_type_res);
        if (auto struct_type = struct_type_res->dyn_cast<StructType>()) {
            if (expected_comptime) {
                return Term(eval_comptime(checker, struct_type), Term::Category::CompRValue);
            } else {
                check_fields(checker, struct_type);
                return Term(struct_type, Term::Category::RValue);
            }
        } else {
            Diagnostic::report(
                TypeMismatchError(struct_type_->location_, "struct", struct_type_res->repr())
            );
            return Term::unknown();
        }
    }

protected:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        UNREACHABLE();
    }

private:
    Value* eval_comptime(TypeChecker& checker, const StructType* struct_type) const noexcept {
        GlobalMemory::Vector<std::pair<std::string_view, Value*>> inits =
            field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
                std::pair<std::string_view, Term> field = init->eval(checker);
                if (!field.second.is_comptime()) {
                    Diagnostic::report(NotConstantExpressionError(init->location_));
                    return std::pair<std::string_view, Value*>{
                        field.first, &UnknownValue::instance
                    };
                }
                return std::pair<std::string_view, Value*>{
                    field.first, field.second.get_comptime()
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
        try {
            struct_type->validate(types);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return &UnknownValue::instance;
        }
        return new StructValue(
            struct_type,
            inits | GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>()
        );
    }

    void check_fields(TypeChecker& checker, const StructType* struct_type) const noexcept {
        GlobalMemory::Vector<std::pair<std::string_view, const Type*>> inits =
            field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
                std::pair<std::string_view, Term> field = init->eval(checker);
                return std::pair<std::string_view, const Type*>{
                    field.first, field.second.effective_type()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const Type*>>>();
        try {
            struct_type->validate(inits);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTFunctionCall final : public ASTExpression {
public:
    ASTExpression* const function_;
    ComparableSpan<ASTExpression*> arguments_;

public:
    ASTFunctionCall(
        const Location& loc, ASTExpression* function, ComparableSpan<ASTExpression*> arguments
    ) noexcept
        : ASTExpression(loc), function_(function), arguments_(arguments) {}
    Term eval_term(
        TypeChecker& checker, const Type* expected, bool expected_comptime
    ) const noexcept final {
        // TODO
        return {};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        // TODO
        out = {};
    }
};

class ASTPrimitiveType final : public ASTExplicitTypeExpr {
public:
    const Type* type_;

public:
    ASTPrimitiveType(const Location& loc, const Type* type) noexcept
        : ASTExplicitTypeExpr(loc), type_(type) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = type_;
    }
};

class ASTFunctionType final : public ASTExplicitTypeExpr {
public:
    ComparableSpan<ASTExpression*> parameter_types_;
    ASTExpression* return_type_;

public:
    ASTFunctionType(
        const Location& loc,
        ComparableSpan<ASTExpression*> parameter_types,
        ASTExpression* return_type
    ) noexcept
        : ASTExplicitTypeExpr(loc), parameter_types_(parameter_types), return_type_(return_type) {}

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<FunctionType>();
        bool any_error = false;
        ComparableSpan<const Type*> param_types =
            parameter_types_ | std::views::transform([&](ASTExpression* param_expr) -> const Type* {
                TypeResolution param_type;
                param_expr->eval_type(checker, param_type);
                return param_type;
            }) |
            GlobalMemory::collect<ComparableSpan<const Type*>>();
        if (any_error) {
            out = TypeRegistry::get_unknown();
            return;
        }
        TypeResolution return_type;
        return_type_->eval_type(checker, return_type);
        TypeRegistry::get_at<FunctionType>(out, param_types, return_type);
    }
};

class ASTFieldDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* type_;

public:
    ASTFieldDeclaration(
        const Location& loc, std::string_view identifier, ASTExpression* type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(type) {}
    void check_types(TypeChecker& checker) final {
        TypeResolution field_type;
        type_->eval_type(checker, field_type);
        checker.add_variable(identifier_, Term(field_type, Term::Category::Var));
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTStructType final : public ASTExplicitTypeExpr {
public:
    ComparableSpan<ASTFieldDeclaration*> fields_;

public:
    ASTStructType(const Location& loc, ComparableSpan<ASTFieldDeclaration*> fields) noexcept
        : ASTExplicitTypeExpr(loc), fields_(fields) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<StructType>();
        GlobalMemory::FlatMap<std::string_view, const Type*> field_map =
            fields_ |
            std::views::transform(
                [&](ASTFieldDeclaration* decl) -> std::pair<std::string_view, const Type*> {
                    TypeResolution field_type;
                    decl->type_->eval_type(checker, field_type);
                    return {decl->identifier_, field_type};
                }
            ) |
            GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
        TypeRegistry::get_at<StructType>(out, field_map);
    }
};

class ASTMutableTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;

public:
    ASTMutableTypeExpr(const Location& loc, ASTExpression* expr) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        TypeRegistry::get_at<MutableType>(out, expr_type);
    }
};

class ASTReferenceTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;

public:
    ASTReferenceTypeExpr(const Location& loc, ASTExpression* expr, bool is_mutable) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out, expr_type);
    }
};

class ASTPointerTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;
    bool is_mutable_;

public:
    ASTPointerTypeExpr(const Location& loc, ASTExpression* expr, bool is_mutable) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr), is_mutable_(is_mutable) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<PointerType>();
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out, expr_type);
    }
};

class ASTExpressionStatement final : public ASTNode {
public:
    ASTExpression* const expr_;
    ASTExpressionStatement(const Location& loc, ASTExpression* expr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void check_types(TypeChecker& checker) final { expr_->check_types(checker); }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* declared_type_;
    ASTExpression* expr_;
    bool is_mutable_;
    bool is_constant_;
    ASTDeclaration(
        const Location& loc,
        std::string_view identifier,
        ASTExpression* declared_type,
        ASTExpression* expr,
        bool is_mutable,
        bool is_constant
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          declared_type_(declared_type),
          expr_(expr),
          is_mutable_(is_mutable),
          is_constant_(is_constant) {
        assert(declared_type || expr);
        assert(!(is_mutable_ && is_constant_));
    }
    void check_types(TypeChecker& checker) final {
        TypeResolution declared_type;
        if (declared_type_) {
            declared_type_->eval_type(checker, declared_type);
        }
        Term term = Term(declared_type, Term::Category::Var);
        if (expr_) {
            term = expr_->eval_term(checker, declared_type, is_constant_);
            if (!is_constant_) {
                term = Term(term.effective_type(), Term::Category::Var);
            }
        }
        try {
            checker.add_variable(identifier_, term);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTTypeAlias final : public ASTNode, public ASTTemplateTarget {
public:
    std::string_view identifier_;
    ASTExpression* const type_;

public:
    ASTTypeAlias(const Location& loc, std::string_view identifier, ASTExpression* type) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(type) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_type(identifier_, type_);
    }
    void check_types(TypeChecker& checker) final { checker.lookup_type(identifier_); }
    ASTNode* as_node() noexcept final { return this; }
    std::string_view get_template_name() const noexcept final { return identifier_; }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTIfStatement final : public ASTNode {
public:
    ASTExpression* const condition_;
    const ComparableSpan<ASTNode*> if_block_;
    const ComparableSpan<ASTNode*> else_block_;
    ASTIfStatement(
        const Location& loc,
        ASTExpression* condition,
        ComparableSpan<ASTNode*> if_block,
        ComparableSpan<ASTNode*> else_block = {}
    ) noexcept
        : ASTNode(loc), condition_(condition), if_block_(if_block), else_block_(else_block) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        Scope& if_scope = Scope::create(&if_block_, scope);
        for (auto& stmt : if_block_) {
            stmt->collect_symbols(if_scope, ops);
        }
        if (!else_block_.empty()) {
            Scope& else_scope = Scope::create(&else_block_, scope);
            for (auto& stmt : else_block_) {
                stmt->collect_symbols(else_scope, ops);
            }
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(&if_block_);
        condition_->eval_term(checker, &BooleanType::instance, false);
        for (auto& stmt : if_block_) {
            stmt->check_types(checker);
        }
        checker.exit();
        if (!else_block_.empty()) {
            checker.enter(&else_block_);
            for (auto& stmt : else_block_) {
                stmt->check_types(checker);
            }
            checker.exit();
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTForStatement final : public ASTNode {
public:
    ASTNode* const initializer_;  // Declaration or expression
    ASTExpression* const condition_;
    ASTExpression* const increment_;
    const ComparableSpan<ASTNode*> body_;
    ASTForStatement(
        const Location& loc,
        ASTDeclaration* initializer,
        ASTExpression* condition,
        ASTExpression* increment,
        ComparableSpan<ASTNode*> body
    ) noexcept
        : ASTNode(loc),
          initializer_(initializer),
          condition_(condition),
          increment_(increment),
          body_(body) {}
    ASTForStatement(
        const Location& loc,
        ASTExpression* initializer,
        ASTExpression* condition,
        ASTExpression* increment,
        ComparableSpan<ASTNode*> body
    ) noexcept
        : ASTNode(loc),
          initializer_(initializer),
          condition_(condition),
          increment_(increment),
          body_(body) {}
    ASTForStatement(const Location& loc, ComparableSpan<ASTNode*> body) noexcept
        : ASTNode(loc),
          initializer_(nullptr),
          condition_(nullptr),
          increment_(nullptr),
          body_(body) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        Scope& local_scope = Scope::create(&body_, scope);
        if (initializer_) {
            initializer_->collect_symbols(local_scope, ops);
        }
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(&body_);
        if (initializer_) {
            initializer_->check_types(checker);
        }
        if (condition_) {
            condition_->eval_term(checker, &BooleanType::instance, false);
        }
        if (increment_) {
            increment_->eval_term(checker, nullptr, false);
        }
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTContinueStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTBreakStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTReturnStatement final : public ASTNode {
public:
    ASTExpression* const expr_;
    ASTReturnStatement(const Location& loc, ASTExpression* expr = nullptr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void check_types(TypeChecker& checker) final {
        if (expr_) expr_->eval_term(checker, nullptr, false);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* type_;
    ASTFunctionParameter(
        const Location& loc, std::string_view identifier, ASTExpression* type
    ) noexcept
        : ASTNode(loc), identifier_(identifier), type_(type) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTFunctionSignature final {
public:
    const ASTFunctionDefinition* owner_;
    ASTFunctionSignature(const ASTFunctionDefinition* owner) noexcept : owner_(owner) {}
    const Object* eval(TypeChecker& checker) const noexcept;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    ComparableSpan<ASTFunctionParameter*> parameters_;
    ASTExpression* return_type_;
    ComparableSpan<ASTNode*> body_;
    bool is_const_;
    bool is_static_;
    ASTFunctionSignature* signature_;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::string_view identifier,
        ComparableSpan<ASTFunctionParameter*> parameters,
        ASTExpression* return_type,
        ComparableSpan<ASTNode*> body,
        bool is_const,
        bool is_static
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          parameters_(parameters),
          return_type_(return_type),
          body_(body),
          is_const_(is_const),
          is_static_(is_static),
          signature_(new ASTFunctionSignature(this)) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_function(identifier_, signature_);
        Scope& local_scope = Scope::create(&body_, scope);
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(&body_);
        for (auto& param : parameters_) {
            TypeResolution param_type;
            param->type_->eval_type(checker, param_type);
            checker.add_variable(param->identifier_, Term(param_type, Term::Category::Var));
        }
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTClassSignature final : public ASTHiddenTypeExpr {
public:
    const ASTClassDefinition* owner_;
    ASTClassSignature(const ASTClassDefinition* owner) noexcept : owner_(owner) {}
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final;
};

class ASTClassDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    std::string_view extends_;
    ComparableSpan<std::string_view> implements_;
    ComparableSpan<ASTDeclaration*> fields_;
    ComparableSpan<ASTTypeAlias*> aliases_;
    ComparableSpan<ASTFunctionDefinition*> functions_;
    ComparableSpan<ASTClassDefinition*> classes_;

    ASTClassSignature* signature_;

public:
    ASTClassDefinition(
        const Location& loc,
        std::string_view identifier,
        std::string_view extends,
        ComparableSpan<std::string_view> implements,
        ComparableSpan<ASTDeclaration*> fields,
        ComparableSpan<ASTTypeAlias*> aliases,
        ComparableSpan<ASTFunctionDefinition*> functions,
        ComparableSpan<ASTClassDefinition*> classes
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          extends_(extends),
          implements_(implements),
          fields_(fields),
          aliases_(aliases),
          functions_(functions),
          classes_(classes),
          signature_(new ASTClassSignature(this)) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        Scope& static_scope = Scope::create(this, scope, identifier_);
        Scope& instance_scope = Scope::create(&identifier_, static_scope);
        for (auto& func : functions_) {
            if (func->is_static_) {
                func->collect_symbols(static_scope, ops);
            } else {
                func->collect_symbols(instance_scope, ops);
            }
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(this);          // static scope
        checker.enter(&identifier_);  // instance scope
        for (auto& field : fields_) {
            field->check_types(checker);
        }
        checker.exit();  // instance scope
        for (auto& func : functions_) {
            if (!func->is_static_) {
                checker.enter(&identifier_);  // instance scope
            }
            func->check_types(checker);
            if (!func->is_static_) {
                checker.exit();  // instance scope
            }
        }
        checker.exit();  // static scope
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTNamespaceDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    ComparableSpan<ASTNode*> items_;
    ASTNamespaceDefinition(
        const Location& loc, std::string_view identifier, ComparableSpan<ASTNode*> items
    ) noexcept
        : ASTNode(loc), identifier_(identifier), items_(items) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        Scope& namespace_scope = Scope::create(this, scope, identifier_);
        for (auto& item : items_) {
            item->collect_symbols(namespace_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(this);
        for (auto& item : items_) {
            item->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTTemplateTypeArgument final : public ASTHiddenTypeExpr {
public:
    Type* type_;

public:
    ASTTemplateTypeArgument(Type* type) noexcept : type_(type) {}

private:
    void eval_type_impl(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = type_;
    }
};

class ASTTemplateDefinition final : public ASTNode {
public:
    ASTTemplateTarget* target_;
    ComparableSpan<std::pair<std::string_view, ASTExplicitTypeExpr*>> parameters_;

public:
    ASTTemplateDefinition(
        const Location& loc,
        ASTTemplateTarget* target,
        ComparableSpan<std::pair<std::string_view, ASTExplicitTypeExpr*>> parameters
    ) noexcept
        : ASTNode(loc), target_(target), parameters_(parameters) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_template(target_->get_template_name(), this);
    }
    ScopeValue instantiate(TypeChecker& checker, ComparableSpan<Object*> arguments) {
        if (arguments.size() != parameters_.size()) {
            Diagnostic::report(
                TemplateArgumentCountMismatchError(location_, parameters_.size(), arguments.size())
            );
            return new Term(Term::unknown());
        }
        Scope& template_scope = Scope::create(this, *checker.current_scope());
        for (size_t i = 0; i < parameters_.size(); ++i) {
            const auto& [param_name, param_type] = parameters_[i];
            Object* argument = arguments[i];
            if ((param_type == nullptr) != (argument->dyn_type() != nullptr)) {
                Diagnostic::report(TemplateArgumentCategoryMismatchError(
                    location_, param_name, param_type != nullptr
                ));
                return new Term(Term::unknown());
            }
            if (param_type == nullptr) {
                // type parameter
                template_scope.add_type(
                    param_name, new ASTTemplateTypeArgument(argument->cast<Type>())
                );
            } else {
                TypeResolution constraint_type;
                param_type->eval_type(checker, constraint_type);
                if (!constraint_type->assignable_from(argument->cast<Value>()->get_type())) {
                    Diagnostic::report(TemplateArgumentTypeMismatchError(
                        location_,
                        param_name,
                        constraint_type->repr(),
                        argument->cast<Value>()->get_type()->repr()
                    ));
                    return new Term(Term::unknown());
                }
                template_scope.set_variable(
                    param_name, Term(argument->cast<Value>(), Term::Category::CompConst)
                );
            }
        }
        ASTNode* node = target_->as_node();
        node->collect_symbols(template_scope, checker.ops_);
        checker.enter(this);
        node->check_types(checker);
        checker.exit();
        return template_scope[target_->get_template_name()];
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

// ===================== Inline implementations =====================

inline TypeResolution TypeChecker::lookup_type_in(std::string_view identifier, Scope& scope) {
    // Check cache
    auto [it_id_cache, inserted] =
        id_cache_.insert({std::pair{&scope, identifier}, TypeResolution()});
    if (!inserted) {
        return it_id_cache->second;
    }
    // Cache miss; resolve
    auto it_id = scope.identifiers_.find(identifier);
    if (it_id == scope.identifiers_.end()) {
        if (scope.parent_ != nullptr) {
            it_id_cache->second = lookup_type_in(identifier, *scope.parent_);
            return it_id_cache->second;
        }
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    } else if (auto type = it_id->second.get<const ASTExpression*>()) {
        Scope* previous_scope = std::exchange(current_scope_, &scope);
        type->eval_type(*this, it_id_cache->second);
        current_scope_ = previous_scope;
        if (TypeRegistry::is_type_incomplete(it_id_cache->second)) {
            const Type* incomplete_type = it_id_cache->second;
            id_cache_.erase(it_id_cache);
            return incomplete_type;
        } else {
            return it_id_cache->second;
        }
    } else if (it_id->second.get<Term*>()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(false);
    } else if (it_id->second.get<GlobalMemory::Vector<const ASTFunctionSignature*>*>()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(false);
    } else {
        /// TODO: template instantiation
        assert(false);
    }
}

inline Term TypeChecker::lookup_term_in(std::string_view identifier, Scope& scope) {
    auto it = scope.identifiers_.find(identifier);
    if (it == scope.identifiers_.end()) {
        if (scope.parent_ != nullptr) {
            return lookup_term_in(identifier, *scope.parent_);
        }
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    } else if (auto term = it->second.get<Term*>()) {
        const Type* type = (*term)->dyn_type();
        if (!type) {
            type = (*term)->cast<Value>()->get_type();
        }
        return *term;
    } else if (auto func = it->second.get<GlobalMemory::Vector<const ASTFunctionSignature*>*>()) {
        Scope* previous_scope = std::exchange(current_scope_, &scope);
        ComparableSpan<const Type*> overload_types =
            *func | std::views::transform([this](const ASTFunctionSignature* expr) -> const Type* {
                const Object* overload = expr->eval(*this);
                if (auto type = overload->dyn_type()) {
                    return type->cast<FunctionType>();
                }
                return overload->cast<FunctionValue>()->get_type();
            }) |
            GlobalMemory::collect<ComparableSpan<const Type*>>();
        const IntersectionType* intersection_type =
            TypeRegistry::get<IntersectionType>(overload_types);
        current_scope_ = previous_scope;
        return Term(intersection_type, Term::Category::Var);
    } else if (it->second.get<const ASTExpression*>()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    } else {
        /// TODO: template instantiation
        assert(false);
    }
}

inline ASTRoot::ASTRoot(const Location& loc, ComparableSpan<ASTNode*> statements) noexcept
    : ASTNode(loc), statements_(statements) {
    for (auto& stmt : statements_) {
        if (auto func_decl = dynamic_cast<ASTFunctionDefinition*>(stmt)) {
            func_decl->is_static_ = true;
        }
    }
}

inline const Object* ASTFunctionSignature::eval(TypeChecker& checker) const noexcept {
    bool any_error = false;
    ComparableSpan params = owner_->parameters_ |
                            std::views::transform([&](ASTFunctionParameter* param) -> const Type* {
                                TypeResolution param_type;
                                param->type_->eval_type(checker, param_type);
                                return param_type;
                            }) |
                            GlobalMemory::collect<ComparableSpan<const Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    TypeResolution return_type;
    owner_->return_type_->eval_type(checker, return_type);
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline void ASTClassSignature::eval_type_impl(
    TypeChecker& checker, TypeResolution& out, bool
) const noexcept {
    // Resolve base class
    const Type* extends_ = [&]() -> const Type* {
        if (owner_->extends_.empty()) {
            return nullptr;
        }
        TypeResolution result;
        try {
            result = checker.lookup_type(owner_->extends_);
        } catch (UnlocatedProblem& e) {
            e.report_at(owner_->location_);
            return TypeRegistry::get_unknown();
        }
        if (!result.is_sized()) {
            Diagnostic::report(CircularTypeDependencyError(owner_->location_));
            return TypeRegistry::get_unknown();
        }
        const Type* type = result;
        if (type->kind_ != Kind::Instance) {
            Diagnostic::report(TypeMismatchError(owner_->location_, "class", type->repr()));
            return TypeRegistry::get_unknown();
        }
        return type->cast<InstanceType>();
    }();
    // Resolve implemented interfaces
    ComparableSpan interfaces =
        owner_->implements_ |
        std::views::transform([&](std::string_view interface_name) -> const Type* {
            TypeResolution result;
            try {
                result = checker.lookup_type(interface_name);
            } catch (UnlocatedProblem& e) {
                e.report_at(owner_->location_);
                return TypeRegistry::get_unknown();
            }
            if (!result.is_sized()) {
                Diagnostic::report(CircularTypeDependencyError(owner_->location_));
                return TypeRegistry::get_unknown();
            }
            const Type* type = result;
            if (type->kind_ != Kind::Interface) {
                Diagnostic::report(TypeMismatchError(owner_->location_, "interface", type->repr()));
                return TypeRegistry::get_unknown();
            }
            return type->cast<InterfaceType>();
        }) |
        GlobalMemory::collect<ComparableSpan<const Type*>>();
    // Collect attributes
    checker.enter(this);
    checker.enter(&owner_->identifier_);
    GlobalMemory::FlatMap<std::string_view, const Type*> attrs =
        owner_->fields_ | std::views::transform([&](const auto& field_decl) {
            std::pair<std::string_view, const Type*> result;
            result.first = field_decl->identifier_;
            TypeResolution field_type;
            field_decl->declared_type_->eval_type(checker, field_type);
            if (!field_type.is_sized()) {
                Diagnostic::report(
                    SymbolCategoryMismatchError(field_decl->declared_type_->location_, true)
                );
                result.second = TypeRegistry::get_unknown();
            } else {
                result.second = field_type;
            }
            return result;
        }) |
        GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
    // Collect methods
    GlobalMemory::Vector non_static_functions =
        owner_->functions_ |
        std::views::filter([](const auto& func_def) { return !func_def->is_static_; }) |
        GlobalMemory::collect<GlobalMemory::Vector<const ASTFunctionDefinition*>>();
    std::ranges::sort(non_static_functions, [](const auto& a, const auto& b) {
        return a->identifier_ < b->identifier_;
    });
    std::ranges::unique(non_static_functions, [](const auto& a, const auto& b) {
        return a->identifier_ == b->identifier_;
    });
    GlobalMemory::FlatMap<std::string_view, FunctionOverloads> methods =
        non_static_functions |
        std::views::transform(
            [&](
                const ASTFunctionDefinition* func_def
            ) -> std::pair<std::string_view, FunctionOverloads> {
                TypeResolution result;
                result = checker.lookup_type(func_def->identifier_);
                if (!result.is_sized()) {
                    Diagnostic::report(CircularTypeDependencyError(func_def->location_));
                    return {func_def->identifier_, {}};
                }
                return {func_def->identifier_, {}};  /// TODO:
            }
        ) |
        GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, FunctionOverloads>>();
    checker.exit();
    out = TypeRegistry::get<InstanceType>(
        checker.current_scope(),
        owner_->identifier_,
        extends_,
        interfaces,
        std::move(attrs),
        std::move(methods)
    );
    checker.exit();
}

static_assert(requires { std::vector<int>(std::declval<const std::vector<int>&>()); });
