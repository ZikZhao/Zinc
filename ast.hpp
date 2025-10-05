#pragma once
#include "pch.hpp"
#include "value.hpp"
#include "type.hpp"
using namespace std::literals::string_literals;

struct Location {
    struct {
        uint64_t line;
        uint64_t column;
    } begin, end;
};

std::ostream& operator<< (std::ostream& os, const Location& loc);

struct LeftShiftFunctor {
    ValueRef operator() (const ValueRef& left, const ValueRef& right) const;
};

struct RightShiftFunctor {
    ValueRef operator() (const ValueRef& left, const ValueRef& right) const;
};

// Assignment functor that performs operation and assignment
template<typename FuncType = void>
struct AssignFunctor {
    static constexpr auto Func = FuncType();
    ValueRef operator() (ValueRef& left, ValueRef& right) const {
        const ValueRef result = Func(left, right);
        left = result;
        return left;
    }
};

// Specialization for simple assignment
template<>
struct AssignFunctor<void> {
    ValueRef operator() (ValueRef& left, ValueRef& right) const {
        return left = right;
    }
};

class ASTNode;
class ASTToken;
template<typename TargetType> class ASTExpression;
using ASTValueExpression = ASTExpression<Value>;
using ASTTypeExpression  = ASTExpression<Type>;
class ASTConstant;
class ASTIdentifier;
template<FixedString Operator, typename FuncType> class ASTUnaryOp;
template<FixedString Operator, typename FuncType> class ASTBinaryOp;
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

class ASTConstant : public ASTValueExpression {
public:
    const ValueRef value;
    ASTConstant(const ASTToken& token, LiteralType type);
    void print(std::ostream& os, uint64_t indent) const override;
    ValueRef eval(Context& globals, Context& locals) const override;
};

class ASTIdentifier : public ASTValueExpression {
public:
    const std::string name;
    ASTIdentifier(const ASTToken& token);
    void print(std::ostream& os, uint64_t indent) const override;
    ValueRef eval(Context& globals, Context& locals) const override;
};

template<FixedString Operator, typename FuncType>
class ASTUnaryOp : public ASTValueExpression {
private:
    static constexpr auto Func = FuncType();
public:
    const ASTValueExpression* const expr;
    ASTUnaryOp(const Location& location, const ASTValueExpression* expr) : ASTValueExpression(location), expr(expr) {}
    ~ASTUnaryOp() override {
        delete expr;
    }
    void print(std::ostream& os, uint64_t indent) const override {
        os << std::string(indent, ' ') << "UnaryOp(" << *Operator << ")" << std::endl;
        expr->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result = expr->eval(globals, locals);
        return Func(result);
    }
};

template<FixedString Operator, typename FuncType>
class ASTBinaryOp : public ASTValueExpression {
private:
    static constexpr auto Func = FuncType();
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
        os << std::string(indent, ' ') << "BinaryOp(" << *Operator << ")" << std::endl;
        left->print(os, indent + 2);
        right->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result_left = left->eval(globals, locals);
        ValueRef result_right = right->eval(globals, locals);
        return Func(result_left, result_right);
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
    ASTFunctionCallArguments& push_back(const ASTValueExpression* arg);
    decltype(auto) size() const;
    decltype(auto) operator[] (uint64_t index) const;
    decltype(auto) begin();
    decltype(auto) begin() const;
    decltype(auto) end();
    decltype(auto) end() const;
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

using ASTAddOp              = ASTBinaryOp<"+", std::plus<>>;
using ASTSubOp              = ASTBinaryOp<"-", std::minus<>>;
using ASTNegOp              = ASTUnaryOp<"~", std::negate<>>;
using ASTMulOp              = ASTBinaryOp<"*", std::multiplies<>>;
using ASTDivOp              = ASTBinaryOp<"/", std::divides<>>;
using ASTRemOp              = ASTBinaryOp<"%", std::modulus<>>;

using ASTEqualOp            = ASTBinaryOp<"==", std::equal_to<>>;
using ASTNotEqualOp         = ASTBinaryOp<"!=", std::not_equal_to<>>;
using ASTLessThanOp         = ASTBinaryOp<"<", std::less<>>;
using ASTLessEqualOp        = ASTBinaryOp<"<=", std::less_equal<>>;
using ASTGreaterThanOp      = ASTBinaryOp<">", std::greater<>>;
using ASTGreaterEqualOp     = ASTBinaryOp<">=", std::greater_equal<>>;

using ASTLogicalAndOp       = ASTBinaryOp<"&&", std::logical_and<>>;
using ASTLogicalOrOp        = ASTBinaryOp<"||", std::logical_or<>>;
using ASTLogicalNotOp       = ASTUnaryOp<"!", std::logical_not<>>;

using ASTBitwiseAndOp       = ASTBinaryOp<"&", std::bit_and<>>;
using ASTBitwiseOrOp        = ASTBinaryOp<"|", std::bit_or<>>;
using ASTBitwiseXorOp       = ASTBinaryOp<"^", std::bit_xor<>>;
using ASTBitwiseNotOp       = ASTUnaryOp<"~", std::bit_not<>>;
using ASTLeftShiftOp        = ASTBinaryOp<"<<", LeftShiftFunctor>;
using ASTRightShiftOp       = ASTBinaryOp<">>", RightShiftFunctor>;

using ASTAssignOp           = ASTBinaryOp<"=", AssignFunctor<>>;
using ASTAddAssignOp        = ASTBinaryOp<"+=", AssignFunctor<std::plus<>>>;
using ASTSubAssignOp        = ASTBinaryOp<"-=", AssignFunctor<std::minus<>>>;
using ASTMulAssignOp        = ASTBinaryOp<"*=", AssignFunctor<std::multiplies<>>>;
using ASTDivAssignOp        = ASTBinaryOp<"/=", AssignFunctor<std::divides<>>>;
using ASTRemAssignOp        = ASTBinaryOp<"%=", AssignFunctor<std::modulus<>>>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<"&=", AssignFunctor<std::bit_and<>>>;
using ASTBitwiseOrAssignOp  = ASTBinaryOp<"|=", AssignFunctor<std::bit_or<>>>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<"^=", AssignFunctor<std::bit_xor<>>>;
using ASTLeftShiftAssignOp  = ASTBinaryOp<"<<=", AssignFunctor<LeftShiftFunctor>>;
using ASTRightShiftAssignOp = ASTBinaryOp<">>=", AssignFunctor<RightShiftFunctor>>;

class ASTDeclaration : public ASTNode {
public:
    const bool is_const = false;
    const ASTTypeExpression* const type;
    const ASTIdentifier* const identifier;
    const ASTValueExpression* const expr;
    const TypeRef inferred_type;
    ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const = false);
    ASTDeclaration(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const = false);
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTIfStatement : public ASTNode {
public:
    const ASTValueExpression* condition;
    const ASTStatements* const if_block;
    const ASTStatements* const else_block;
    ASTIfStatement(const Location& location, const ASTValueExpression* condition, const ASTStatements* const if_block, const ASTStatements* const else_block = nullptr);
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
    const ASTTypeExpression* const type;
    const ASTIdentifier* const identifier;
    ASTFunctionParameter(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* name);
    ~ASTFunctionParameter() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
};

class ASTFunctionSignature : public ASTNode {
public:
    std::vector<ASTFunctionParameter*> parameters;
    ASTTypeExpression* return_type;
    ASTFunctionSignature(const Location& location, const ASTTypeExpression* first_type, const ASTIdentifier* first_name);
    ~ASTFunctionSignature() override;
    void execute(Context& globals, Context& locals) const override;
    void print(std::ostream& os, uint64_t indent) const override;
    ASTFunctionSignature& push(const Location& new_location, ASTFunctionParameter* param);
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
    virtual ValueRef call(Context& globals, const ASTFunctionCallArguments& arguments) const;
protected:
    Context prepare_locals(Context& globals, const ASTFunctionCallArguments& arguments) const;
};
