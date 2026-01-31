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
using ASTValueExpression = ASTExpression;
using ASTTypeExpression = ASTExpression;
class ASTConstant;
class ASTIdentifier;
template <typename Op>
class ASTUnaryOp;
template <typename Op>
class ASTBinaryOp;
class ASTFunctionCall;
class ASTPrimitiveType;
class ASTRecordType;
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

struct Resolving {
    Object** cache_value;
};

using ScopeValue = PointerVariant<
    const ASTTypeExpression*,                     // type alias
    Term*,                                        // constant/variable/rvalue
    GlobalMemory::Vector<const ASTExpression*>*,  // function overloads
    const ASTTemplateDefinition*>;                // template definition

class Scope : public GlobalMemory::MemoryManaged {
    friend class TypeChecker;

public:
    static Scope& create(const void* owner, Scope& parent, std::string_view name = "") {
        Scope* scope = new Scope(parent, name);
        Scope& ref = *scope;
        parent.children_.insert({owner, scope});
        return ref;
    }

    static Scope& create_temp(Scope& parent) { return *new Scope(parent, ""); }

private:
    Scope* parent_ = nullptr;
    GlobalMemory::Map<std::string_view, ScopeValue> identifiers_;
    GlobalMemory::Map<const void*, Scope*> children_;

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
    void add_type(std::string_view identifier, const ASTTypeExpression* expr) {
        auto [_, inserted] = identifiers_.insert({identifier, expr});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void set_variable(std::string_view identifier, Term term, bool is_mutable) {
        auto [_, inserted] = identifiers_.insert({identifier, new Term(term)});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_function(std::string_view identifier, const ASTExpression* expr) {
        if (!identifiers_.contains(identifier)) {
            auto overloads = GlobalMemory::alloc<GlobalMemory::Vector<const ASTExpression*>>();
            overloads->push_back(expr);
            identifiers_[identifier] = overloads;
        } else {
            auto it = identifiers_.find(identifier);
            if (it != identifiers_.end() &&
                !it->second.get<GlobalMemory::Vector<const ASTExpression*>*>()) {
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

class TypeChecker {
private:
    struct CacheRecord {
        bool is_resolving = false;
        Object* result;
    };

private:
    Scope* current_scope_;
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, CacheRecord> cache_;

public:
    OperationHandler& ops_;

public:
    TypeChecker(Scope& root, OperationHandler& ops) noexcept : current_scope_(&root), ops_(ops) {}
    void add_variable(std::string_view identifier, Term term, bool is_mutable) {
        current_scope_->set_variable(identifier, term, is_mutable);
    }
    void enter(const void* child) noexcept { current_scope_ = current_scope_->children_.at(child); }
    void exit() noexcept { current_scope_ = current_scope_->parent_; }
    Scope* get_current_scope() noexcept { return current_scope_; }
    std::expected<Object*, Resolving> lookup_static(std::string_view identifier) {
        return lookup_static_in(identifier, *current_scope_);
    }
    Term lookup_term(std::string_view identifier) {
        return lookup_term_in(identifier, *current_scope_);
    }
    bool is_at_top_level() const noexcept { return current_scope_->parent_ == nullptr; }

private:
    std::expected<Object*, Resolving> lookup_static_in(std::string_view identifier, Scope& scope);
    Term lookup_term_in(std::string_view identifier, Scope& scope);
};

class ASTNode : public GlobalMemory::MemoryManaged {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
    virtual ~ASTNode() noexcept = default;
    virtual void collect_symbols(Scope& scope, OperationHandler& ops) {}
    virtual void check_types(TypeChecker& checker) {}
    virtual void transpile(Transpiler& transpiler, TypeChecker& checker) const = 0;
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTExpression : public ASTNode {
protected:
    static bool expect_type(std::expected<Object*, Resolving> result, const Location& loc) {
        if (result) {
            Object* actual = *result;
            if (actual->as_type()) {
                return true;
            } else {
                Diagnostic::report(SymbolCategoryMismatchError(loc, true));
            }
        } else {
            Diagnostic::report(CircularTypeDependencyError(loc));
        }
        return false;
    }

public:
    ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}
    virtual Object* eval_static(TypeChecker& checker) const noexcept = 0;
    virtual Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept = 0;
    void check_types(TypeChecker& checker) final {
        std::ignore = resolve_term(checker, nullptr, false);
    }
};

/// A hidden type expression that does not appear in source code
class ASTHiddenTypeExpression : public ASTTypeExpression {
public:
    ASTHiddenTypeExpression() noexcept : ASTTypeExpression({}) {}
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        UNREACHABLE();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTConstant final : public ASTExpression {
public:
    Value* value_;
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    Value* eval_static(TypeChecker& checker) const noexcept final { return value_; }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        if (expected) {
            try {
                Value* typed_value = value_->resolve_to(expected);
                return Term::from_const(typed_value);
            } catch (UnlocatedProblem& e) {
                e.report_at(location_);
                return Term::unknown();
            }
        }
        return Term::from_const(value_);
    }
    void resolve_type(Type* target_type) { value_ = value_->resolve_to(target_type); }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view str_;
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), str_(name) {}
    Object* eval_static(TypeChecker& checker) const noexcept final {
        try {
            std::expected result = checker.lookup_static(str_);
            if (result) {
                return *result;
            } else {
                Diagnostic::report(CircularTypeDependencyError(location_));
                return TypeRegistry::get_unknown();
            }
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return TypeRegistry::get_unknown();
        }
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        try {
            Term term = checker.lookup_term(str_);
            if (expected_const && !term.is_const()) {
                Diagnostic::report(NotConstantExpressionError(location_));
                return Term::unknown();
            }
            return term;
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return Term::unknown();
        }
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    ASTExpression* const expr_;
    ASTUnaryOp(const Location& loc, ASTExpression* expr) noexcept
        : ASTExpression(loc), expr_(expr) {}
    Object* eval_static(TypeChecker& checker) const noexcept final {
        Object* expr_result = expr_->eval_static(checker);
        try {
            return checker.ops_.eval_type_op(Op::opcode, expr_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return TypeRegistry::get_unknown();
        }
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        Term expr_term = expr_->resolve_term(checker, expected, expected_const);
        return checker.ops_.eval_value_op(Op::opcode, expr_term);
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

template <typename Op>
class ASTBinaryOp final : public ASTExpression {
public:
    ASTExpression* const left_;
    ASTExpression* const right_;
    ASTBinaryOp(const Location& loc, ASTExpression* left, ASTExpression* right) noexcept
        : ASTExpression(loc), left_(left), right_(right) {}
    Object* eval_static(TypeChecker& checker) const noexcept final {
        Object* left_result = left_->eval_static(checker);
        Object* right_result = right_->eval_static(checker);
        try {
            return checker.ops_.eval_type_op(Op::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return TypeRegistry::get_unknown();
        }
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        Term left_term = left_->resolve_term(checker, expected, expected_const);
        Term right_term = right_->resolve_term(checker, expected, expected_const);
        try {
            return checker.ops_.eval_value_op(Op::opcode, left_term, right_term);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return Term::unknown();
        }
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionCall final : public ASTExpression {
public:
    ASTValueExpression* const function_;
    const ComparableSpan<ASTValueExpression*> arguments_;
    ASTFunctionCall(
        const Location& loc,
        ASTValueExpression* function,
        ComparableSpan<ASTValueExpression*> arguments
    ) noexcept
        : ASTExpression(loc), function_(function), arguments_(arguments) {}
    Value* eval_static(TypeChecker& checker) const noexcept final {
        // TODO
        return {};
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        // TODO
        return {};
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
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

class ASTPrimitiveType final : public ASTTypeExpression {
public:
    Type* type_;
    ASTPrimitiveType(const Location& loc, Type* type) noexcept
        : ASTTypeExpression(loc), type_(type) {}
    Type* eval_static(TypeChecker& checker) const noexcept final { return type_; }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, true));
        return Term::unknown();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionType final : public ASTTypeExpression {
private:
    using Components = std::tuple<ComparableSpan<ASTTypeExpression*>, ASTTypeExpression*>;

public:
    const std::variant<Components, FunctionType*> representation_;
    ASTFunctionType(
        const Location& loc,
        ComparableSpan<ASTTypeExpression*> parameter_types,
        ASTTypeExpression* return_type
    ) noexcept
        : ASTTypeExpression(loc), representation_(Components(parameter_types, return_type)) {}
    ASTFunctionType(FunctionType* func) noexcept : ASTTypeExpression({}), representation_(func) {}
    Type* eval_static(TypeChecker& checker) const noexcept final {
        if (std::holds_alternative<FunctionType*>(representation_)) {
            return std::get<FunctionType*>(representation_);
        }
        const auto& comps = std::get<Components>(representation_);
        bool any_error = false;
        ComparableSpan<Type*> param_types =
            std::get<0>(comps) | std::views::transform([&](ASTTypeExpression* param_expr) -> Type* {
                if (Type* param_type = param_expr->eval_static(checker)->as_type()) {
                    return param_type;
                }
                Diagnostic::report(SymbolCategoryMismatchError(param_expr->location_, true));
                any_error = true;
                return TypeRegistry::get_unknown();
            }) |
            GlobalMemory::collect<ComparableSpan<Type*>>();
        if (any_error) {
            return TypeRegistry::get_unknown();
        }
        Type* return_type = std::get<1>(comps)->eval_static(checker)->as_type();
        if (!return_type) {
            Diagnostic::report(SymbolCategoryMismatchError(std::get<1>(comps)->location_, true));
            return TypeRegistry::get_unknown();
        }
        return TypeRegistry::get<FunctionType>(param_types, return_type);
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, true));
        return Term::unknown();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFieldDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    ASTTypeExpression* type_;
    ASTFieldDeclaration(
        const Location& loc, std::string_view identifier, ASTTypeExpression* type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(type) {}
    void check_types(TypeChecker& checker) final {
        Type* field_type = type_->eval_static(checker)->as_type();
        if (!field_type) {
            Diagnostic::report(SymbolCategoryMismatchError(type_->location_, true));
        }
        checker.add_variable(identifier_, Term::from_var(field_type), true);
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTRecordType final : public ASTTypeExpression {
public:
    ComparableSpan<ASTFieldDeclaration*> fields_;
    ASTRecordType(const Location& loc, ComparableSpan<ASTFieldDeclaration*> fields) noexcept
        : ASTTypeExpression(loc), fields_(fields) {}
    Type* eval_static(TypeChecker& checker) const noexcept final {
        auto func = [&](ASTFieldDeclaration* decl) {
            try {
                std::expected result = checker.lookup_static(decl->identifier_);
                if (!result) {
                    Diagnostic::report(CircularTypeDependencyError(decl->location_));
                    return std::pair(decl->identifier_, TypeRegistry::get_unknown());
                } else if (Type* type = (*result)->as_type()) {
                    return std::pair(decl->identifier_, type);
                } else {
                    Diagnostic::report(SymbolCategoryMismatchError(decl->location_, true));
                    return std::pair(decl->identifier_, TypeRegistry::get_unknown());
                }
            } catch (UnlocatedProblem& e) {
                e.report_at(decl->location_);
                return std::pair(decl->identifier_, TypeRegistry::get_unknown());
            }
        };
        auto rng = fields_ | std::views::transform(func);
        GlobalMemory::Map<std::string_view, Type*> field_types(std::from_range, std::move(rng));
        return TypeRegistry::get<RecordType>(field_types);
    }
    Term resolve_term(
        TypeChecker& checker, Type* expected, bool expected_const
    ) const noexcept final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, true));
        return Term::unknown();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTExpressionStatement final : public ASTNode {
public:
    ASTExpression* const expr_;
    ASTExpressionStatement(const Location& loc, ASTExpression* expr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void check_types(TypeChecker& checker) final { expr_->check_types(checker); }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    ASTTypeExpression* type_;
    ASTValueExpression* expr_;
    bool is_mutable_;
    bool is_constant_;
    ASTDeclaration(
        const Location& loc,
        std::string_view identifier,
        ASTTypeExpression* type,
        ASTValueExpression* expr,
        bool is_mutable,
        bool is_constant
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          type_(type),
          expr_(expr),
          is_mutable_(is_mutable),
          is_constant_(is_constant) {
        assert(!(is_mutable_ && is_constant_));
    }
    void check_types(TypeChecker& checker) final {
        Type* declared_type = nullptr;
        if (type_) {
            declared_type = type_->eval_static(checker)->as_type();
            if (!declared_type) {
                Diagnostic::report(SymbolCategoryMismatchError(type_->location_, true));
            }
        }
        Term term;
        try {
            term = expr_->resolve_term(checker, declared_type, is_constant_);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            term = Term::unknown();
        }
        try {
            checker.add_variable(identifier_, term, is_mutable_);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTTypeAlias final : public ASTNode, public ASTTemplateTarget {
public:
    const std::string_view identifier_;
    ASTTypeExpression* const type_;
    ASTTypeAlias(const Location& loc, std::string_view identifier, ASTTypeExpression* type) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(type) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_type(identifier_, type_);
    }
    ASTNode* as_node() noexcept final { return this; }
    std::string_view get_template_name() const noexcept final { return identifier_; }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
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
        condition_->resolve_term(checker, &BooleanType::instance, false);
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTForStatement final : public ASTNode {
public:
    ASTNode* const initializer_;  // Can be either a declaration or an expression
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
        ASTValueExpression* initializer,
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
            condition_->resolve_term(checker, &BooleanType::instance, false);
        }
        if (increment_) {
            increment_->resolve_term(checker, nullptr, false);
        }
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTContinueStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTBreakStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTReturnStatement final : public ASTNode {
public:
    ASTExpression* const expr_;
    ASTReturnStatement(const Location& loc, ASTExpression* expr = nullptr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void check_types(TypeChecker& checker) final {
        if (expr_) expr_->resolve_term(checker, nullptr, false);
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* type_;
    ASTFunctionParameter(
        const Location& loc, std::string_view identifier, ASTExpression* type
    ) noexcept
        : ASTNode(loc), identifier_(identifier), type_(type) {}
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionSignature final : public ASTHiddenTypeExpression {
public:
    const ASTFunctionDefinition* owner_;
    ASTFunctionSignature(const ASTFunctionDefinition* owner) noexcept : owner_(owner) {}
    Type* eval_static(TypeChecker& checker) const noexcept final;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    ComparableSpan<ASTFunctionParameter*> parameters_;
    ASTTypeExpression* return_type_;
    ComparableSpan<ASTNode*> body_;
    bool is_const_;
    bool is_static_;
    ASTFunctionSignature* signature_;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::string_view identifier,
        ComparableSpan<ASTFunctionParameter*> parameters,
        ASTTypeExpression* return_type,
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
            Type* param_type = param->type_->eval_static(checker)->as_type();
            if (!param_type) {
                Diagnostic::report(SymbolCategoryMismatchError(param->type_->location_, true));
                param_type = TypeRegistry::get_unknown();
            }
            checker.add_variable(param->identifier_, Term::from_var(param_type), false);
        }
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTClassSignature final : public ASTHiddenTypeExpression {
public:
    const ASTClassDefinition* owner_;
    ASTClassSignature(const ASTClassDefinition* owner) noexcept : owner_(owner) {}
    Type* eval_static(TypeChecker& checker) const noexcept final;
};

class ASTClassDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    std::string_view extends_;
    ComparableSpan<std::string_view> implements_;
    ComparableSpan<ASTFieldDeclaration*> fields_;
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
        ComparableSpan<ASTFieldDeclaration*> fields,
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTTemplateTypeArgument final : public ASTHiddenTypeExpression {
public:
    Type* type_;
    ASTTemplateTypeArgument(Type* type) noexcept : type_(type) {}
    Type* eval_static(TypeChecker& checker) const noexcept final { return type_; }
};

class ASTTemplateDefinition final : public ASTNode {
public:
    ASTTemplateTarget* target_;
    ComparableSpan<std::pair<std::string_view, ASTTypeExpression*>> parameters_;

public:
    ASTTemplateDefinition(
        const Location& loc,
        ASTTemplateTarget* target,
        ComparableSpan<std::pair<std::string_view, ASTTypeExpression*>> parameters
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
        Scope& template_scope = Scope::create(this, *checker.get_current_scope());
        for (size_t i = 0; i < parameters_.size(); ++i) {
            const auto& [param_name, param_type] = parameters_[i];
            Object* argument = arguments[i];
            if ((param_type == nullptr) != (argument->as_type() != nullptr)) {
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
                Type* constraint_type = param_type->eval_static(checker)->as_type();
                if (!constraint_type) {
                    Diagnostic::report(SymbolCategoryMismatchError(param_type->location_, true));
                    return new Term(Term::unknown());
                }
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
                    param_name, Term::from_const(argument->cast<Value>()), false
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
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

// ===================== Inline implementations =====================

inline std::expected<Object*, Resolving> TypeChecker::lookup_static_in(
    std::string_view identifier, Scope& scope
) {
    // Check cache
    auto& record = cache_[std::pair(&scope, identifier)];
    if (record.is_resolving) {
        if (record.result) {
            return record.result;
        }
        return std::unexpected(Resolving{&record.result});
    }
    // Cache miss; resolve
    record.is_resolving = true;
    auto it = scope.identifiers_.find(identifier);
    if (it == scope.identifiers_.end()) {
        if (scope.parent_ != nullptr) {
            std::expected<Object*, Resolving> parent_result =
                lookup_static_in(identifier, *scope.parent_);
            if (parent_result) {
                record.result = *parent_result;
            }
            return parent_result;
        }
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    } else if (auto type = it->second.get<const ASTTypeExpression*>()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        record.result = type->eval_static(*this);
        std::swap(previous_scope, current_scope_);
        return record.result;
    } else if (auto term = it->second.get<Term*>()) {
        if ((*term)->as_type()) {
            // not a constant
            throw UnlocatedProblem::make<NotConstantExpressionError>();
        } else {
            return (record.result = (*term)->cast<Value>());
        }
    } else if (auto func = it->second.get<GlobalMemory::Vector<const ASTExpression*>*>()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        ComparableSpan<Object*> overloads =
            *func | std::views::transform([this](const ASTExpression* func_sig) {
                return func_sig->eval_static(*this);
            }) |
            GlobalMemory::collect<ComparableSpan<Object*>>();
        record.result = new OverloadedFunctionValue(overloads);
        std::swap(previous_scope, current_scope_);
        return record.result;
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
        Type* type = (*term)->as_type();
        if (!type) {
            type = (*term)->cast<Value>()->get_type();
        }
        return *term;
    } else if (auto func = it->second.get<GlobalMemory::Vector<const ASTExpression*>*>()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        ComparableSpan<Type*> overload_types =
            *func | std::views::transform([this](const ASTExpression* expr) -> Type* {
                Object* obj = expr->eval_static(*this);
                if (auto type = obj->as_type()) {
                    return type;
                }
                return obj->cast<FunctionValue>()->get_type();
            }) |
            GlobalMemory::collect<ComparableSpan<Type*>>();
        IntersectionType* intersection_type = TypeRegistry::get<IntersectionType>(overload_types);
        std::swap(previous_scope, current_scope_);
        return Term::from_var(intersection_type);
    } else if (it->second.get<const ASTTypeExpression*>()) {
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

inline Type* ASTFunctionSignature::eval_static(TypeChecker& checker) const noexcept {
    bool any_error = false;
    ComparableSpan params =
        owner_->parameters_ | std::views::transform([&](const auto& param) -> Type* {
            Type* param_type = param->type_->eval_static(checker)->as_type();
            if (!param_type) {
                Diagnostic::report(SymbolCategoryMismatchError(param->type_->location_, true));
                any_error = true;
                return nullptr;
            }
            return param_type;
        }) |
        GlobalMemory::collect<ComparableSpan<Type*>>();
    if (any_error) {
        return TypeRegistry::get_unknown();
    }
    Type* return_type = owner_->return_type_->eval_static(checker)->as_type();
    if (!return_type) {
        Diagnostic::report(SymbolCategoryMismatchError(owner_->return_type_->location_, true));
        return TypeRegistry::get_unknown();
    }
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline Type* ASTClassSignature::eval_static(TypeChecker& checker) const noexcept {
    // Resolve base class
    Type* extends_ = [&]() -> Type* {
        if (owner_->extends_.empty()) {
            return nullptr;
        }
        std::expected<Object*, Resolving> result;
        try {
            result = checker.lookup_static(owner_->extends_);
        } catch (UnlocatedProblem& e) {
            e.report_at(owner_->location_);
            return TypeRegistry::get_unknown();
        }
        if (!expect_type(result, owner_->location_)) {
            return TypeRegistry::get_unknown();
        }
        if ((*result)->as_type()->kind_ != Kind::Instance) {
            Diagnostic::report(
                TypeMismatchError(owner_->location_, "class", (*result)->as_type()->repr())
            );
            return TypeRegistry::get_unknown();
        }
        return (*result)->cast<ClassType>();
    }();
    // Resolve implemented interfaces
    ComparableSpan interfaces =
        owner_->implements_ | std::views::transform([&](std::string_view interface_name) -> Type* {
            std::expected<Object*, Resolving> result;
            try {
                result = checker.lookup_static(interface_name);
            } catch (UnlocatedProblem& e) {
                e.report_at(owner_->location_);
                return TypeRegistry::get_unknown();
            }
            if (!expect_type(result, owner_->location_)) {
                return TypeRegistry::get_unknown();
            }
            if ((*result)->as_type()->kind_ != Kind::Interface) {
                Diagnostic::report(
                    TypeMismatchError(owner_->location_, "interface", (*result)->as_type()->repr())
                );
                return TypeRegistry::get_unknown();
            }
            return (*result)->cast<InterfaceType>();
        }) |
        GlobalMemory::collect<ComparableSpan<Type*>>();
    // Collect attributes
    checker.enter(this);
    checker.enter(&owner_->identifier_);
    GlobalMemory::Map<std::string_view, Type*> attrs =
        owner_->fields_ | std::views::transform([&](const auto& field_decl) {
            std::pair<std::string_view, Type*> result;
            result.first = field_decl->identifier_;
            Type* field_type = field_decl->type_->eval_static(checker)->as_type();
            if (!field_type) {
                Diagnostic::report(SymbolCategoryMismatchError(field_decl->type_->location_, true));
                result.second = TypeRegistry::get_unknown();
            } else {
                result.second = field_type;
            }
            return result;
        }) |
        GlobalMemory::collect<GlobalMemory::Map<std::string_view, Type*>>();
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
    GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> methods =
        non_static_functions |
        std::views::transform(
            [&](auto func_def) -> std::pair<std::string_view, OverloadedFunctionValue*> {
                std::expected<Object*, Resolving> result =
                    checker.lookup_static(func_def->identifier_);
                return {
                    func_def->identifier_, (*result)->as_value()->cast<OverloadedFunctionValue>()
                };
            }
        ) |
        GlobalMemory::collect<GlobalMemory::Map<std::string_view, OverloadedFunctionValue*>>();
    checker.exit();
    checker.exit();
    return TypeRegistry::get<ClassType>(owner_->identifier_, extends_, interfaces, attrs, methods);
}
