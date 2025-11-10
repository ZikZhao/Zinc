#pragma once
#include "pch.hpp"
#include "object.hpp"

template<typename Op>
concept OperatorFunctor = requires { GetOperatorString<Op>(); };

class ASTNode;
class ASTExpression;
using ASTValueExpression = ASTExpression;
using ASTTypeExpression = ASTExpression;
template<ValueClass T> class ASTConstant;
class ASTIdentifier;
template<OperatorFunctor Op> class ASTUnaryOp;
template<OperatorFunctor Op> class ASTBinaryOp;
class ASTDeclaration;
class ASTIfStatement;

class TypeSystem {
public:
    using TypeMap = std::map<const ASTTypeExpression*, TypeRef>;
    struct CompareBySecond {
        constexpr bool operator () (const auto& left, const auto& right) const {
            return left.second == right.second ? left.first < right.first : left.second < right.second;
        }
    };
private:
    std::vector<const ASTTypeExpression*> types_;
    std::map<const ASTTypeExpression*, TypeRef> type_map_;
public:
    TypeSystem() = default;
    ~TypeSystem() noexcept = default;
    void add(const ASTTypeExpression* expr);
    void resolve();
    TypeRef eval(const ASTTypeExpression* expr) const noexcept;
};

class Scope {
private:
    Scope* const parent_ = nullptr;
    Scope* const enclosing_ = nullptr;
    std::vector<std::pair<std::string, const ASTTypeExpression*>> symbols_;
    std::uint64_t shadow_count_ = 0;
public:
    Scope(Scope& parent) noexcept;
    Scope(Scope& enclosing, int) noexcept;
    Scope(const std::vector<std::pair<std::string, TypeRef>>& builtins) noexcept;
    ~Scope() noexcept = default;
    std::uint64_t get_length() const noexcept;
    void add(std::string name, const ASTTypeExpression* symbol);
    std::pair<std::uint64_t, bool> get_offset(std::string_view name) const;
    void update_parent_shadow() const noexcept;
private:
    std::uint64_t get_parent_offset() const noexcept;
    bool is_global() const noexcept;
};

class RuntimeStack {
private:
    std::vector<ValueRef> symbols_;
public:
    RuntimeStack() = default;
    RuntimeStack(const RuntimeStack& other) = default;
    RuntimeStack(RuntimeStack&& other) noexcept = default;
    RuntimeStack(auto&& view) : symbols_(std::forward<decltype(view)>(view)) {}
    ~RuntimeStack() = default;
    ValueRef operator[] (std::uint64_t index) noexcept;
    void enter(const Scope& scope) noexcept;
    void exit(const Scope& scope) noexcept;
};

class ASTNode {
public:
    Location location_;
    ASTNode(const Location& loc) noexcept;
    virtual ~ASTNode() noexcept = default;
    virtual std::generator<ASTNode*> get_children() const noexcept;
    virtual void print(std::ostream& os, std::uint64_t indent = 0) const noexcept = 0;
    virtual void first_analyze(TypeSystem& ts);
    virtual void second_analyze(TypeSystem& ts, Scope& scope);
    virtual void execute(RuntimeStack& globals, RuntimeStack& locals) const;
};

class ASTCodeBlock final : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTNode>> statements_;
    std::unique_ptr<Scope> local_scope_;
    ASTCodeBlock(const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements) noexcept;
    ~ASTCodeBlock() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, std::uint64_t indent) const noexcept final;
    void second_analyze(TypeSystem& ts, Scope& scope) final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
    ASTCodeBlock& push(const Location& new_location, std::unique_ptr<ASTNode> node) noexcept;
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& location) : ASTNode(location) {};
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final {
        (void)eval(globals, locals);
    }
    virtual ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const {
        assert(false && "Is not a value expression");
    }
    virtual TypeRef eval(const TypeSystem& type_map) const noexcept {
        assert(false && "Is not a type expression");
    };
    virtual std::generator<const ASTTypeExpression*> get_dependencies() const noexcept {
        assert(false && "Is not a type expression");
    }
};

template<ValueClass V>
class ASTConstant final : public ASTExpression {
public:
    const ValueRef value_;
    ASTConstant(const Location& loc, std::string_view str) : ASTExpression(loc), value_(Value::FromLiteral<V>(str)) {}
    ~ASTConstant() noexcept final = default;
    void print(std::ostream& os, std::uint64_t indent) const noexcept final {
        os << std::string(indent, ' ') << "Constant(" << value_.repr() << ")" << std::endl;
    }
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const noexcept final {
        return value_;
    }
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string name_;
    std::uint64_t index_;
    bool is_global_;
    ASTIdentifier(const Location& loc, std::string name) noexcept;
    ~ASTIdentifier() noexcept final = default;
    void print(std::ostream& os, std::uint64_t indent) const noexcept final;
    void second_analyze(TypeSystem& ts, Scope& scope) final;
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const noexcept final;
};

template<OperatorFunctor Op>
class ASTUnaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTUnaryOp(const Location& location, std::unique_ptr<ASTExpression> expr) noexcept : ASTExpression(location), expr_(std::move(expr)) {}
    ~ASTUnaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield expr_.get();
    }
    void print(std::ostream& os, std::uint64_t indent) const noexcept final {
        os << std::string(indent, ' ') << "UnaryOp(" << GetOperatorString<Op>() << ")" << std::endl;
        expr_->print(os, indent + 2);
    }
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const final {
        ValueRef result = expr_->eval(globals, locals);
        return result.eval_operation<Op>();
    }
};

template<OperatorFunctor Op>
class ASTBinaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();
public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(const Location& location, std::unique_ptr<ASTExpression> left, std::unique_ptr<ASTExpression> right) noexcept
        : ASTExpression(location), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield left_.get();
        co_yield right_.get();
    }
    void print(std::ostream& os, std::uint64_t indent) const noexcept final {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<Op>() << ")" << std::endl;
        left_->print(os, indent + 2);
        right_->print(os, indent + 2);
    }
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const final {
        ValueRef result_left = left_->eval(globals, locals);
        ValueRef result_right = right_->eval(globals, locals);
        return result_left.eval_operation<Op>(result_right);
    }
};

template<OperatorFunctor Op>
class ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>> final : public ASTExpression {
private:
    using AssignOperator = OperatorFunctors::OperateAndAssign<Op>;
    static constexpr auto Func = AssignOperator();
public:
    const std::unique_ptr<ASTExpression> left_;
    const std::unique_ptr<ASTExpression> right_;
    ASTBinaryOp(const Location& location, std::unique_ptr<ASTExpression> left, std::unique_ptr<ASTExpression> right) noexcept
        : ASTExpression(location), left_(std::move(left)), right_(std::move(right)) {}
    ~ASTBinaryOp() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final {
        co_yield left_.get();
        co_yield right_.get();
    }
    void print(std::ostream& os, std::uint64_t indent) const noexcept final {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<AssignOperator>() << ")" << std::endl;
        left_->print(os, indent + 2);
        right_->print(os, indent + 2);
    }
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const final {
        ValueRef result_left = left_->eval(globals, locals);
        ValueRef result_right = right_->eval(globals, locals);
        ValueRef result = result_left.eval_operation<Op>(result_right);
        return result_left = result;
    }
};

class ASTFunctionCall final : public ASTExpression {
public:
    const std::unique_ptr<ASTValueExpression> function_;
    const std::vector<std::unique_ptr<ASTValueExpression>> arguments_;
    ASTFunctionCall(const Location& location, std::unique_ptr<ASTValueExpression> function, std::vector<std::unique_ptr<ASTValueExpression>> arguments) noexcept;
    ~ASTFunctionCall() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    ValueRef eval(RuntimeStack& globals, RuntimeStack& locals) const final;
};

using ASTAddOp              = ASTBinaryOp<OperatorFunctors::Add>;
using ASTSubtractOp         = ASTBinaryOp<OperatorFunctors::Subtract>;
using ASTNegateOp           = ASTUnaryOp<OperatorFunctors::Negate>;
using ASTMultiplyOp         = ASTBinaryOp<OperatorFunctors::Multiply>;
using ASTDivideOp           = ASTBinaryOp<OperatorFunctors::Divide>;
using ASTRemainderOp        = ASTBinaryOp<OperatorFunctors::Remainder>;
using ASTIncrementOp        = ASTUnaryOp<OperatorFunctors::Increment>;
using ASTDecrementOp        = ASTUnaryOp<OperatorFunctors::Decrement>;

using ASTEqualOp            = ASTBinaryOp<OperatorFunctors::Equal>;
using ASTNotEqualOp         = ASTBinaryOp<OperatorFunctors::NotEqual>;
using ASTLessThanOp         = ASTBinaryOp<OperatorFunctors::LessThan>;
using ASTLessEqualOp        = ASTBinaryOp<OperatorFunctors::LessEqual>;
using ASTGreaterThanOp      = ASTBinaryOp<OperatorFunctors::GreaterThan>;
using ASTGreaterEqualOp     = ASTBinaryOp<OperatorFunctors::GreaterEqual>;

using ASTLogicalAndOp       = ASTBinaryOp<OperatorFunctors::LogicalAnd>;
using ASTLogicalOrOp        = ASTBinaryOp<OperatorFunctors::LogicalOr>;
using ASTLogicalNotOp       = ASTUnaryOp<OperatorFunctors::LogicalNot>;

using ASTBitwiseAndOp       = ASTBinaryOp<OperatorFunctors::BitwiseAnd>;
using ASTBitwiseOrOp        = ASTBinaryOp<OperatorFunctors::BitwiseOr>;
using ASTBitwiseXorOp       = ASTBinaryOp<OperatorFunctors::BitwiseXor>;
using ASTBitwiseNotOp       = ASTUnaryOp<OperatorFunctors::BitwiseNot>;
using ASTLeftShiftOp        = ASTBinaryOp<OperatorFunctors::LeftShift>;
using ASTRightShiftOp       = ASTBinaryOp<OperatorFunctors::RightShift>;

using ASTAssignOp           = ASTBinaryOp<OperatorFunctors::Assign>;
using ASTAddAssignOp        = ASTBinaryOp<OperatorFunctors::AddAssign>;
using ASTSubtractAssignOp   = ASTBinaryOp<OperatorFunctors::SubtractAssign>;
using ASTMultiplyAssignOp   = ASTBinaryOp<OperatorFunctors::MultiplyAssign>;
using ASTDivideAssignOp     = ASTBinaryOp<OperatorFunctors::DivideAssign>;
using ASTRemainderAssignOp  = ASTBinaryOp<OperatorFunctors::RemainderAssign>;
using ASTLogicalAndAssignOp = ASTBinaryOp<OperatorFunctors::LogicalAndAssign>;
using ASTLogicalOrAssignOp  = ASTBinaryOp<OperatorFunctors::LogicalOrAssign>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp  = ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>;
using ASTLeftShiftAssignOp  = ASTBinaryOp<OperatorFunctors::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorFunctors::RightShiftAssign>;

template<TypeClass T>
class ASTPrimitiveType final : public ASTExpression {
public:
    ASTPrimitiveType(const Location& loc) noexcept : ASTExpression(loc) {}
    void print(std::ostream& os, uint64_t indent) const noexcept final {
        if constexpr (std::is_same_v<T, NullType>) {
            os << std::string(indent, ' ') << "PrimitiveType(Null)" << std::endl;
        } else if constexpr (std::is_same_v<T, IntegerType>) {
            os << std::string(indent, ' ') << "PrimitiveType(Integer)" << std::endl;
        } else if constexpr (std::is_same_v<T, FloatType>) {
            os << std::string(indent, ' ') << "PrimitiveType(Float)" << std::endl;
        } else if constexpr (std::is_same_v<T, StringType>) {
            os << std::string(indent, ' ') << "PrimitiveType(String)" << std::endl;
        } else if constexpr (std::is_same_v<T, BooleanType>) {
            os << std::string(indent, ' ') << "PrimitiveType(Boolean)" << std::endl;
        } else {
            static_assert(false, "Unhandled primitive type");
        }
    }
    TypeRef eval(const TypeSystem& type_map) const noexcept final {
        return new T();
    }
};

template<>
class ASTPrimitiveType<FunctionType> final : public ASTExpression {
public:
    const TypeRef value_;
    ASTPrimitiveType(FunctionType* func) noexcept : ASTExpression({0, {0, 0}, {0, 0}}), value_(func) {}
    ASTPrimitiveType(const Location& loc, FunctionType* func) noexcept : ASTExpression(loc), value_(func) {}
    void print(std::ostream& os, uint64_t indent) const noexcept final {
        os << std::string(indent, ' ') << "PrimitiveType(Function)" << std::endl;
    }
    TypeRef eval(const TypeSystem& type_map) const noexcept final {
        return value_;
    }
};

class ASTRecordType final : public ASTExpression {
public:
    std::vector<std::unique_ptr<ASTDeclaration>> fields_;
    ASTRecordType(const Location& location, std::vector<std::unique_ptr<ASTDeclaration>> fields) noexcept;
    ~ASTRecordType() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    TypeRef eval(const TypeSystem& type_map) const noexcept final;
};

class ASTDeclaration final : public ASTNode {
public:
    const std::unique_ptr<ASTIdentifier> identifier_;
    const std::unique_ptr<ASTExpression> type_;
    const std::unique_ptr<ASTExpression> expr_;
    TypeRef inferred_type_;
    ASTDeclaration(const Location& location, std::unique_ptr<ASTIdentifier> identifier, std::unique_ptr<ASTExpression> type, std::unique_ptr<ASTExpression> expr = nullptr) noexcept;
    ~ASTDeclaration() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void second_analyze(TypeSystem& ts, Scope& scope) final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTTypeAlias final : public ASTNode {
public:
    const std::unique_ptr<ASTIdentifier> identifier_;
    const std::unique_ptr<ASTExpression> type_;
    ASTTypeAlias(const Location& location, std::unique_ptr<ASTIdentifier> identifier, std::unique_ptr<ASTExpression> type) noexcept;
    ~ASTTypeAlias() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void first_analyze(TypeSystem& ts) final;
};

class ASTIfStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTCodeBlock> if_block_;
    const std::unique_ptr<ASTCodeBlock> else_block_;
    ASTIfStatement(const Location& location, std::unique_ptr<ASTExpression> condition, std::unique_ptr<ASTCodeBlock> if_block, std::unique_ptr<ASTCodeBlock> else_block = nullptr) noexcept;
    ~ASTIfStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTForStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTNode> initializer_; // Can be either a declaration or an expression
    const std::unique_ptr<ASTExpression> condition_;
    const std::unique_ptr<ASTExpression> increment_;
    const std::unique_ptr<ASTCodeBlock> body_;
    ASTForStatement(const Location& location, std::unique_ptr<ASTNode> initializer, std::unique_ptr<ASTExpression> condition, std::unique_ptr<ASTExpression> increment, std::unique_ptr<ASTCodeBlock> body) noexcept;
    ASTForStatement(const Location& location, std::unique_ptr<ASTCodeBlock> body) noexcept;
    ~ASTForStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& location) noexcept;
    ~ASTContinueStatement() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& location) noexcept;
    ~ASTBreakStatement() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTReturnStatement final : public ASTNode {
public:
    const std::unique_ptr<ASTExpression> expr_;
    ASTReturnStatement(const Location& location, std::unique_ptr<ASTExpression> expr = nullptr) noexcept;
    ~ASTReturnStatement() noexcept final = default;
    std::generator<ASTNode*> get_children() const noexcept final;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    void execute(RuntimeStack& globals, RuntimeStack& locals) const final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    const std::unique_ptr<const ASTIdentifier> identifier_;
    const std::unique_ptr<const ASTExpression> type_;
    ASTFunctionParameter(const Location& location, std::unique_ptr<const ASTIdentifier> identifier, std::unique_ptr<const ASTExpression> type) noexcept;
    ~ASTFunctionParameter() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
};

class ASTFunctionSignature final : public ASTNode {
public:
    std::vector<std::unique_ptr<ASTFunctionParameter>> parameters_;
    std::unique_ptr<ASTFunctionParameter> spread_;
    std::unique_ptr<ASTExpression> return_type_;
    ASTFunctionSignature(const Location& location, std::unique_ptr<ASTExpression> first_type, std::unique_ptr<ASTIdentifier> first_name) noexcept;
    ~ASTFunctionSignature() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
    ASTFunctionSignature& push(const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param) noexcept;
    ASTFunctionSignature& push_spread(const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param) noexcept;
    ASTFunctionSignature& set_return_type(const Location& new_location, std::unique_ptr<ASTExpression> return_type) noexcept;
    RuntimeStack collect_arguments(const Arguments& raw) const;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    const std::string name_;
    const std::unique_ptr<const ASTFunctionSignature> signature_;
    const std::unique_ptr<const ASTCodeBlock> body_;
public:
    ASTFunctionDefinition(const Location& location, std::unique_ptr<const ASTIdentifier> name, std::unique_ptr<const ASTFunctionSignature> signature, std::unique_ptr<const ASTCodeBlock> body) noexcept;
    ~ASTFunctionDefinition() noexcept final = default;
    void print(std::ostream& os, uint64_t indent) const noexcept final;
};
