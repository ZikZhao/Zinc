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

struct ExprInfo {
    Type* type;
    bool is_mutable = false;
    bool is_rvalue = false;
};

struct Resolving {
    Object** cache_value;
};

class Scope {
    friend class TypeChecker;

public:
    static Scope& create(const void* owner, Scope& parent, std::string_view name = "") {
        std::unique_ptr scope = std::unique_ptr<Scope>(new Scope(parent, name));
        Scope& ref = *scope;
        parent.children_.insert({owner, std::move(scope)});
        return ref;
    }

private:
    Scope* parent_ = nullptr;
    GlobalMemory::Map<std::string_view, const ASTTypeExpression*> types_;
    GlobalMemory::Map<std::string_view, Object*> variables_;
    GlobalMemory::Map<std::string_view, GlobalMemory::Vector<const ASTExpression*>> functions_;
    GlobalMemory::Map<const void*, std::unique_ptr<Scope>> children_;
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
        types_.insert({identifier, expr});
    }

    void set_variable(std::string_view identifier, Object* expr) {
        if (types_.contains(identifier)) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
        variables_.insert({identifier, expr});
    }

    void add_function(std::string_view identifier, const ASTExpression* expr) {
        functions_[identifier].push_back(expr);
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
    const OperationHandler& ops_;

public:
    TypeChecker(Scope& root, const OperationHandler& ops) noexcept
        : current_scope_(&root), ops_(ops) {}
    void add_variable(std::string_view identifier, Object* expr) {
        current_scope_->set_variable(identifier, expr);
    }
    void enter(const void* child) noexcept {
        current_scope_ = current_scope_->children_.at(child).get();
    }
    void exit() noexcept { current_scope_ = current_scope_->parent_; }
    std::expected<Object*, Resolving> resolve(std::string_view identifier) {
        return resolve_in(identifier, *current_scope_);
    }
    ExprInfo var_type(std::string_view identifier) {
        return var_type_in(identifier, *current_scope_);
    }
    std::string_view get_current_scope_prefix() const noexcept { return current_scope_->prefix_; }
    bool at_top_level() const noexcept { return current_scope_->parent_ == nullptr; }

private:
    std::expected<Object*, Resolving> resolve_in(std::string_view identifier, Scope& scope);
    ExprInfo var_type_in(std::string_view identifier, Scope& scope);
};

class ASTNode : public MemoryManaged {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
    virtual ~ASTNode() noexcept = default;
    virtual void collect_symbols(Scope& scope, OperationHandler& ops) {}
    virtual void check_types(TypeChecker& checker) {}
    virtual void transpile(Transpiler& transpiler, TypeChecker& checker) const = 0;
};

class ASTRoot final : public ASTNode {
public:
    ComparableSpan<std::unique_ptr<ASTNode>> statements_;
    ASTRoot(const Location& loc, ComparableSpan<std::unique_ptr<ASTNode>> statements) noexcept;
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
    ComparableSpan<std::unique_ptr<ASTNode>> statements_;
    ASTLocalBlock(const Location& loc, ComparableSpan<std::unique_ptr<ASTNode>> statements) noexcept
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
    virtual Object* eval(TypeChecker& checker) const noexcept = 0;
    virtual ExprInfo get_expr_info(TypeChecker& checker) const = 0;
    void check_types(TypeChecker& checker) final { std::ignore = get_expr_info(checker); }
};

/// A hidden type expression that does not appear in source code
class ASTHiddenTypeExpression : public ASTTypeExpression {
public:
    ASTHiddenTypeExpression() noexcept : ASTTypeExpression({}) {}
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        assert(false);
        std::unreachable();
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTConstant final : public ASTExpression {
public:
    Value* value_;
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    Value* eval(TypeChecker& checker) const noexcept final { return value_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final { return {value_->get_type(), false}; }
    void resolve_type(Type* target_type) { value_ = value_->resolve_to(target_type); }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view str_;
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), str_(name) {}
    Object* eval(TypeChecker& checker) const noexcept final {
        try {
            std::expected result = checker.resolve(str_);
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
    ExprInfo get_expr_info(TypeChecker& checker) const final { return checker.var_type(str_); }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTUnaryOp(const Location& loc, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTExpression(loc), expr_(std::move(expr)) {}
    ~ASTUnaryOp() noexcept final = default;
    Object* eval(TypeChecker& checker) const noexcept final {
        Object* expr_result = expr_->eval(checker);
        try {
            return checker.ops_.eval_op(Op::opcode, expr_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return TypeRegistry::get_unknown();
        }
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo expr_info = expr_->get_expr_info(checker);
        return {checker.ops_.get_result_type(Op::opcode, expr_info.type), false};
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

template <typename Op>
class ASTBinaryOp final : public ASTExpression {
public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(
        const Location& loc,
        std::unique_ptr<ASTExpression> left,
        std::unique_ptr<ASTExpression> right
    ) noexcept
        : ASTExpression(loc), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    Object* eval(TypeChecker& checker) const noexcept final {
        Object* left_result = left_->eval(checker);
        Object* right_result = right_->eval(checker);
        try {
            return checker.ops_.eval_op(Op::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return TypeRegistry::get_unknown();
        }
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo left_info = left_->get_expr_info(checker);
        ExprInfo right_info = right_->get_expr_info(checker);
        return {
            checker.ops_.get_result_type(Op::opcode, left_info.type, right_info.type), false, true
        };
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

template <typename InnerOp>
class ASTBinaryOp<OperatorFunctors::OperateAndAssign<InnerOp>> final : public ASTExpression {
private:
    using Op = OperatorFunctors::OperateAndAssign<InnerOp>;

public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(
        const Location& loc,
        std::unique_ptr<ASTExpression> left,
        std::unique_ptr<ASTExpression> right
    ) noexcept
        : ASTExpression(loc), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    Object* eval(TypeChecker& checker) const noexcept final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return TypeRegistry::get_unknown();
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo left_info = left_->get_expr_info(checker);
        ExprInfo right_info = right_->get_expr_info(checker);
        if (!left_info.is_mutable) {
            Diagnostic::report(ImmutableMutationError(location_));
        }
        if (left_info.is_rvalue) {
            Diagnostic::report(InvalidAssignmentTargetError(location_));
        }
        if (!left_info.type->assignable_from(right_info.type)) {
            Diagnostic::report(
                TypeMismatchError(location_, left_info.type->repr(), right_info.type->repr())
            );
        }
        return {left_info.type, true, false};
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionCall final : public ASTExpression {
public:
    const std::unique_ptr<ASTValueExpression> function_;
    const ComparableSpan<std::unique_ptr<ASTValueExpression>> arguments_;
    ASTFunctionCall(
        const Location& loc,
        std::unique_ptr<ASTValueExpression> function,
        ComparableSpan<std::unique_ptr<ASTValueExpression>> arguments
    ) noexcept
        : ASTExpression(loc), function_(std::move(function)), arguments_(arguments) {}
    ~ASTFunctionCall() noexcept final = default;
    Value* eval(TypeChecker& checker) const noexcept final {
        // TODO
        return {};
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
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
    Type* eval(TypeChecker& checker) const noexcept final { return type_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        throw std::logic_error("Type expressions do not have result types");
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionType final : public ASTTypeExpression {
private:
    using Components = std::tuple<
        ComparableSpan<std::unique_ptr<ASTTypeExpression>>,
        std::unique_ptr<ASTTypeExpression>>;

public:
    const std::variant<Components, FunctionType*> representation_;
    ASTFunctionType(
        const Location& loc,
        ComparableSpan<std::unique_ptr<ASTTypeExpression>> parameter_types,
        std::unique_ptr<ASTTypeExpression> return_type
    ) noexcept
        : ASTTypeExpression(loc),
          representation_(Components(parameter_types, std::move(return_type))) {}
    ASTFunctionType(FunctionType* func) noexcept : ASTTypeExpression({}), representation_(func) {}
    Type* eval(TypeChecker& checker) const noexcept final {
        if (std::holds_alternative<FunctionType*>(representation_)) {
            return std::get<FunctionType*>(representation_);
        }
        const auto& comps = std::get<Components>(representation_);
        bool any_error = false;
        ComparableSpan<Type*> param_types =
            std::get<0>(comps) | std::views::transform([&](const auto& param_expr) -> Type* {
                if (Type* param_type = param_expr->eval(checker)->as_type()) {
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
        Type* return_type = std::get<1>(comps)->eval(checker)->as_type();
        if (!return_type) {
            Diagnostic::report(SymbolCategoryMismatchError(std::get<1>(comps)->location_, true));
            return TypeRegistry::get_unknown();
        }
        return TypeRegistry::get<FunctionType>(param_types, return_type);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {TypeRegistry::get_unknown(), false, true};
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFieldDeclaration final : public ASTNode {
public:
    const std::string_view identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    ASTFieldDeclaration(
        const Location& loc, std::string_view identifier, std::unique_ptr<ASTTypeExpression> type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}
    ~ASTFieldDeclaration() noexcept final = default;
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTRecordType final : public ASTTypeExpression {
public:
    const ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields_;
    ASTRecordType(
        const Location& loc, ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields
    ) noexcept
        : ASTTypeExpression(loc), fields_(fields) {}
    ~ASTRecordType() noexcept final = default;
    Type* eval(TypeChecker& checker) const noexcept final {
        auto func = [&](const std::unique_ptr<ASTFieldDeclaration>& decl) {
            try {
                std::expected result = checker.resolve(decl->identifier_);
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
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {TypeRegistry::get_unknown(), false, true};
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTExpressionStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTExpressionStatement(const Location& loc, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTNode(loc), expr_(std::move(expr)) {}
    ~ASTExpressionStatement() noexcept final = default;
    void check_types(TypeChecker& checker) final { expr_->check_types(checker); }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    std::unique_ptr<ASTTypeExpression> type_;
    std::unique_ptr<ASTValueExpression> expr_;
    bool is_mutable_;
    bool is_constant_ = false;
    ASTDeclaration(
        const Location& loc,
        std::string_view identifier,
        std::unique_ptr<ASTTypeExpression> type,
        std::unique_ptr<ASTValueExpression> expr,
        bool is_mutable,
        bool is_constant
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          type_(std::move(type)),
          expr_(std::move(expr)),
          is_mutable_(is_mutable),
          is_constant_(is_constant) {
        assert(!(is_mutable_ && is_constant_));
    }
    ~ASTDeclaration() noexcept final = default;
    void check_types(TypeChecker& checker) final {
        if (is_constant_) {
            Value* value = expr_->eval(checker)->as_value();
            if (!value) {
                Diagnostic::report(SymbolCategoryMismatchError(expr_->location_, false));
            }
            try {
                Value* typed_value = value->resolve_to(value->get_type());
                std::ignore = get_declared_type(checker, typed_value->get_type());
                checker.add_variable(identifier_, typed_value);
            } catch (UnlocatedProblem& e) {
                e.report_at(location_);
            }
        } else {
            Type* inferred_type = expr_->get_expr_info(checker).type;
            Type* declared_type = get_declared_type(checker, inferred_type);
            checker.add_variable(identifier_, declared_type);
        }
    }
    Type* get_declared_type(TypeChecker& checker, Type* inferred_type) const {
        if (type_) {
            Type* declared_type = type_->eval(checker)->as_type();
            if (!declared_type) {
                Diagnostic::report(SymbolCategoryMismatchError(type_->location_, true));
            }
            if (!declared_type->assignable_from(inferred_type)) {
                Diagnostic::report(
                    TypeMismatchError(location_, declared_type->repr(), inferred_type->repr())
                );
            }
            return declared_type;
        } else {
            return inferred_type;
        }
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTTypeAlias final : public ASTNode {
public:
    const std::string_view identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    ASTTypeAlias(
        const Location& loc, std::string_view identifier, std::unique_ptr<ASTTypeExpression> type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}
    ~ASTTypeAlias() noexcept final = default;
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_type(identifier_, type_.get());
    }
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTIfStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> condition_;
    const ComparableSpan<std::unique_ptr<ASTNode>> if_block_;
    const ComparableSpan<std::unique_ptr<ASTNode>> else_block_;
    ASTIfStatement(
        const Location& loc,
        std::unique_ptr<ASTExpression> condition,
        ComparableSpan<std::unique_ptr<ASTNode>> if_block,
        ComparableSpan<std::unique_ptr<ASTNode>> else_block = {}
    ) noexcept
        : ASTNode(loc),
          condition_(std::move(condition)),
          if_block_(if_block),
          else_block_(else_block) {}
    ~ASTIfStatement() noexcept final = default;
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
        ExprInfo cond_info = condition_->get_expr_info(checker);
        if (cond_info.type->kind_ != Kind::Boolean) {
            Diagnostic::report(
                TypeMismatchError(condition_->location_, "bool", cond_info.type->repr())
            );
        }
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
    const std::unique_ptr<ASTNode> initializer_;  // Can be either a declaration or an expression
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTExpression> increment_;
    const ComparableSpan<std::unique_ptr<ASTNode>> body_;
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTDeclaration> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        ComparableSpan<std::unique_ptr<ASTNode>> body
    ) noexcept
        : ASTNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(body) {}
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTValueExpression> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        ComparableSpan<std::unique_ptr<ASTNode>> body
    ) noexcept
        : ASTNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(body) {}
    ASTForStatement(const Location& loc, ComparableSpan<std::unique_ptr<ASTNode>> body) noexcept
        : ASTNode(loc), body_(body) {}
    ~ASTForStatement() noexcept final = default;
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
            ExprInfo cond_info = condition_->get_expr_info(checker);
            if (cond_info.type->kind_ != Kind::Boolean) {
                Diagnostic::report(
                    TypeMismatchError(condition_->location_, "bool", cond_info.type->repr())
                );
            }
        }
        if (increment_) {
            increment_->get_expr_info(checker);
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
    const std::unique_ptr<ASTExpression> expr_;
    ASTReturnStatement(const Location& loc, std::unique_ptr<ASTExpression> expr = nullptr) noexcept
        : ASTNode(loc), expr_(std::move(expr)) {}
    ~ASTReturnStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    std::string_view identifier_;
    std::unique_ptr<ASTExpression> type_;
    ASTFunctionParameter(
        const Location& loc, std::string_view identifier, std::unique_ptr<ASTExpression> type
    ) noexcept
        : ASTNode(loc), identifier_(identifier), type_(std::move(type)) {}
    ~ASTFunctionParameter() noexcept final = default;
    void transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept final;
};

class ASTFunctionSignature final : public ASTHiddenTypeExpression {
public:
    const ASTFunctionDefinition* owner_;
    ASTFunctionSignature(const ASTFunctionDefinition* owner) noexcept : owner_(owner) {}
    Type* eval(TypeChecker& checker) const noexcept final;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    ComparableSpan<std::unique_ptr<ASTFunctionParameter>> parameters_;
    std::unique_ptr<ASTTypeExpression> return_type_;
    ComparableSpan<std::unique_ptr<ASTNode>> body_;
    bool is_const_;
    bool is_static_;
    std::unique_ptr<ASTFunctionSignature> signature_;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::string_view identifier,
        ComparableSpan<std::unique_ptr<ASTFunctionParameter>> parameters,
        std::unique_ptr<ASTTypeExpression> return_type,
        ComparableSpan<std::unique_ptr<ASTNode>> body,
        bool is_const,
        bool is_static
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          parameters_(parameters),
          return_type_(std::move(return_type)),
          body_(body),
          is_const_(is_const),
          is_static_(is_static),
          signature_(std::make_unique<ASTFunctionSignature>(this)) {}
    void collect_symbols(Scope& scope, OperationHandler& ops) final {
        scope.add_function(identifier_, signature_.get());
        Scope& local_scope = Scope::create(&body_, scope);
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(&body_);
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
    Type* eval(TypeChecker& checker) const noexcept final;
};

class ASTClassDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    std::string_view extends_;
    ComparableSpan<std::string_view> implements_;
    ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields_;
    ComparableSpan<std::unique_ptr<ASTTypeAlias>> aliases_;
    ComparableSpan<std::unique_ptr<ASTFunctionDefinition>> functions_;
    ComparableSpan<std::unique_ptr<ASTClassDefinition>> classes_;

    std::unique_ptr<ASTClassSignature> signature_;

public:
    ASTClassDefinition(
        const Location& loc,
        std::string_view identifier,
        std::string_view extends,
        ComparableSpan<std::string_view> implements,
        ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields,
        ComparableSpan<std::unique_ptr<ASTTypeAlias>> aliases,
        ComparableSpan<std::unique_ptr<ASTFunctionDefinition>> functions,
        ComparableSpan<std::unique_ptr<ASTClassDefinition>> classes
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          extends_(extends),
          implements_(implements),
          fields_(fields),
          aliases_(aliases),
          functions_(functions),
          classes_(classes),
          signature_(std::make_unique<ASTClassSignature>(this)) {}
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
    ComparableSpan<std::unique_ptr<ASTNode>> items_;
    ASTNamespaceDefinition(
        const Location& loc,
        std::string_view identifier,
        ComparableSpan<std::unique_ptr<ASTNode>> items
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

// ===================== Inline implementations =====================

inline std::expected<Object*, Resolving> TypeChecker::resolve_in(
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
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        if (it_var->second->as_type()) {
            throw UnlocatedProblem::make<NotConstantExpressionError>();
        } else {
            // constant (it_var->second is the value stored)
            return (record.result = it_var->second->as_value());
        }
    }
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        record.result = it_type->second->eval(*this);
        std::swap(previous_scope, current_scope_);
        return record.result;
    }
    if (auto it_func = scope.functions_.find(identifier); it_func != scope.functions_.end()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        ComparableSpan overloads =
            it_func->second |
            std::views::transform([&](const auto& func_sig) { return func_sig->eval(*this); }) |
            GlobalMemory::collect<ComparableSpan<Object*>>();
        record.result = new OverloadedFunctionValue(overloads);
        std::swap(previous_scope, current_scope_);
        return record.result;
    }
    if (scope.parent_ != nullptr) {
        std::expected<Object*, Resolving> parent_result = resolve_in(identifier, *scope.parent_);
        if (parent_result) {
            record.result = *parent_result;
        }
        return parent_result;
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}

inline ExprInfo TypeChecker::var_type_in(std::string_view identifier, Scope& scope) {
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        Type* type = it_var->second->as_type();
        if (!type) {
            type = it_var->second->as_value()->get_type();
        }
        return ExprInfo{type, it_var->second->as_type() != nullptr};
    }
    if (auto it_func = scope.functions_.find(identifier); it_func != scope.functions_.end()) {
        Scope* previous_scope = &scope;
        std::swap(previous_scope, current_scope_);
        ComparableSpan<Type*> overload_types =
            it_func->second | std::views::transform([this](const ASTExpression* expr) -> Type* {
                Object* obj = expr->eval(*this);
                if (auto type = obj->as_type()) {
                    return type;
                }
                return obj->cast<FunctionValue>()->get_type();
            }) |
            GlobalMemory::collect<ComparableSpan<Type*>>();
        IntersectionType* intersection_type = TypeRegistry::get<IntersectionType>(overload_types);
        std::swap(previous_scope, current_scope_);
        return ExprInfo{intersection_type, false};
    }
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    }
    if (scope.parent_ != nullptr) {
        return var_type_in(identifier, *scope.parent_);
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}

inline ASTRoot::ASTRoot(
    const Location& loc, ComparableSpan<std::unique_ptr<ASTNode>> statements
) noexcept
    : ASTNode(loc), statements_(statements) {
    for (auto& stmt : statements_) {
        if (auto func_decl = dynamic_cast<ASTFunctionDefinition*>(stmt.get())) {
            func_decl->is_static_ = true;
        }
    }
}

inline Type* ASTFunctionSignature::eval(TypeChecker& checker) const noexcept {
    bool any_error = false;
    ComparableSpan params =
        owner_->parameters_ | std::views::transform([&](const auto& param) -> Type* {
            Type* param_type = param->type_->eval(checker)->as_type();
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
    Type* return_type = owner_->return_type_->eval(checker)->as_type();
    if (!return_type) {
        Diagnostic::report(SymbolCategoryMismatchError(owner_->return_type_->location_, true));
        return TypeRegistry::get_unknown();
    }
    /// TODO: handle constexpr functions
    return TypeRegistry::get<FunctionType>(params, return_type);
}

inline Type* ASTClassSignature::eval(TypeChecker& checker) const noexcept {
    // Resolve base class
    Type* extends_ = [&]() -> Type* {
        if (owner_->extends_.empty()) {
            return nullptr;
        }
        std::expected<Object*, Resolving> result;
        try {
            result = checker.resolve(owner_->extends_);
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
                result = checker.resolve(interface_name);
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
            Type* field_type = field_decl->type_->eval(checker)->as_type();
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
        std::views::transform([](const auto& func_def) -> const ASTFunctionDefinition* {
            return func_def.get();
        }) |
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
                std::expected<Object*, Resolving> result = checker.resolve(func_def->identifier_);
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
