#pragma once
#include <string>
#include <stdexcept>
#include <variant>
#include "ref.hpp"
using namespace std::literals::string_literals;

struct Location {
    struct {
        uint64_t line;
        uint64_t column;
    } begin, end;
};

namespace OpStr {
    constexpr const char NEG[]     = "-";
    constexpr const char NOT[]     = "not";
    constexpr const char BIT_NOT[] = "~";
    constexpr const char ADD[]     = "+";
    constexpr const char SUB[]     = "-";
    constexpr const char MUL[]     = "*";
    constexpr const char DIV[]     = "/";
    constexpr const char EQUAL[]      = "==";
    constexpr const char NOT_EQUAL[]     = "!=";
    constexpr const char LESS_THAN[]      = "<";
    constexpr const char LESS_THAN_EQUAL[]     = "<=";
    constexpr const char GREATER_THAN[]      = ">";
    constexpr const char GREATE_THAN_EQUAL[]     = ">=";
    constexpr const char AND[]     = "and";
    constexpr const char OR[]      = "or";
    constexpr const char BITWISE_AND[] = "&";
    constexpr const char BITWISE_OR[]  = "|";
    constexpr const char BITWISE_XOR[] = "^";
    constexpr const char REM[]     = "%";
    constexpr const char EXP[]     = "**";
    constexpr const char LEFT_SHIFT[]  = "<<";
    constexpr const char RIGHT_SHIFT[] = ">>";
    constexpr const char ASSIGN[]  = "=";
    constexpr const char ADD_ASSIGN[] = "+=";
    constexpr const char SUB_ASSIGN[] = "-=";
    constexpr const char MUL_ASSIGN[] = "*=";
    constexpr const char DIV_ASSIGN[] = "/=";
    constexpr const char REM_ASSIGN[] = "%=";
    constexpr const char EXP_ASSIGN[] = "**=";
    constexpr const char BITWISE_AND_ASSIGN[] = "&=";
    constexpr const char BITWISE_OR_ASSIGN[]  = "|=";
    constexpr const char BITWISE_XOR_ASSIGN[] = "^=";
    constexpr const char LEFT_SHIFT_ASSIGN[]  = "<<=";
    constexpr const char RIGHT_SHIFT_ASSIGN[] = ">>=";
}

struct ExpFunctor {
    ValueRef operator() (const ValueRef& left, const ValueRef& right) const {
        throw std::runtime_error("Exponentiation not implemented");
    }
};

struct LeftShiftFunctor {
    ValueRef operator() (const ValueRef& left, const ValueRef& right) const {
        throw std::runtime_error("Left shift not implemented");
    }
};

struct RightShiftFunctor {
    ValueRef operator() (const ValueRef& left, const ValueRef& right) const {
        throw std::runtime_error("Right shift not implemented");
    }
};

template<typename FuncType = void>
struct AssignFunctor {
    static constexpr auto Func = FuncType();
    ValueRef operator() (ValueRef& left, ValueRef& right) const {
        const ValueRef result = Func(left, right);
        left = result;
        return left;
    }
};

template<>
struct AssignFunctor<void> {
    ValueRef operator() (ValueRef& a, ValueRef& b) const {
        a = b;
        return a;
    }
};

class ASTNode;
class ASTToken;
template<typename TargetType> class ASTExpression;
using ASTValueExpression = ASTExpression<Value>;
using ASTTypeExpression  = ASTExpression<Type>;
class ASTConstant;
class ASTIdentifier;
template<const char* Operator, typename FuncType> class ASTUnaryOp;
template<const char* Operator, typename FuncType> class ASTBinaryOp;
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
    static ASTToken* New(auto... args) {
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
    const ValueRef ref;
    ASTConstant(const ASTToken& token);
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

template<const char* Operator, typename FuncType>
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
        os << std::string(indent, ' ') << "UnaryOp("s + Operator + ")"s << std::endl;
        expr->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result = expr->eval(globals, locals);
        return Func(result);
    }
};

template<const char* Operator, typename FuncType>
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
        os << std::string(indent, ' ') << "BinaryOp("s + Operator + ")"s << std::endl;
        left->print(os, indent + 2);
        right->print(os, indent + 2);
    }
    ValueRef eval(Context& globals, Context& locals) const override {
        ValueRef result_left = left->eval(globals, locals);
        ValueRef result_right = right->eval(globals, locals);
        return Func(result_left, result_right);
    }
};

using ASTAddOp              = ASTBinaryOp<OpStr::ADD, std::plus<>>;
using ASTSubOp              = ASTBinaryOp<OpStr::SUB, std::minus<>>;
using ASTNegOp              = ASTUnaryOp<OpStr::NEG, std::negate<>>;
using ASTMulOp              = ASTBinaryOp<OpStr::MUL, std::multiplies<>>;
using ASTDivOp              = ASTBinaryOp<OpStr::DIV, std::divides<>>;
using ASTRemOp              = ASTBinaryOp<OpStr::REM, std::modulus<>>;
using ASTExpOp              = ASTBinaryOp<OpStr::EXP, ExpFunctor>;

using ASTEqualOp            = ASTBinaryOp<OpStr::EQUAL, std::equal_to<>>;
using ASTNotEqualOp         = ASTBinaryOp<OpStr::NOT_EQUAL, std::not_equal_to<>>;
using ASTLessThanOp         = ASTBinaryOp<OpStr::LESS_THAN, std::less<>>;
using ASTLessEqualOp        = ASTBinaryOp<OpStr::LESS_THAN_EQUAL, std::less_equal<>>;
using ASTGreaterThanOp      = ASTBinaryOp<OpStr::GREATER_THAN, std::greater<>>;
using ASTGreaterEqualOp     = ASTBinaryOp<OpStr::GREATE_THAN_EQUAL, std::greater_equal<>>;

using ASTLogicalAndOp       = ASTBinaryOp<OpStr::AND, std::logical_and<>>;
using ASTLogicalOrOp        = ASTBinaryOp<OpStr::OR, std::logical_or<>>;
using ASTLogicalNotOp       = ASTUnaryOp<OpStr::NOT, std::logical_not<>>;

using ASTBitwiseAndOp       = ASTBinaryOp<OpStr::BITWISE_AND, std::bit_and<>>;
using ASTBitwiseOrOp        = ASTBinaryOp<OpStr::BITWISE_OR, std::bit_or<>>;
using ASTBitwiseXorOp       = ASTBinaryOp<OpStr::BITWISE_XOR, std::bit_xor<>>;
using ASTBitwiseNotOp       = ASTUnaryOp<OpStr::BIT_NOT, std::bit_not<>>;
using ASTLeftShiftOp        = ASTBinaryOp<OpStr::LEFT_SHIFT, LeftShiftFunctor>;
using ASTRightShiftOp       = ASTBinaryOp<OpStr::RIGHT_SHIFT, RightShiftFunctor>;

using ASTAssignOp           = ASTBinaryOp<OpStr::ASSIGN, AssignFunctor<>>;
using ASTAddAssignOp        = ASTBinaryOp<OpStr::ADD_ASSIGN, AssignFunctor<std::plus<>>>;
using ASTSubAssignOp        = ASTBinaryOp<OpStr::SUB_ASSIGN, AssignFunctor<std::minus<>>>;
using ASTMulAssignOp        = ASTBinaryOp<OpStr::MUL_ASSIGN, AssignFunctor<std::multiplies<>>>;
using ASTDivAssignOp        = ASTBinaryOp<OpStr::DIV_ASSIGN, AssignFunctor<std::divides<>>>;
using ASTRemAssignOp        = ASTBinaryOp<OpStr::REM_ASSIGN, AssignFunctor<std::modulus<>>>;
using ASTExpAssignOp        = ASTBinaryOp<OpStr::EXP_ASSIGN, AssignFunctor<ExpFunctor>>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OpStr::BITWISE_AND_ASSIGN, AssignFunctor<std::bit_and<>>>;
using ASTBitwiseOrAssignOp  = ASTBinaryOp<OpStr::BITWISE_OR_ASSIGN, AssignFunctor<std::bit_or<>>>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OpStr::BITWISE_XOR_ASSIGN, AssignFunctor<std::bit_xor<>>>;
using ASTLeftShiftAssignOp  = ASTBinaryOp<OpStr::LEFT_SHIFT_ASSIGN, AssignFunctor<LeftShiftFunctor>>;
using ASTRightShiftAssignOp = ASTBinaryOp<OpStr::RIGHT_SHIFT_ASSIGN, AssignFunctor<RightShiftFunctor>>;

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
