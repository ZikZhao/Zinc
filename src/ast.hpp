#pragma once
#include "pch.hpp"
#include <string_view>
#include <type_traits>
#include <utility>

#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

class ASTNode;
class ASTExpression;
using ASTValueExpression = ASTExpression;
using ASTTypeExpression = ASTExpression;
class ASTCodeBlock;
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
    TypePtr type;
    bool is_mutable = false;
    bool is_rvalue = false;
};

class Scope {
    friend class TypeChecker;

private:
    Scope* const parent_ = nullptr;
    const std::string_view name_;
    FlatMap<std::string_view, ObjectPtr> variables_;
    FlatMap<std::string_view, const ASTTypeExpression*> types_;
    std::vector<const Scope*> anonymous_children_;
    FlatMap<std::string_view, const Scope*> children_;

public:
    Scope() noexcept = default;
    Scope(std::string_view name, Scope& parent) noexcept : parent_(&parent), name_(name) {
        if (name.empty()) {
            parent_->anonymous_children_.push_back(this);
        } else {
            parent_->children_.insert(name, this);
        }
    }
    void add_type(std::string_view identifier, const ASTTypeExpression* expr) {
        types_.insert(identifier, expr);
    }

private:
    void add_variable(std::string_view identifier, ObjectPtr expr) {
        if (types_.contains(identifier)) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
        variables_.insert(identifier, expr);
    }
};

class TypeChecker {
private:
    struct CacheRecord {
        bool is_resolving = false;
        ObjectPtr result;
    };

private:
    Scope* current_scope_;
    FlatMap<std::pair<const Scope*, std::string_view>, CacheRecord> cache_;

public:
    const OpDispatcher& ops_;
    TypeRegistry& types_;

public:
    TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& types) noexcept;
    void add_variable(std::string_view identifier, ObjectPtr expr);
    void enter(const ASTCodeBlock* child) noexcept;
    void exit() noexcept;
    ObjectPtr resolve(std::string_view identifier);
    ExprInfo type_of(std::string_view identifier);

private:
    ObjectPtr resolve_in(std::string_view identifier, Scope& scope);
    ExprInfo type_of_in(std::string_view identifier, Scope& scope);
};

class ASTNode : public MemoryManaged {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
    virtual ~ASTNode() noexcept = default;
    virtual void collect_types(Scope& scope, OpDispatcher& ops) {}
    virtual void check_types(TypeChecker& checker) {}
};

class ASTRecursiveNode : public ASTNode {
public:
    ASTRecursiveNode(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTRecursiveNode() noexcept override = default;
    virtual std::generator<ASTNode*> get_children() const noexcept = 0;
    void collect_types(Scope& scope, OpDispatcher& ops) override {
        for (const auto& child : get_children()) {
            if (child == nullptr) {
                continue;
            }
            child->collect_types(scope, ops);
        }
    }
    void check_types(TypeChecker& checker) override {
        for (const auto& child : get_children()) {
            if (child == nullptr) {
                continue;
            }
            child->check_types(checker);
        }
    }
};

class ASTRoot final : public ASTRecursiveNode {
public:
    std::vector<std::unique_ptr<ASTNode>> children_;
    ASTRoot(const Location& loc, std::vector<std::unique_ptr<ASTNode>> children) noexcept
        : ASTRecursiveNode(loc), children_(std::move(children)) {}
    std::generator<ASTNode*> get_children() const noexcept final {
        for (const auto& child : children_) {
            co_yield child.get();
        }
    }
};

class ASTCodeBlock final : public ASTRecursiveNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements_;
    std::unique_ptr<Scope> local_scope_;
    std::string_view name_;
    ASTCodeBlock(const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements) noexcept
        : ASTRecursiveNode(loc), statements_(std::move(statements)) {}
    ASTCodeBlock(
        const Location& loc, std::string_view name, std::vector<std::unique_ptr<ASTNode>> statements
    ) noexcept
        : ASTRecursiveNode(loc), statements_(std::move(statements)), name_(name) {}
    std::generator<ASTNode*> get_children() const noexcept final {
        for (const auto& stmt : statements_) {
            co_yield stmt.get();
        }
    }
    void collect_types(Scope& scope, OpDispatcher& ops) final {
        local_scope_ = std::make_unique<Scope>(name_, scope);
        for (auto& stmt : statements_) {
            stmt->collect_types(*local_scope_, ops);
        }
    }
    void check_types(TypeChecker& checker) final {
        checker.enter(this);
        for (auto& stmt : statements_) {
            stmt->check_types(checker);
        }
        checker.exit();
    }
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}
    virtual ObjectPtr eval(TypeChecker& checker) const = 0;
    virtual ExprInfo get_expr_info(TypeChecker& checker) const = 0;
    void check_types(TypeChecker& checker) final { std::ignore = get_expr_info(checker); }
};

class ASTConstant final : public ASTExpression {
public:
    ValuePtr value_;
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    ObjectPtr eval(TypeChecker& checker) const final { return value_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final { return {value_->get_type(), false}; }
    void resolve_type(TypePtr target_type) { value_ = value_->resolve_to(target_type); }
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view name_;
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), name_(name) {}
    ObjectPtr eval(TypeChecker& checker) const final { return checker.resolve(name_); }
    ExprInfo get_expr_info(TypeChecker& checker) const final { return checker.type_of(name_); }
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTUnaryOp(const Location& loc, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTExpression(loc), expr_(std::move(expr)) {}
    ~ASTUnaryOp() noexcept final = default;
    ObjectPtr eval(TypeChecker& checker) const final {
        ObjectPtr expr_result = expr_->eval(checker);
        return checker.ops_.eval_op(Op::opcode, expr_result);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        ExprInfo expr_info = expr_->get_expr_info(checker);
        return {checker.ops_.get_result_type(Op::opcode, expr_info.type), false};
    }
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
    ObjectPtr eval(TypeChecker& checker) const final {
        ObjectPtr left_result = left_->eval(checker);
        ObjectPtr right_result = right_->eval(checker);
        try {
            return checker.ops_.eval_op(Op::opcode, left_result, right_result);
        } catch (const UnlocatedProblem& e) {
            TypePtr left_type = left_result->as_type() ? left_result->as_type()
                                                       : left_result->as_value()->get_type();
            TypePtr right_type = right_result->as_type() ? right_result->as_type()
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
    ObjectPtr eval(TypeChecker& checker) const final {
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
};

class ASTFunctionCall final : public ASTExpression {
public:
    const std::unique_ptr<ASTValueExpression> function_;
    const std::vector<std::unique_ptr<ASTValueExpression>> arguments_;
    ASTFunctionCall(
        const Location& loc,
        std::unique_ptr<ASTValueExpression> function,
        std::vector<std::unique_ptr<ASTValueExpression>> arguments
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
    TypePtr type_;
    ASTPrimitiveType(const Location& loc, TypePtr type) noexcept
        : ASTTypeExpression(loc), type_(type) {}
    ObjectPtr eval(TypeChecker& checker) const final { return type_; }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        throw std::logic_error("Type expressions do not have result types");
    }
};

class ASTFunctionType final : public ASTTypeExpression {
private:
    using Components = std::tuple<
        std::unique_ptr<ASTTypeExpression>,
        ComparableSpan<std::unique_ptr<ASTTypeExpression>>,
        std::unique_ptr<ASTTypeExpression>>;

public:
    const std::variant<Components, Type*> representation_;
    ASTFunctionType(
        const Location& loc,
        std::unique_ptr<ASTTypeExpression> return_type,
        ComparableSpan<std::unique_ptr<ASTTypeExpression>> parameter_types,
        std::unique_ptr<ASTTypeExpression> variadic_type
    ) noexcept
        : ASTTypeExpression(loc),
          representation_(Components(
              std::move(return_type), std::move(parameter_types), std::move(variadic_type)
          )) {}
    ASTFunctionType(Type* func) noexcept : ASTTypeExpression({}), representation_(func) {}
    ObjectPtr eval(TypeChecker& checker) const final {
        if (std::holds_alternative<Type*>(representation_)) {
            return std::get<Type*>(representation_);
        }
        const auto& comps = std::get<Components>(representation_);
        if (TypePtr return_type = std::get<0>(comps)->eval(checker)->as_type()) {
            ComparableSpan<TypePtr> param_types =
                std::get<1>(comps) | std::views::transform([&](const auto& param_expr) -> TypePtr {
                    if (TypePtr param_type = param_expr->eval(checker)->as_type()) {
                        return param_type;
                    }
                    Diagnostic::report(SymbolCategoryMismatchError(param_expr->location_, true));
                    return checker.types_.get_unknown()->as_type();
                }) |
                GlobalMemory::collect<ComparableSpan<TypePtr>>();
            TypePtr variadic_type;
            if (const auto& opt_variadic = std::get<2>(comps)) {
                if (variadic_type = opt_variadic->eval(checker)->as_type(); variadic_type) {
                    throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
                }
            }
            return checker.types_.get<FunctionType>(param_types, variadic_type, return_type);
        }
        Diagnostic::report(SymbolCategoryMismatchError(std::get<0>(comps)->location_, true));
        return checker.types_.get_unknown();
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {checker.types_.get_unknown()->as_type(), false, true};
    }
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
};

class ASTRecordType final : public ASTTypeExpression {
public:
    const std::vector<std::unique_ptr<ASTFieldDeclaration>> fields_;
    ASTRecordType(
        const Location& loc, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
    ) noexcept
        : ASTTypeExpression(loc), fields_(std::move(fields)) {}
    ~ASTRecordType() noexcept final = default;
    ObjectPtr eval(TypeChecker& checker) const final {
        auto rng = fields_ | std::views::transform([&](const auto& decl) {
                       if (TypePtr type = checker.resolve(decl->identifier_)->as_type()) {
                           return std::pair(decl->identifier_, type);
                       }
                       throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
                   });
        FlatMap<std::string_view, TypePtr> field_types(std::from_range, std::move(rng));
        return checker.types_.get<RecordType>(field_types);
    }
    ExprInfo get_expr_info(TypeChecker& checker) const final {
        Diagnostic::report(SymbolCategoryMismatchError(location_, false));
        return {checker.types_.get_unknown()->as_type(), false, true};
    }
};

class ASTDeclaration final : public ASTNode {
public:
    const std::unique_ptr<ASTIdentifier> identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    const std::unique_ptr<ASTValueExpression> expr_;
    const bool is_mutable_;
    const bool is_constant_ = false;
    ASTDeclaration(
        const Location& loc,
        std::unique_ptr<ASTIdentifier> identifier,
        std::unique_ptr<ASTTypeExpression> type,
        std::unique_ptr<ASTValueExpression> expr,
        bool is_mutable,
        bool is_constant
    ) noexcept
        : ASTNode(loc),
          identifier_(std::move(identifier)),
          type_(std::move(type)),
          expr_(std::move(expr)),
          is_mutable_(is_mutable),
          is_constant_(is_constant) {
        assert(!(is_mutable_ && is_constant_));
    }
    ~ASTDeclaration() noexcept final = default;
    void check_types(TypeChecker& checker) final {
        if (is_constant_) {
            ObjectPtr value = expr_->eval(checker);
            if (!value->as_value()) {
                Diagnostic::report(SymbolCategoryMismatchError(expr_->location_, false));
            }
            std::ignore = get_declared_type(checker, value->as_value()->get_type());
            checker.add_variable(identifier_->name_, value);
        } else {
            TypePtr inferred_type = expr_->get_expr_info(checker).type;
            TypePtr declared_type = get_declared_type(checker, inferred_type);
            if (auto ast_constant = dynamic_cast<ASTConstant*>(expr_.get())) {
                try {
                    ast_constant->resolve_type(declared_type);
                } catch (UnlocatedProblem& e) {
                    e.report_at(expr_->location_);
                }
            }
            checker.add_variable(identifier_->name_, declared_type);
        }
    }
    TypePtr get_declared_type(TypeChecker& checker, TypePtr inferred_type) const {
        if (type_) {
            TypePtr declared_type = type_->eval(checker)->as_type();
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
    void collect_types(Scope& scope, OpDispatcher& ops) final {
        scope.add_type(identifier_, type_.get());
    }
};

class ASTIfStatement final : public ASTRecursiveNode {
public:
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTCodeBlock> if_block_;
    const std::unique_ptr<ASTCodeBlock> else_block_;
    ASTIfStatement(
        const Location& loc,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTCodeBlock> if_block,
        std::unique_ptr<ASTCodeBlock> else_block = nullptr
    ) noexcept
        : ASTRecursiveNode(loc),
          condition_(std::move(condition)),
          if_block_(std::move(if_block)),
          else_block_(std::move(else_block)) {}
    ~ASTIfStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield condition_.get();
        co_yield if_block_.get();
        if (else_block_) {
            co_yield else_block_.get();
        }
    }
};

class ASTForStatement final : public ASTRecursiveNode {
public:
    const std::unique_ptr<ASTNode> initializer_;  // Can be either a declaration or an expression
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTExpression> increment_;
    const std::unique_ptr<ASTCodeBlock> body_;
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTDeclaration> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        std::unique_ptr<ASTCodeBlock> body
    ) noexcept
        : ASTRecursiveNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(std::move(body)) {}
    ASTForStatement(
        const Location& loc,
        std::unique_ptr<ASTValueExpression> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        std::unique_ptr<ASTCodeBlock> body
    ) noexcept
        : ASTRecursiveNode(loc),
          initializer_(std::move(initializer)),
          condition_(std::move(condition)),
          increment_(std::move(increment)),
          body_(std::move(body)) {}
    ASTForStatement(const Location& loc, std::unique_ptr<ASTCodeBlock> body) noexcept
        : ASTRecursiveNode(loc), body_(std::move(body)) {}
    ~ASTForStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        if (initializer_) {
            co_yield initializer_.get();
        }
        if (condition_) {
            co_yield condition_.get();
        }
        if (increment_) {
            co_yield increment_.get();
        }
        co_yield body_.get();
    }
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTContinueStatement() noexcept final = default;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTBreakStatement() noexcept final = default;
};

class ASTReturnStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTReturnStatement(const Location& loc, std::unique_ptr<ASTExpression> expr = nullptr) noexcept
        : ASTNode(loc), expr_(std::move(expr)) {}
    ~ASTReturnStatement() noexcept final = default;
};

class ASTFunctionParameter final : public ASTNode {
public:
    const std::unique_ptr<const ASTIdentifier> identifier_;
    const std::unique_ptr<const ASTExpression> type_;
    ASTFunctionParameter(
        const Location& loc,
        std::unique_ptr<const ASTIdentifier> identifier,
        std::unique_ptr<const ASTExpression> type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}
    ~ASTFunctionParameter() noexcept final = default;
};

class ASTFunctionSignature final : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTFunctionParameter>> parameters_;
    std::unique_ptr<ASTFunctionParameter> spread_;
    std::unique_ptr<ASTExpression> return_type_;
    ASTFunctionSignature(
        const Location& loc,
        std::unique_ptr<ASTExpression> first_type,
        std::unique_ptr<ASTIdentifier> first_name
    ) noexcept
        : ASTNode(loc) {
        parameters_.push_back(
            std::make_unique<ASTFunctionParameter>(
                loc, std::move(first_name), std::move(first_type)
            )
        );
    }
    ~ASTFunctionSignature() noexcept final = default;
    ASTFunctionSignature& push(
        const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
    ) noexcept {
        parameters_.push_back(std::move(next_param));
        location_ = new_location;
        return *this;
    }
    ASTFunctionSignature& push_spread(
        const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
    ) noexcept {
        spread_ = std::move(next_param);
        location_ = new_location;
        return *this;
    }
    ASTFunctionSignature& set_return_type(
        const Location& new_location, std::unique_ptr<ASTExpression> return_type
    ) noexcept {
        return_type_ = std::move(return_type);
        location_ = new_location;
        return *this;
    }
};

class ASTFunctionDefinition final : public ASTNode {
public:
    const GlobalMemory::String name_;
    const std::unique_ptr<const ASTFunctionSignature> signature_;
    const std::unique_ptr<const ASTCodeBlock> body_;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::unique_ptr<const ASTIdentifier> name,
        std::unique_ptr<const ASTFunctionSignature> signature,
        std::unique_ptr<const ASTCodeBlock> body
    ) noexcept
        : ASTNode(loc),
          name_(name->name_),
          signature_(std::move(signature)),
          body_(std::move(body)) {}
    ~ASTFunctionDefinition() noexcept final = default;
};

// ===================== Inline implementations of TypeChecker =====================

inline TypeChecker::TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& types) noexcept
    : current_scope_(&root), ops_(ops), types_(types) {}

inline void TypeChecker::add_variable(std::string_view identifier, ObjectPtr expr) {
    current_scope_->add_variable(identifier, expr);
}

inline void TypeChecker::enter(const ASTCodeBlock* child) noexcept {
    assert(
        child->name_.empty()
            ? std::ranges::contains(current_scope_->anonymous_children_, child->local_scope_.get())
            : current_scope_->children_.at(child->name_) == child->local_scope_.get()
    );
    current_scope_ = child->local_scope_.get();
}

inline void TypeChecker::exit() noexcept { current_scope_ = current_scope_->parent_; }

inline ObjectPtr TypeChecker::resolve(std::string_view identifier) {
    return resolve_in(identifier, *current_scope_);
}

inline ExprInfo TypeChecker::type_of(std::string_view identifier) {
    return type_of_in(identifier, *current_scope_);
}

inline ObjectPtr TypeChecker::resolve_in(std::string_view identifier, Scope& scope) {
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
        TypePtr type = it_var->second->as_type();
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
