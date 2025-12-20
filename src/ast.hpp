#pragma once
#include "pch.hpp"
#include <string_view>
#include <utility>

#include "object.hpp"

template <typename Op>
concept OperatorFunctor = requires { GetOperatorString<Op>(); };

class ASTNode;
class ASTExpression;
using ASTValueExpression = ASTExpression;
using ASTTypeExpression = ASTExpression;
class ASTCodeBlock;
template <ValueClass T>
class ASTConstant;
class ASTIdentifier;
template <OperatorFunctor Op>
class ASTUnaryOp;
template <OperatorFunctor Op>
class ASTBinaryOp;
class ASTFunctionCall;
template <TypeClass T>
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

class Context {
    friend class TypeResolver;

private:
    const Context* const parent_ = nullptr;
    const std::string_view name_;
    std::map<const std::string_view, const ASTValueExpression*> symbols_;
    std::map<const std::string_view, const ASTTypeExpression*> types_;
    std::vector<const Context*> anonymous_children_;
    std::map<const std::string_view, const Context*> children_;

public:
    Context() noexcept = default;
    Context(Context& parent) noexcept;
    Context(std::string_view name, Context& parent) noexcept;
    Context(std::vector<std::pair<std::string_view, const ASTExpression*>> builtins) noexcept;
    void add_symbol(std::string_view identifier, const ASTValueExpression* expr) noexcept;
    void add_type(std::string_view identifier, const ASTTypeExpression* expr) noexcept;
};

class TypeResolver {
private:
    struct CacheRecord {
        bool is_resolving = false;
        TypeRef type_ref = nullptr;
    };

private:
    const Context* current_;
    std::map<std::pair<const Context*, std::string_view>, CacheRecord> cache_;

public:
    TypeResolver(const Context& root) noexcept;
    void enter(const ASTCodeBlock* child) noexcept;
    void exit() noexcept;
    TypeRef resolve(std::string_view identifier);
};

class ASTNode {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept;
    virtual ~ASTNode() noexcept = default;
    virtual std::generator<ASTNode*> get_children() const noexcept;
    virtual void analyze(Context& ctx);
};

class ASTCodeBlock : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements_;
    std::unique_ptr<Context> local_scope_;
    std::string_view name_;
    ASTCodeBlock(const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements) noexcept;
    ASTCodeBlock(
        const Location& loc,
        const std::string& name,
        std::vector<std::unique_ptr<ASTNode>> statements
    ) noexcept;
    std::generator<ASTNode*> get_children() const noexcept final;
    void analyze(Context& ctx) final;
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& location) : ASTNode(location) {};
    virtual TypeRef eval(TypeResolver& tr) const noexcept {
        assert(false && "Is not a type expression");
        std::unreachable();
    };
    virtual std::generator<const ASTIdentifier*> get_dependencies() const noexcept {
        assert(false && "Is not a type expression");
        std::unreachable();
    }
};

template <ValueClass V>
class ASTConstant final : public ASTExpression {
public:
    const ValueRef value_;
    ASTConstant(const Location& loc, std::string_view str)
        : ASTExpression(loc), value_(Value::FromLiteral<V>(str)) {}
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view name_;
    ASTIdentifier(const Location& loc, const std::string& name) noexcept;
};

template <OperatorFunctor Op>
class ASTUnaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();

public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTUnaryOp(const Location& location, std::unique_ptr<ASTExpression> expr) noexcept
        : ASTExpression(location), expr_(std::move(expr)) {}
    ~ASTUnaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final { co_yield expr_.get(); }
};

template <OperatorFunctor Op>
class ASTBinaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();

public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(
        const Location& location,
        std::unique_ptr<ASTExpression> left,
        std::unique_ptr<ASTExpression> right
    ) noexcept
        : ASTExpression(location), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield left_.get();
        co_yield right_.get();
    }
};

template <OperatorFunctor Op>
class ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>> final : public ASTExpression {
private:
    using AssignOperator = OperatorFunctors::OperateAndAssign<Op>;
    static constexpr auto Func = AssignOperator();

public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(
        const Location& location,
        std::unique_ptr<ASTExpression> left,
        std::unique_ptr<ASTExpression> right
    ) noexcept
        : ASTExpression(location), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield left_.get();
        co_yield right_.get();
    }
};

class ASTFunctionCall final : public ASTExpression {
public:
    const std::unique_ptr<ASTValueExpression> function_;
    const std::vector<std::unique_ptr<ASTValueExpression>> arguments_;
    ASTFunctionCall(
        const Location& location,
        std::unique_ptr<ASTValueExpression> function,
        std::vector<std::unique_ptr<ASTValueExpression>> arguments
    ) noexcept;
    ~ASTFunctionCall() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
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

template <TypeClass T>
class ASTPrimitiveType final : public ASTTypeExpression {
private:
    static inline const TypeRef type_instance_ = new T();

public:
    ASTPrimitiveType(const Location& loc) noexcept : ASTTypeExpression(loc) {}
    TypeRef eval(TypeResolver& tr) const noexcept final { return type_instance_; }
    std::generator<const ASTIdentifier*> get_dependencies() const noexcept final { co_return; }
};

template <>
class ASTPrimitiveType<FunctionType> final : public ASTTypeExpression {
public:
    const TypeRef value_;
    ASTPrimitiveType(FunctionType* func) noexcept
        : ASTTypeExpression({0, {0, 0}, {0, 0}}), value_(func) {}
    ASTPrimitiveType(const Location& loc, FunctionType* func) noexcept
        : ASTTypeExpression(loc), value_(func) {}
    TypeRef eval(TypeResolver& tr) const noexcept final { return value_; }
    std::generator<const ASTIdentifier*> get_dependencies() const noexcept final { co_return; }
};

class ASTRecordType final : public ASTTypeExpression {
public:
    const std::vector<std::unique_ptr<ASTFieldDeclaration>> fields_;
    ASTRecordType(
        const Location& location, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
    ) noexcept;
    ~ASTRecordType() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    TypeRef eval(TypeResolver& tr) const noexcept final;
    std::generator<const ASTIdentifier*> get_dependencies() const noexcept final;
};

class ASTDeclaration final : public ASTNode {
public:
    const std::unique_ptr<ASTIdentifier> identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    const std::unique_ptr<ASTValueExpression> expr_;
    TypeRef inferred_type_;
    ASTDeclaration(
        const Location& location,
        std::unique_ptr<ASTIdentifier> identifier,
        std::unique_ptr<ASTTypeExpression> type,
        std::unique_ptr<ASTValueExpression> expr = nullptr
    ) noexcept;
    ~ASTDeclaration() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
};

class ASTFieldDeclaration final : public ASTNode {
public:
    const std::string identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    ASTFieldDeclaration(
        const Location& location, std::string identifier, std::unique_ptr<ASTTypeExpression> type
    ) noexcept;
    ~ASTFieldDeclaration() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
};

class ASTTypeAlias final : public ASTNode {
public:
    const std::string identifier_;
    const std::unique_ptr<ASTTypeExpression> type_;
    ASTTypeAlias(
        const Location& location, std::string identifier, std::unique_ptr<ASTTypeExpression> type
    ) noexcept;
    ~ASTTypeAlias() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void analyze(Context& ctx) final;
};

class ASTIfStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTCodeBlock> if_block_;
    const std::unique_ptr<ASTCodeBlock> else_block_;
    ASTIfStatement(
        const Location& location,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTCodeBlock> if_block,
        std::unique_ptr<ASTCodeBlock> else_block = nullptr
    ) noexcept;
    ~ASTIfStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
};

class ASTForStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTNode> initializer_;  // Can be either a declaration or an expression
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTExpression> increment_;
    const std::unique_ptr<ASTCodeBlock> body_;
    ASTForStatement(
        const Location& location,
        std::unique_ptr<ASTNode> initializer,
        std::unique_ptr<ASTExpression> condition,
        std::unique_ptr<ASTExpression> increment,
        std::unique_ptr<ASTCodeBlock> body
    ) noexcept;
    ASTForStatement(const Location& location, std::unique_ptr<ASTCodeBlock> body) noexcept;
    ~ASTForStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& location) noexcept;
    ~ASTContinueStatement() noexcept final = default;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& location) noexcept;
    ~ASTBreakStatement() noexcept final = default;
};

class ASTReturnStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTReturnStatement(
        const Location& location, std::unique_ptr<ASTExpression> expr = nullptr
    ) noexcept;
    ~ASTReturnStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    const std::unique_ptr<const ASTIdentifier> identifier_;
    const std::unique_ptr<const ASTExpression> type_;
    ASTFunctionParameter(
        const Location& location,
        std::unique_ptr<const ASTIdentifier> identifier,
        std::unique_ptr<const ASTExpression> type
    ) noexcept;
    ~ASTFunctionParameter() noexcept final = default;
};

class ASTFunctionSignature final : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTFunctionParameter>> parameters_;
    std::unique_ptr<ASTFunctionParameter> spread_;
    std::unique_ptr<ASTExpression> return_type_;
    ASTFunctionSignature(
        const Location& location,
        std::unique_ptr<ASTExpression> first_type,
        std::unique_ptr<ASTIdentifier> first_name
    ) noexcept;
    ~ASTFunctionSignature() noexcept final = default;
    ASTFunctionSignature& push(
        const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
    ) noexcept;
    ASTFunctionSignature& push_spread(
        const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
    ) noexcept;
    ASTFunctionSignature& set_return_type(
        const Location& new_location, std::unique_ptr<ASTExpression> return_type
    ) noexcept;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    const std::string name_;
    const std::unique_ptr<const ASTFunctionSignature> signature_;
    const std::unique_ptr<const ASTCodeBlock> body_;

public:
    ASTFunctionDefinition(
        const Location& location,
        std::unique_ptr<const ASTIdentifier> name,
        std::unique_ptr<const ASTFunctionSignature> signature,
        std::unique_ptr<const ASTCodeBlock> body
    ) noexcept;
    ~ASTFunctionDefinition() noexcept final = default;
};
