#pragma once
#include "pch.hpp"
#include <string_view>
#include <type_traits>
#include <utility>

#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

class CppWriter;

class ASTNode;
class ASTExpression;
using ASTValueExpression = ASTExpression;
using ASTTypeExpression = ASTExpression;
class ASTBlock;
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

struct ExprInfo {
    Type* type;
    bool is_mutable = false;
    bool is_rvalue = false;
};

class Scope {
    friend class TypeChecker;

public:
    static Scope& create(const auto* owner, Scope& parent, std::string_view name = "") {
        std::unique_ptr scope = std::unique_ptr<Scope>(new Scope(parent, name));
        Scope& ref = *scope;
        parent.children_.insert({owner, std::move(scope)});
        return ref;
    }

private:
    Scope* parent_ = nullptr;
    std::string_view name_;
    FlatMap<std::string_view, Object*> variables_;
    FlatMap<std::string_view, const ASTTypeExpression*> types_;
    FlatMap<const void*, std::unique_ptr<Scope>> children_;

private:
    Scope(Scope& parent, std::string_view name) noexcept : parent_(&parent), name_(name) {}

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

    void add_overload(std::string_view identifier, const FunctionValue* func) {
        auto it = variables_.find(identifier);
        if (it == variables_.end()) {
            variables_.insert({identifier, new OverloadedFunctionValue(func)});
        } else {
            Value* existing = it->second->as_value();
            if (existing == nullptr || existing->kind_ != Kind::Intersection) {
                throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
            }
            OverloadedFunctionValue* overloads = static_cast<OverloadedFunctionValue*>(existing);
            overloads->add_overload(func);
        }
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
    FlatMap<std::pair<const Scope*, std::string_view>, CacheRecord> cache_;

public:
    const OpDispatcher& ops_;
    TypeRegistry& types_;

public:
    TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& types) noexcept;
    void add_variable(std::string_view identifier, Object* expr);
    void enter(const ASTBlock* child) noexcept;
    void exit() noexcept;
    Object* resolve(std::string_view identifier);
    ExprInfo type_of(std::string_view identifier);

private:
    Object* resolve_in(std::string_view identifier, Scope& scope);
    ExprInfo type_of_in(std::string_view identifier, Scope& scope);
};

class ASTNode : public MemoryManaged {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
    virtual ~ASTNode() noexcept = default;
    virtual void collect_symbols(Scope& scope, OpDispatcher& ops) {}
    virtual void check_types(TypeChecker& checker) {}
    virtual void transpile(CppWriter& writer) const = 0;
};

class ASTRoot final : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> children_;
    ASTRoot(const Location& loc, std::vector<std::unique_ptr<ASTNode>> children) noexcept
        : ASTNode(loc), children_(std::move(children)) {}
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTBlock : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements_;
    ASTBlock(const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements) noexcept
        : ASTNode(loc), statements_(std::move(statements)) {}
    void collect_symbols(Scope& scope, OpDispatcher& ops) override {
        for (auto& stmt : statements_) {
            stmt->collect_symbols(scope, ops);
        }
    }
    void check_types(TypeChecker& checker) override {
        for (auto& stmt : statements_) {
            stmt->check_types(checker);
        }
    }
    void transpile(CppWriter& writer) const noexcept override;
};

class ASTLocalBlock final : public ASTBlock {
public:
    using ASTBlock::ASTBlock;
    void collect_symbols(Scope& scope, OpDispatcher& ops) final {
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
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}
    virtual Object* eval(TypeChecker& checker) const = 0;
    virtual ExprInfo get_expr_info(TypeChecker& checker) const = 0;
    void check_types(TypeChecker& checker) final { std::ignore = get_expr_info(checker); }
};

class ASTConstant final : public ASTExpression {
public:
    Value* value_;
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    Object* eval(TypeChecker& checker) const final { return value_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final { return {value_->get_type(), false}; }
    void resolve_type(Type* target_type) { value_ = value_->resolve_to(target_type); }
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view str_;
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), str_(name) {}
    Object* eval(TypeChecker& checker) const final { return checker.resolve(str_); }
    ExprInfo get_expr_info(TypeChecker& checker) const final { return checker.type_of(str_); }
    void transpile(CppWriter& writer) const noexcept final;
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTUnaryOp(const Location& loc, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTExpression(loc), expr_(std::move(expr)) {}
    ~ASTUnaryOp() noexcept final = default;
    Object* eval(TypeChecker& checker) const final {
        Object* expr_result = expr_->eval(checker);
        return checker.ops_.eval_op(Op::opcode, expr_result);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo expr_info = expr_->get_expr_info(checker);
        return {checker.ops_.get_result_type(Op::opcode, expr_info.type), false};
    }
    void transpile(CppWriter& writer) const noexcept final;
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
    Object* eval(TypeChecker& checker) const final {
        Object* left_result = left_->eval(checker);
        Object* right_result = right_->eval(checker);
        try {
            return checker.ops_.eval_op(Op::opcode, left_result, right_result);
        } catch (const UnlocatedProblem& e) {
            Type* left_type = left_result->as_type() ? left_result->as_type()
                                                     : left_result->as_value()->get_type();
            Type* right_type = right_result->as_type() ? right_result->as_type()
                                                       : right_result->as_value()->get_type();
            Diagnostic::report(OperationNotDefinedError(
                location_, OperatorCodeToString(Op::opcode), left_type->repr(), right_type->repr()
            ));
            return checker.types_.get_unknown();
        }
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo left_info = left_->get_expr_info(checker);
        ExprInfo right_info = right_->get_expr_info(checker);
        return {
            checker.ops_.get_result_type(Op::opcode, left_info.type, right_info.type), false, true
        };
    }
    void transpile(CppWriter& writer) const noexcept final;
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
    Object* eval(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return checker.types_.get_unknown();
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
        if (!left_info.type->assignable_from(*right_info.type)) {
            Diagnostic::report(
                TypeMismatchError(location_, left_info.type->repr(), right_info.type->repr())
            );
        }
        return {left_info.type, true, false};
    }
    void transpile(CppWriter& writer) const noexcept final;
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
        : ASTExpression(loc), function_(std::move(function)), arguments_(std::move(arguments)) {}
    ~ASTFunctionCall() noexcept final = default;
    Object* eval(TypeChecker& checker) const final {
        // TODO
        return {};
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        // TODO
        return {};
    }
    void transpile(CppWriter& writer) const noexcept final;
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
    Object* eval(TypeChecker& checker) const final { return type_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        throw std::logic_error("Type expressions do not have result types");
    }
    void transpile(CppWriter& writer) const noexcept final;
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
          representation_(Components(std::move(parameter_types), std::move(return_type))) {}
    ASTFunctionType(FunctionType* func) noexcept : ASTTypeExpression({}), representation_(func) {}
    Object* eval(TypeChecker& checker) const final {
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
                return checker.types_.get_unknown()->as_type();
            }) |
            GlobalMemory::collect<ComparableSpan<Type*>>();
        if (any_error) {
            return checker.types_.get_unknown();
        }
        Type* return_type = std::get<1>(comps)->eval(checker)->as_type();
        if (!return_type) {
            Diagnostic::report(SymbolCategoryMismatchError(std::get<1>(comps)->location_, true));
            return checker.types_.get_unknown();
        }
        return checker.types_.get<FunctionType>(param_types, return_type);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {checker.types_.get_unknown()->as_type(), false, true};
    }
    void transpile(CppWriter& writer) const noexcept final;
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
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTRecordType final : public ASTTypeExpression {
public:
    const ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields_;
    ASTRecordType(
        const Location& loc, ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields
    ) noexcept
        : ASTTypeExpression(loc), fields_(std::move(fields)) {}
    ~ASTRecordType() noexcept final = default;
    Object* eval(TypeChecker& checker) const final {
        auto rng = fields_ | std::views::transform([&](const auto& decl) {
                       if (Type* type = checker.resolve(decl->identifier_)->as_type()) {
                           return std::pair(decl->identifier_, type);
                       }
                       throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
                   });
        FlatMap<std::string_view, Type*> field_types(std::from_range, std::move(rng));
        return checker.types_.get<RecordType>(field_types);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {checker.types_.get_unknown()->as_type(), false, true};
    }
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTExpressionStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTExpressionStatement(const Location& loc, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTNode(loc), expr_(std::move(expr)) {}
    ~ASTExpressionStatement() noexcept final = default;
    void check_types(TypeChecker& checker) final { expr_->check_types(checker); }
    void transpile(CppWriter& writer) const noexcept final;
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
            Object* value = expr_->eval(checker);
            if (!value->as_value()) {
                Diagnostic::report(SymbolCategoryMismatchError(expr_->location_, false));
            }
            std::ignore = get_declared_type(checker, value->as_value()->get_type());
            checker.add_variable(identifier_, value);
        } else {
            Type* inferred_type = expr_->get_expr_info(checker).type;
            Type* declared_type = get_declared_type(checker, inferred_type);
            if (auto ast_constant = dynamic_cast<ASTConstant*>(expr_.get())) {
                try {
                    ast_constant->resolve_type(declared_type);
                } catch (UnlocatedProblem& e) {
                    e.report_at(expr_->location_);
                }
            }
            checker.add_variable(identifier_, declared_type);
        }
    }
    Type* get_declared_type(TypeChecker& checker, Type* inferred_type) const {
        if (type_) {
            Type* declared_type = type_->eval(checker)->as_type();
            if (!declared_type) {
                Diagnostic::report(SymbolCategoryMismatchError(type_->location_, true));
            }
            if (!declared_type->assignable_from(*inferred_type)) {
                Diagnostic::report(
                    TypeMismatchError(location_, declared_type->repr(), inferred_type->repr())
                );
            }
            return declared_type;
        } else {
            return inferred_type;
        }
    }
    void transpile(CppWriter& writer) const noexcept final;
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
    void collect_symbols(Scope& scope, OpDispatcher& ops) final {
        scope.add_type(identifier_, type_.get());
    }
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTIfStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTBlock> if_block_;
    const std::unique_ptr<ASTBlock> else_block_;
    ASTIfStatement(
        const Location& loc,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTBlock> if_block,
        std::unique_ptr<ASTBlock> else_block = nullptr
    ) noexcept
        : ASTNode(loc),
          condition_(std::move(condition)),
          if_block_(std::move(if_block)),
          else_block_(std::move(else_block)) {}
    ~ASTIfStatement() noexcept final = default;
    void collect_symbols(Scope& scope, OpDispatcher& ops) final {
        Scope& if_scope = Scope::create(if_block_.get(), scope);
        if_block_->collect_symbols(if_scope, ops);
        if (else_block_) {
            Scope& else_scope = Scope::create(else_block_.get(), scope);
            else_block_->collect_symbols(else_scope, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(if_block_.get());
        ExprInfo cond_info = condition_->get_expr_info(checker);
        if (cond_info.type->kind_ != Kind::Boolean) {
            Diagnostic::report(
                TypeMismatchError(condition_->location_, "bool", cond_info.type->repr())
            );
        }
        if_block_->check_types(checker);
        checker.exit();
        if (else_block_) {
            checker.enter(else_block_.get());
            else_block_->check_types(checker);
            checker.exit();
        }
    }
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTForStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTNode> initializer_;  // Can be either a declaration or an expression
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTExpression> increment_;
    const std::unique_ptr<ASTBlock> body_;
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTDeclaration> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        std::unique_ptr<ASTBlock> body
    ) noexcept
        : ASTNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(std::move(body)) {}
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTValueExpression> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        std::unique_ptr<ASTBlock> body
    ) noexcept
        : ASTNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(std::move(body)) {}
    ASTForStatement(const Location& loc, std::unique_ptr<ASTBlock> body) noexcept
        : ASTNode(loc), body_(std::move(body)) {}
    ~ASTForStatement() noexcept final = default;
    void collect_symbols(Scope& scope, OpDispatcher& ops) final {
        Scope& local_scope = Scope::create(body_.get(), scope);
        if (initializer_) {
            initializer_->collect_symbols(local_scope, ops);
        }
        body_->collect_symbols(local_scope, ops);
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(body_.get());
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
        body_->check_types(checker);
        checker.exit();
    }
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTContinueStatement() noexcept final = default;
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTBreakStatement() noexcept final = default;
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTReturnStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTReturnStatement(const Location& loc, std::unique_ptr<ASTExpression> expr = nullptr) noexcept
        : ASTNode(loc), expr_(std::move(expr)) {}
    ~ASTReturnStatement() noexcept final = default;
    void transpile(CppWriter& writer) const noexcept final;
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
    void transpile(CppWriter& writer) const noexcept final;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    std::string_view identifier_;
    ComparableSpan<std::unique_ptr<ASTFunctionParameter>> parameters_;
    std::unique_ptr<ASTTypeExpression> return_type_;
    std::unique_ptr<ASTBlock> body_;
    bool is_const_;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::string_view identifier,
        ComparableSpan<std::unique_ptr<ASTFunctionParameter>> parameters,
        std::unique_ptr<ASTTypeExpression> return_type,
        std::unique_ptr<ASTBlock> body,
        bool is_const
    ) noexcept
        : ASTNode(loc),
          identifier_(identifier),
          parameters_(std::move(parameters)),
          return_type_(std::move(return_type)),
          body_(std::move(body)),
          is_const_(is_const) {}
    std::generator<ASTNode*> get_children() const noexcept {
        for (const auto& param : parameters_) {
            co_yield param.get();
        }
        co_yield return_type_.get();
        co_yield body_.get();
    }
    void add_overload(Scope& scope) const {
        try {
            const FunctionValue* func =
                new FunctionValue(this, [](auto&& args) { return nullptr; });
            scope.add_overload(identifier_, func);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }
    void collect_symbols(Scope& scope, OpDispatcher& ops) final {
        add_overload(scope);
        Scope& local_scope = Scope::create(body_.get(), scope);
        body_->collect_symbols(local_scope, ops);
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(body_.get());
        for (const auto& param : parameters_) {
            Type* param_type = checker.resolve(param->identifier_)->as_type();
            if (!param_type) {
                Diagnostic::report(SymbolCategoryMismatchError(param->type_->location_, true));
            }
        }
        body_->check_types(checker);
        checker.exit();
    }
    void transpile(CppWriter& writer) const noexcept final;
};

// ===================== Inline implementations of TypeChecker =====================

inline TypeChecker::TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& types) noexcept
    : current_scope_(&root), ops_(ops), types_(types) {}

inline void TypeChecker::add_variable(std::string_view identifier, Object* expr) {
    current_scope_->set_variable(identifier, expr);
}

inline void TypeChecker::enter(const ASTBlock* child) noexcept {
    current_scope_ = current_scope_->children_.at(child).get();
}

inline void TypeChecker::exit() noexcept { current_scope_ = current_scope_->parent_; }

inline Object* TypeChecker::resolve(std::string_view identifier) {
    return resolve_in(identifier, *current_scope_);
}

inline ExprInfo TypeChecker::type_of(std::string_view identifier) {
    return type_of_in(identifier, *current_scope_);
}

inline Object* TypeChecker::resolve_in(std::string_view identifier, Scope& scope) {
    // Check cache
    auto& record = cache_[std::pair(&scope, identifier)];
    if (record.is_resolving) {
        if (record.result) {
            return record.result;
        }
        throw UnlocatedProblem::make<CircularTypeDependencyError>();
    }
    // Cache miss; resolve
    record.is_resolving = true;
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        record.result = it_type->second->eval(*this);
        return record.result;
    }
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        if (it_var->second->as_type()) {
            throw UnlocatedProblem::make<NotConstantExpressionError>();
        } else {
            // constant (it_var->second is the value stored)
            record.result = it_var->second->as_value()->get_type();
            return record.result;
        }
    }
    if (scope.parent_ != nullptr) {
        record.result = resolve_in(identifier, *scope.parent_);
        return record.result;
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}

inline ExprInfo TypeChecker::type_of_in(std::string_view identifier, Scope& scope) {
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        Type* type = it_var->second->as_type();
        if (!type) {
            type = it_var->second->as_value()->get_type();
        }
        return ExprInfo{type, it_var->second->as_type() != nullptr};
    }
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    }
    if (scope.parent_ != nullptr) {
        return type_of_in(identifier, *scope.parent_);
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}
