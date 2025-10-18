#pragma once
#include "pch.hpp"
#include "object.hpp"

template<typename Op>
concept OperatorFunctor = requires { GetOperatorString<Op>(); };

class ASTNode;
class ASTToken;
class ASTExpression;
using ASTTypeExpression = ASTExpression;
using ASTValueExpression = ASTExpression;
template<ValueClass T> class ASTConstant;
class ASTIdentifier;
template<OperatorFunctor Op> class ASTUnaryOp;
template<OperatorFunctor Op> class ASTBinaryOp;
class ASTDeclaration;
class ASTIfStatement;

class ScopeDefinition {
private:
    const ScopeDefinition* const parent_;
    std::vector<std::pair<std::string, const ASTExpression*>> symbols;
    uint64_t shadow_count;
public:
    ScopeDefinition(const ScopeDefinition* parent);
    ScopeDefinition(const std::vector<std::pair<std::string, TypeRef>>& builtins);
    ~ScopeDefinition() = default;
    uint64_t get_length() const;
    void add_symbol(const std::string& name, const ASTExpression* symbol);
    bool has_var(const std::string& name) const;
    std::pair<uint64_t, bool> get_var_offset(const std::string& name) const noexcept;
    void update_shadow_count(const ScopeDefinition& child);
private:
    uint64_t get_full_length() const;
};

class ASTNode {
public:
    Location location_;
    ASTNode(const Location& location);
    virtual ~ASTNode() = default;
    virtual void print(std::ostream& os, uint64_t indent = 0) const = 0;
    virtual void first_analyze(ScopeDefinition* scope);
    virtual void second_analyze(ScopeDefinition* scope);
    virtual void execute(Context& globals, Context& locals) const;
};

class ASTToken final : public ASTNode {
private:
    static std::vector<std::unique_ptr<ASTToken>> Instances;
public:
    static ASTToken* New(auto&&... args) {
        Instances.emplace_back(new ASTToken(std::forward<decltype(args)>(args)...));
        return Instances.back().get();
    }
public:
    const std::string str_;
private:
    ASTToken(const Location& location, const char* str);
public:
    ~ASTToken() final = default;
    void print(std::ostream& os, uint64_t indent) const final;
    void execute(Context& globals, Context& locals) const final;
};

class ASTCodeBlock final : public ASTNode {
public:
    std::vector<ASTNode*> statements_;
    ScopeDefinition* local_scope_;
    ASTCodeBlock(const Location& location);
    ASTCodeBlock(const Location& location, ASTNode* node);
    ~ASTCodeBlock() final;
    void print(std::ostream& os, uint64_t indent) const final;
    void first_analyze(ScopeDefinition* scope) final;
    void second_analyze(ScopeDefinition* scope) final;
    void execute(Context& globals, Context& locals) const final;
    ASTCodeBlock& push(const Location& new_location, ASTNode* node);
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& location) : ASTNode(location) {};
    void execute(Context& globals, Context& locals) const final {
        (void)eval(globals, locals);
    }
    virtual Ref eval(Context& globals, Context& locals) const = 0;
};

template<ValueClass V>
class ASTConstant final : public ASTExpression {
public:
    const ValueRef value_;
    ASTConstant(const ASTToken& token) : ASTExpression(token.location_), value_(Value::FromLiteral<V>(token.str_)) {}
    ~ASTConstant() final = default;
    void print(std::ostream& os, uint64_t indent) const final {
        os << std::string(indent, ' ') << "Constant(" << value_.repr() << ")" << std::endl;
    }
    ValueRef eval(Context& globals, Context& locals) const final {
        return value_;
    }
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string name_;
    uint64_t index_;
    bool is_global_;
    ASTIdentifier(const ASTToken& token);
    ~ASTIdentifier() final = default;
    void print(std::ostream& os, uint64_t indent) const final;
    void first_analyze(ScopeDefinition* scope) final;
    void second_analyze(ScopeDefinition* scope) final;
    Ref eval(Context& globals, Context& locals) const final;
};

template<OperatorFunctor Op>
class ASTUnaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();
public:
    const ASTExpression* const expr_;
    ASTUnaryOp(const Location& location, const ASTExpression* expr) : ASTExpression(location), expr_(expr) {}
    ~ASTUnaryOp() final {
        delete expr_;
    }
    void print(std::ostream& os, uint64_t indent) const final {
        os << std::string(indent, ' ') << "UnaryOp(" << GetOperatorString<Op>() << ")" << std::endl;
        expr_->print(os, indent + 2);
    }
    Ref eval(Context& globals, Context& locals) const final {
        Ref result = expr_->eval(globals, locals);
        return result.eval_operation<Op>();
    }
};

template<OperatorFunctor Op>
class ASTBinaryOp final : public ASTExpression {
private:
    static constexpr auto Func = Op();
public:
    const ASTExpression* const left_;
    const ASTExpression* const right_;
    ASTBinaryOp(const Location& location, ASTExpression* left, ASTExpression* right)
        : ASTExpression(location), left_(left), right_(right) {}
    ~ASTBinaryOp() final {
        delete left_;
        delete right_;
    }
    void print(std::ostream& os, uint64_t indent) const final {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<Op>() << ")" << std::endl;
        left_->print(os, indent + 2);
        right_->print(os, indent + 2);
    }
    Ref eval(Context& globals, Context& locals) const final {
        Ref result_left = left_->eval(globals, locals);
        Ref result_right = right_->eval(globals, locals);
        return result_left.eval_operation<Op>(result_right);
    }
};

template<OperatorFunctor Op>
class ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>> final : public ASTExpression {
private:
    using AssignOperator = OperatorFunctors::OperateAndAssign<Op>;
    static constexpr auto Func = AssignOperator();
public:
    const ASTExpression* const left_;
    const ASTExpression* const right_;
    ASTBinaryOp(const Location& location, ASTExpression* left, ASTExpression* right)
        : ASTExpression(location), left_(left), right_(right) {}
    ~ASTBinaryOp() final {
        delete left_;
        delete right_;
    }
    void print(std::ostream& os, uint64_t indent) const final {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<AssignOperator>() << ")" << std::endl;
        left_->print(os, indent + 2);
        right_->print(os, indent + 2);
    }
    Ref eval(Context& globals, Context& locals) const final {
        Ref result_left = left_->eval(globals, locals);
        Ref result_right = right_->eval(globals, locals);
        Ref result = result_left.eval_operation<Op>(result_right);
        return result_left = result;
    }
};

class ASTFunctionCallArguments final : public ASTNode {
public:
    std::vector<const ASTExpression*> arguments_;
    ASTFunctionCallArguments();
    ASTFunctionCallArguments(const Location& location, const ASTExpression* first_arg);
    ~ASTFunctionCallArguments() final;
    void print(std::ostream& os, uint64_t indent) const final;
    ASTFunctionCallArguments& push_back(const Location& new_location, const ASTExpression* arg);
    Arguments eval_arguments(Context& globals, Context& locals) const;
};

class ASTFunctionCall final : public ASTExpression {
public:
    const ASTExpression* const function_;
    const ASTFunctionCallArguments* const arguments_;
    ASTFunctionCall(const Location& location, const ASTExpression* function, const ASTFunctionCallArguments* arguments = 0);
    ~ASTFunctionCall() final;
    void print(std::ostream& os, uint64_t indent) const final;
    ValueRef eval(Context& globals, Context& locals) const final;
};

using ASTAddOp              = ASTBinaryOp<OperatorFunctors::Add>;
using ASTSubtractOp         = ASTBinaryOp<OperatorFunctors::Subtract>;
using ASTNegateOp           = ASTUnaryOp<OperatorFunctors::Negate>;
using ASTMultiplyOp         = ASTBinaryOp<OperatorFunctors::Multiply>;
using ASTDivideOp           = ASTBinaryOp<OperatorFunctors::Divide>;
using ASTRemainderOp        = ASTBinaryOp<OperatorFunctors::Remainder>;

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
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp  = ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>;
using ASTLeftShiftAssignOp  = ASTBinaryOp<OperatorFunctors::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorFunctors::RightShiftAssign>;

template<TypeClass T>
class ASTPrimitiveType final : public ASTExpression {
public:
    ASTPrimitiveType(const ASTToken& token) : ASTExpression(token.location_) {}
    void print(std::ostream& os, uint64_t indent) const final {
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
    TypeRef eval(Context& globals, Context& locals) const final {
        return new T();
    }
};

template<>
class ASTPrimitiveType<FunctionType> final : public ASTExpression {
public:
    const TypeRef value_;
    ASTPrimitiveType(FunctionType* func) : ASTExpression({{0, 0}, {0, 0}}), value_(func) {}
    ASTPrimitiveType(const ASTToken& token, FunctionType* func) : ASTExpression(token.location_), value_(func) {}
    void print(std::ostream& os, uint64_t indent) const final {
        os << std::string(indent, ' ') << "PrimitiveType(Function)" << std::endl;
    }
    TypeRef eval(Context& globals, Context& locals) const final {
        return value_;
    }
};

class ASTDeclaration final : public ASTNode {
public:
    const bool is_const_;
    const ASTExpression* const type_;
    const ASTIdentifier* const identifier_;
    const ASTExpression* const expr_;
    const TypeRef inferred_type_;
    ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTExpression* expr, const bool is_const = false);
    ASTDeclaration(const Location& location, const ASTExpression* type, const ASTIdentifier* identifier, const ASTExpression* expr, const bool is_const = false);
    ~ASTDeclaration() final;
    void print(std::ostream& os, uint64_t indent) const final;
    void first_analyze(ScopeDefinition* scope) final;
};

class ASTTypeAlias final : public ASTNode {
public:
    const ASTIdentifier* const identifier_;
    const ASTExpression* const type_;
    ASTTypeAlias(const Location& location, const ASTIdentifier* identifier, const ASTExpression* type);
    ~ASTTypeAlias() final;
    void print(std::ostream& os, uint64_t indent) const final;
    void first_analyze(ScopeDefinition* scope) final;
};

class ASTIfStatement final : public ASTNode {
public:
    const ASTExpression* condition_;
    const ASTCodeBlock* const if_block_;
    const ASTCodeBlock* const else_block_;
    ASTIfStatement(const Location& location, const ASTExpression* condition, const ASTCodeBlock* const if_block, const ASTCodeBlock* const else_block = nullptr);
    ~ASTIfStatement() final;
    void execute(Context& globals, Context& locals) const final;
    void print(std::ostream& os, uint64_t indent) const final;
};

class ASTForStatement final : public ASTNode {
public:
    const ASTNode* const initializer_; // Can be either a declaration or an expression
    const ASTExpression* const condition_;
    const ASTExpression* const increment_;
    const ASTCodeBlock* const body_;
    ASTForStatement(const Location& location, const ASTNode* initializer, const ASTExpression* condition, const ASTExpression* increment, const ASTCodeBlock* body);
    ~ASTForStatement() final;
    void execute(Context& globals, Context& locals) const final;
    void print(std::ostream& os, uint64_t indent) const final;
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& location);
    ~ASTContinueStatement() final = default;
    void execute(Context& globals, Context& locals) const final;
    void print(std::ostream& os, uint64_t indent) const final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& location);
    ~ASTBreakStatement() final = default;
    void execute(Context& globals, Context& locals) const final;
    void print(std::ostream& os, uint64_t indent) const final;
};

class ASTFunctionParameter final : public ASTNode {
public:
    const ASTIdentifier* const identifier_;
    const ASTExpression* const type_;
    ASTFunctionParameter(const Location& location, const ASTIdentifier* identifier, const ASTExpression* type);
    ~ASTFunctionParameter() final;
    void print(std::ostream& os, uint64_t indent) const final;
};

class ASTFunctionSignature final : public ASTNode {
public:
    std::vector<ASTFunctionParameter*> parameters_;
    ASTFunctionParameter* spread_;
    ASTExpression* return_type_;
    ASTFunctionSignature(const Location& location, const ASTExpression* first_type, const ASTIdentifier* first_name);
    ~ASTFunctionSignature() final;
    void print(std::ostream& os, uint64_t indent) const final;
    ASTFunctionSignature& push(const Location& new_location, ASTFunctionParameter* param);
    ASTFunctionSignature& push_spread(const Location& new_location, ASTFunctionParameter* param);
    ASTFunctionSignature& set_return_type(const Location& new_location, ASTExpression* return_type);
    Context collect_arguments(const Arguments& raw) const;
};

class ASTFunctionDefinition final : public ASTNode {
public:
    const std::string name_;
    const ASTFunctionSignature* const signature_;
    const ASTCodeBlock* const body_;
protected:
    ASTFunctionDefinition(const char* name);
public:
    ASTFunctionDefinition(const Location& location, const ASTIdentifier* name, const ASTFunctionSignature* signature, const ASTCodeBlock* body);
    ~ASTFunctionDefinition() final;
    void print(std::ostream& os, uint64_t indent) const final;
};
