#pragma once
#include "pch.hpp"
#include "value.hpp"
#include "type.hpp"

struct Location {
    struct {
        uint64_t line;
        uint64_t column;
    } begin, end;
};

std::ostream& operator << (std::ostream& os, const Location& loc);

class ASTNode;
class ASTToken;
template<typename TargetType> class ASTExpression;
using ASTValueExpression = ASTExpression<Value>;
using ASTTypeExpression  = ASTExpression<Type>;
template<typename LiteralType> class ASTConstant;
class ASTIdentifier;
template<typename Operator> class ASTUnaryOp;
template<typename Operator> class ASTBinaryOp;
class ASTDeclaration;
class ASTIfStatement;

class ASTNode {
public:
    Location location;
    ASTNode(const Location& location);
    virtual ~ASTNode() = default;
    virtual void print(std::ostream& os, uint64_t indent = 0) const = 0;
    virtual void execute(Context& globals, Context& locals) const = 0;
};

class ASTToken : public ASTNode {
private:
    static std::vector<ASTToken> Instances;
public:
    static ASTToken* New(auto&&... args) {
        Instances.emplace_back(std::forward<decltype(args)>(args)...);
        return &Instances.back();
    }
public:
    std::string str;
    const int64_t type;
    ASTToken(const Location& location, const char* str, int64_t type);
    void print(std::ostream& os, uint64_t indent) const override;
    void execute(Context& globals, Context& locals) const override;
};

class ASTStatements : public ASTNode {
public:
    std::vector<ASTNode*> statements;
    ASTStatements(const Location& location);
    ASTStatements(const Location& location, ASTNode* node);
    ~ASTStatements() override;
    void print(std::ostream& os, uint64_t indent) const override;
    void execute(Context& globals, Context& locals) const override;
    ASTStatements& push(const Location& new_location, ASTNode* node);
};

template<typename TargetType>
class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& location) : ASTNode(location) {};
    void execute(Context& globals, Context& locals) const final {
        (void)this->eval(globals, locals);
    }
    virtual Reference<TargetType> eval(Context& globals, Context& locals) const = 0;
};

template<typename LiteralType>
class ASTConstant : public ASTValueExpression {
public:
    const ValueRef value;
    ASTConstant(const ASTToken& token) : ASTValueExpression(token.location), value(Value::FromLiteral<LiteralType>(token.str)) {}
    void print(std::ostream& os, uint64_t indent) const override {
        os << std::string(indent, ' ') << "Constant(" << static_cast<std::string>(value) << ")" << std::endl;
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        return value;
    }
};

class ASTIdentifier : public ASTValueExpression {
public:
    const std::string name;
    ASTIdentifier(const ASTToken& token);
    void print(std::ostream& os, uint64_t indent) const override;
    ValueRef eval(Context& globals, Context& locals) const override;
};

template<typename Operator>
class ASTUnaryOp : public ASTValueExpression {
private:
    static constexpr auto Func = Operator();
public:
    const ASTValueExpression* const expr;
    ASTUnaryOp(const Location& location, const ASTValueExpression* expr) : ASTValueExpression(location), expr(expr) {}
    ~ASTUnaryOp() override {
        delete expr;
    }
    void print(std::ostream& os, uint64_t indent) const override {
        os << std::string(indent, ' ') << "UnaryOp(" << GetOperatorString<Operator>() << ")" << std::endl;
        expr->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result = expr->eval(globals, locals);
        return result.eval_operation<Operator>();
    }
};

template<typename Operator>
class ASTBinaryOp : public ASTValueExpression {
private:
    static constexpr auto Func = Operator();
public:
    const ASTValueExpression* const left;
    const ASTValueExpression* const right;
    ASTBinaryOp(const Location& location, ASTValueExpression* left, ASTValueExpression* right)
        : ASTValueExpression(location), left(left), right(right) {}
    ~ASTBinaryOp() override {
        delete left;
        delete right;
    }
    void print(std::ostream& os, uint64_t indent) const override {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<Operator>() << ")" << std::endl;
        left->print(os, indent + 2);
        right->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result_left = left->eval(globals, locals);
        ValueRef result_right = right->eval(globals, locals);
        return result_left.eval_operation<Operator>(result_right);
    }
};

template<typename Operator>
class ASTBinaryOp<OperatorFunctors::Assign<Operator>> : public ASTValueExpression {
private:
    using AssignOperator = OperatorFunctors::Assign<Operator>;
    static constexpr auto Func = AssignOperator();
public:
    const ASTValueExpression* const left;
    const ASTValueExpression* const right;
    ASTBinaryOp(const Location& location, ASTValueExpression* left, ASTValueExpression* right)
        : ASTValueExpression(location), left(left), right(right) {}
    ~ASTBinaryOp() override {
        delete left;
        delete right;
    }
    void print(std::ostream& os, uint64_t indent) const override {
        os << std::string(indent, ' ') << "BinaryOp(" << GetOperatorString<AssignOperator>() << ")" << std::endl;
        left->print(os, indent + 2);
        right->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result_left = left->eval(globals, locals);
        ValueRef result_right = right->eval(globals, locals);
        ValueRef result = result_left.eval_operation<Operator>(result_right);
        return result_left = result;
    }
};

class ASTFunctionCallArguments : public ASTNode {
public:
    std::vector<const ASTValueExpression*> arguments;
    ASTFunctionCallArguments();
    ASTFunctionCallArguments(const Location& location, const ASTValueExpression* first_arg);
    ~ASTFunctionCallArguments() override;
    void print(std::ostream& os, uint64_t indent) const override;
    void execute(Context& globals, Context& locals) const override;
    ASTFunctionCallArguments& push_back(const Location& new_location, const ASTValueExpression* arg);
    Arguments eval_arguments(Context& globals, Context& locals) const;
};

class ASTFunctionCall : public ASTValueExpression {
public:
    const ASTValueExpression* const function;
    const ASTFunctionCallArguments* const arguments;
    ASTFunctionCall(const Location& location, const ASTValueExpression* function, const ASTFunctionCallArguments* arguments = 0);
    ~ASTFunctionCall() override;
    void print(std::ostream& os, uint64_t indent) const override;
    ValueRef eval(Context& globals, Context& locals) const override;
};

using ASTAddOp              = ASTBinaryOp<OperatorFunctors::Add>;
using ASTSubOp              = ASTBinaryOp<OperatorFunctors::Subtract>;
using ASTNegOp              = ASTUnaryOp<OperatorFunctors::Negate>;
using ASTMulOp              = ASTBinaryOp<OperatorFunctors::Multiply>;
using ASTDivOp              = ASTBinaryOp<OperatorFunctors::Divide>;
using ASTRemOp              = ASTBinaryOp<OperatorFunctors::Remainder>;

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

using ASTAssignOp           = ASTBinaryOp<OperatorFunctors::Assign<>>;
using ASTAddAssignOp        = ASTBinaryOp<OperatorFunctors::AddAssign>;
using ASTSubAssignOp        = ASTBinaryOp<OperatorFunctors::SubtractAssign>;
using ASTMulAssignOp        = ASTBinaryOp<OperatorFunctors::MultiplyAssign>;
using ASTDivAssignOp        = ASTBinaryOp<OperatorFunctors::DivideAssign>;
using ASTRemAssignOp        = ASTBinaryOp<OperatorFunctors::RemainderAssign>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp  = ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>;
using ASTLeftShiftAssignOp  = ASTBinaryOp<OperatorFunctors::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorFunctors::RightShiftAssign>;

class ASTDeclaration : public ASTNode {
public:
    const bool is_const = false;
    const ASTTypeExpression* const type;
    const ASTIdentifier* const identifier;
    const ASTValueExpression* const expr;
    const TypeRef inferred_type;
    ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const = false);
    ASTDeclaration(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const = false);
    ~ASTDeclaration() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTIfStatement : public ASTNode {
public:
    const ASTValueExpression* condition;
    const ASTStatements* const if_block;
    const ASTStatements* const else_block;
    ASTIfStatement(const Location& location, const ASTValueExpression* condition, const ASTStatements* const if_block, const ASTStatements* const else_block = nullptr);
    ~ASTIfStatement() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTForStatement : public ASTNode {
public:
    const ASTNode* const initializer; // Can be either a declaration or an expression
    const ASTValueExpression* const condition;
    const ASTValueExpression* const increment;
    const ASTStatements* const body;
    ASTForStatement(const Location& location, const ASTNode* initializer, const ASTValueExpression* condition, const ASTValueExpression* increment, const ASTStatements* body);
    ~ASTForStatement() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTContinueStatement : public ASTNode {
public:
    ASTContinueStatement(const Location& location);
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTBreakStatement : public ASTNode {
public:
    ASTBreakStatement(const Location& location);
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTFunctionParameter : public ASTNode {
public:
    const ASTIdentifier* const identifier;
    const ASTTypeExpression* const type;
    ASTFunctionParameter(const Location& location, const ASTIdentifier* identifier, const ASTTypeExpression* type);
    ~ASTFunctionParameter() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTFunctionSignature : public ASTNode {
public:
    std::vector<ASTFunctionParameter*> parameters;
    ASTFunctionParameter* spread_param;
    ASTTypeExpression* return_type;
    ASTFunctionSignature(const Location& location, const ASTTypeExpression* first_type, const ASTIdentifier* first_name);
    ASTFunctionSignature(std::initializer_list<ASTFunctionParameter*> params);
    ~ASTFunctionSignature() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
    ASTFunctionSignature& push(const Location& new_location, ASTFunctionParameter* param);
    ASTFunctionSignature& push_spread(const Location& new_location, ASTFunctionParameter* param);
    ASTFunctionSignature& set_return_type(const Location& new_location, ASTTypeExpression* return_type);
    Context collect_arguments(const Arguments& raw) const;
};

class ASTFunctionDefinition : public ASTNode {
public:
    const std::string name;
    const ASTFunctionSignature* const signature;
    const ASTStatements* const body;
protected:
    ASTFunctionDefinition(const char* name);
public:
    ASTFunctionDefinition(const Location& location, const ASTIdentifier* name, const ASTFunctionSignature* signature, const ASTStatements* body);
    ~ASTFunctionDefinition() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};
