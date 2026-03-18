#pragma once
#include "pch.hpp"

#include "object.hpp"

enum class OperatorCode : std::uint8_t {
    Add,
    Subtract,
    Negate,
    Multiply,
    Divide,
    Remainder,
    Increment,
    Decrement,
    Equal,
    NotEqual,
    LessThan,
    LessEqual,
    GreaterThan,
    GreaterEqual,
    LogicalAnd,
    LogicalOr,
    LogicalNot,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    BitwiseNot,
    LeftShift,
    RightShift,
    Assign,
    SIZE,
    AddAssign,
    SubtractAssign,
    MultiplyAssign,
    DivideAssign,
    RemainderAssign,
    LogicalAndAssign,
    LogicalOrAssign,
    BitwiseAndAssign,
    BitwiseOrAssign,
    BitwiseXorAssign,
    LeftShiftAssign,
    RightShiftAssign,
};

struct ASTNode;
struct ASTRoot;
struct ASTLocalBlock;

struct ASTExpression;
struct ASTExplicitTypeExpr;
struct ASTSelfExpr;
struct ASTConstant;
struct ASTIdentifier;
struct ASTAccessChain;
struct ASTParenExpr;
struct ASTUnaryOp;
struct ASTBinaryOp;

struct ASTFieldInitialization;
struct ASTStructInitialization;
struct ASTArrayInitialization;
struct ASTArrayAccess;
struct ASTFunctionCall;
struct ASTPrimitiveType;
struct ASTFunctionType;
struct ASTStructType;
struct ASTArrayType;
struct ASTMutableType;
struct ASTReferenceType;
struct ASTPointerType;
struct ASTExpressionStatement;
struct ASTDeclaration;
struct ASTFieldDeclaration;
struct ASTTypeAlias;
struct ASTIfStatement;
struct ASTForStatement;
struct ASTContinueStatement;
struct ASTBreakStatement;
struct ASTReturnStatement;
struct ASTFunctionParameter;
struct ASTFunctionDefinition;
struct ASTConstructorDestructorDefinition;
struct ASTOperatorOverloadDefinition;
struct ASTClassDefinition;
struct ASTNamespaceDefinition;
struct ASTTemplateParameter;
struct ASTTemplateDefinition;
struct ASTTemplateSpecialization;
struct ASTStaticAssertStatement;

// All AST node types (base classes and final classes)
using ASTNodeVariant = std::variant<
    std::monostate,
    // Root and blocks
    const ASTRoot*,
    const ASTLocalBlock*,
    // Expressions
    const ASTParenExpr*,
    const ASTConstant*,
    const ASTSelfExpr*,
    const ASTIdentifier*,
    const ASTAccessChain*,
    const ASTUnaryOp*,
    const ASTBinaryOp*,
    // Member access and calls
    const ASTStructInitialization*,
    const ASTFunctionCall*,
    // Type expressions
    const ASTPrimitiveType*,
    const ASTFunctionType*,
    const ASTFieldDeclaration*,
    const ASTStructType*,
    const ASTMutableType*,
    const ASTReferenceType*,
    const ASTPointerType*,
    // Statements
    const ASTExpressionStatement*,
    const ASTDeclaration*,
    const ASTTypeAlias*,
    const ASTIfStatement*,
    const ASTForStatement*,
    const ASTContinueStatement*,
    const ASTBreakStatement*,
    const ASTReturnStatement*,
    // Functions and classes
    const ASTFunctionDefinition*,
    const ASTConstructorDestructorDefinition*,
    const ASTOperatorOverloadDefinition*,
    const ASTClassDefinition*,
    const ASTNamespaceDefinition*,
    // Templates
    const ASTTemplateDefinition*,
    const ASTTemplateSpecialization*,
    // Static asserts
    const ASTStaticAssertStatement*>;

// All expression node types (all ASTExpression derivatives)
using ASTExprVariant = std::variant<
    std::monostate,
    // Basic expressions
    const ASTParenExpr*,
    const ASTConstant*,
    const ASTSelfExpr*,
    const ASTIdentifier*,
    const ASTAccessChain*,
    const ASTUnaryOp*,
    const ASTBinaryOp*,
    // Member access and calls
    const ASTStructInitialization*,
    const ASTArrayInitialization*,
    const ASTArrayAccess*,
    const ASTFunctionCall*,
    // Type expressions
    const ASTPrimitiveType*,
    const ASTArrayType*,
    const ASTFunctionType*,
    const ASTStructType*,
    const ASTMutableType*,
    const ASTReferenceType*,
    const ASTPointerType*>;

struct NodePtrVisitor {
    auto operator()(ASTNodeVariant variant) -> const ASTNode* { return std::visit(*this, variant); }
    auto operator()(ASTExprVariant variant) -> const ASTNode* { return std::visit(*this, variant); }
    template <typename T>
    auto operator()(const T* node) -> const ASTNode* {
        return node;
    }
    auto operator()(std::monostate) -> const ASTNode* { return nullptr; }
};

struct ASTNode : public GlobalMemory::MonotonicAllocated {
    Location location;
    ASTNode(const Location& loc) noexcept : location(loc) {}
};

struct ASTLocalBlock final : public ASTNode {
    std::span<ASTNodeVariant> statements;
};

struct ASTRoot final : public ASTNode {
    std::span<ASTNodeVariant> statements;
};

struct ASTExpression : public ASTNode {};

struct ASTExplicitTypeExpr : public ASTExpression {};

struct ASTSelfExpr final : public ASTExpression {
    bool is_type;
};

struct ASTIdentifier final : public ASTExpression {
    std::string_view str;
};

struct ASTAccessChain final : public ASTExpression {
    ASTExprVariant base;
    std::span<std::string_view> members;
    std::span<ASTExprVariant> instantiation_args;
};

struct ASTConstant final : public ASTExpression {
    const Value* value;
};

struct ASTParenExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTUnaryOp final : public ASTExpression {
    OperatorCode opcode;
    ASTExprVariant expr;
};

struct ASTBinaryOp final : public ASTExpression {
    OperatorCode opcode;
    ASTExprVariant left;
    ASTExprVariant right;
};

struct ASTFieldInitialization final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant value;
};

struct ASTStructInitialization final : public ASTExpression {
    ASTExprVariant struct_type;
    std::span<ASTFieldInitialization> field_inits;
};

struct ASTArrayInitialization final : public ASTExpression {
    std::span<ASTExprVariant> elements;
};

struct ASTArrayAccess final : public ASTExpression {
    ASTExprVariant base;
    ASTExprVariant length;
};

struct ASTFunctionCall final : public ASTExpression {
    ASTExprVariant function;
    std::span<ASTExprVariant> arguments;
};

struct ASTPrimitiveType final : public ASTExplicitTypeExpr {
    const Type* type;
};

struct ASTFunctionType final : public ASTExplicitTypeExpr {
    std::span<ASTExprVariant> parameter_types;
    ASTExprVariant return_type;
};

struct ASTFieldDeclaration final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant type;
};

struct ASTStructType final : public ASTExplicitTypeExpr {
    std::span<ASTFieldDeclaration> fields;
};

struct ASTArrayType final : public ASTExplicitTypeExpr {
    ASTExprVariant element_type;
    ASTExprVariant length;
};

struct ASTMutableType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner;
};

struct ASTReferenceType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner;
    bool is_moved;
};

struct ASTPointerType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner;
};

// struct ASTTemplateInstantiation final : public ASTExpression {
//     std::string_view template_identifier;
//     std::span<ASTExprVariant> arguments;
// };

// struct ASTTemplateMemberAccessInstantiation final : public ASTExpression {
//     ASTExprVariant target;
//     std::string_view member;
//     std::span<ASTExprVariant> arguments;
// };

struct ASTExpressionStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTDeclaration final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant declared_type;
    ASTExprVariant expr;
    bool is_mutable;
    bool is_constant;
};

struct ASTTypeAlias final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant type;
};

struct ASTIfStatement final : public ASTNode {
    ASTExprVariant condition;
    const ASTLocalBlock* if_block;
    const ASTLocalBlock* else_block;
};

struct ASTForStatement final : public ASTNode {
    const ASTDeclaration* initializer_decl;
    ASTExprVariant initializer_expr;
    ASTExprVariant condition;
    ASTExprVariant increment;
    const ASTLocalBlock* body;
};

struct ASTContinueStatement final : public ASTNode {};

struct ASTBreakStatement final : public ASTNode {};

struct ASTReturnStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTFunctionParameter final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant type;
    ASTExprVariant default_value;
    bool is_mutable;
    bool is_variadic;
};

struct ASTFunctionDefinition final : public ASTNode {
    std::string_view identifier;
    std::span<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    std::span<ASTNodeVariant> body;
    bool declared_const;
    bool declared_static;
};

struct ASTConstructorDestructorDefinition final : public ASTNode {
    std::span<ASTFunctionParameter> parameters;
    std::span<ASTNodeVariant> body;
    bool is_constructor;
    bool declared_const;
};

struct ASTOperatorOverloadDefinition final : public ASTNode {
    OperatorCode opcode;
    ASTFunctionParameter left;
    ASTFunctionParameter* right;
    ASTExprVariant return_type;
    std::span<ASTNodeVariant> body;
    bool declared_const;
};

struct ASTClassDefinition final : public ASTNode {
    std::string_view identifier;
    std::string_view extends;
    std::span<std::string_view> implements;
    std::span<const ASTConstructorDestructorDefinition*> constructors;
    const ASTConstructorDestructorDefinition* destructor;
    std::span<const ASTDeclaration*> fields;
    std::span<const ASTTypeAlias*> aliases;
    std::span<const ASTFunctionDefinition*> functions;
    std::span<const ASTClassDefinition*> classes;
};

struct ASTNamespaceDefinition final : public ASTNode {
    std::string_view identifier;
    std::span<ASTNodeVariant> items;
};

struct ASTTemplateParameter final : public ASTNode {
    bool is_nttp;  // true if non-type template parameter, false if type template parameter
    std::string_view identifier;
    ASTExprVariant constraint;
    ASTExprVariant default_value;
};

struct ASTTemplateDefinition final : public ASTNode {
    std::string_view identifier;
    std::span<ASTTemplateParameter> parameters;
    ASTNodeVariant target_node;
};

struct ASTTemplateSpecialization final : public ASTNode {
    std::string_view identifier;
    std::span<ASTTemplateParameter> parameters;
    std::span<ASTExprVariant> patterns;
    ASTNodeVariant target_node;
};

struct ASTStaticAssertStatement final : public ASTNode {
    ASTExprVariant condition;
    ASTExprVariant message;
};

enum class OperatorGroup : std::uint8_t {
    Arithmetic,
    Comparison,
    Logical,
    Bitwise,
    Assignment,
    UnaryArithmetic,
    UnaryLogical,
    UnaryBitwise,
};

constexpr auto GetOperatorGroup(OperatorCode opcode) -> OperatorGroup {
    switch (opcode) {
    case OperatorCode::Add:
    case OperatorCode::Subtract:
    case OperatorCode::Multiply:
    case OperatorCode::Divide:
    case OperatorCode::Remainder:
        return OperatorGroup::Arithmetic;
    case OperatorCode::Negate:
    case OperatorCode::Increment:
    case OperatorCode::Decrement:
        return OperatorGroup::UnaryArithmetic;
    case OperatorCode::Equal:
    case OperatorCode::NotEqual:
    case OperatorCode::LessThan:
    case OperatorCode::LessEqual:
    case OperatorCode::GreaterThan:
    case OperatorCode::GreaterEqual:
        return OperatorGroup::Comparison;
    case OperatorCode::LogicalAnd:
    case OperatorCode::LogicalOr:
        return OperatorGroup::Logical;
    case OperatorCode::LogicalNot:
        return OperatorGroup::UnaryLogical;
    case OperatorCode::BitwiseAnd:
    case OperatorCode::BitwiseOr:
    case OperatorCode::BitwiseXor:
        return OperatorGroup::Bitwise;
    case OperatorCode::BitwiseNot:
        return OperatorGroup::UnaryBitwise;
    case OperatorCode::Assign:
    case OperatorCode::AddAssign:
    case OperatorCode::SubtractAssign:
    case OperatorCode::MultiplyAssign:
    case OperatorCode::DivideAssign:
    case OperatorCode::RemainderAssign:
    case OperatorCode::LogicalAndAssign:
    case OperatorCode::LogicalOrAssign:
    case OperatorCode::BitwiseAndAssign:
    case OperatorCode::BitwiseOrAssign:
    case OperatorCode::BitwiseXorAssign:
    case OperatorCode::LeftShiftAssign:
    case OperatorCode::RightShiftAssign:
        return OperatorGroup::Assignment;
    default:
        UNREACHABLE();
    }
};

constexpr auto GetAssignmentEquivalent(OperatorCode opcode) -> OperatorCode {
    switch (opcode) {
    case OperatorCode::AddAssign:
        return OperatorCode::Add;
    case OperatorCode::SubtractAssign:
        return OperatorCode::Subtract;
    case OperatorCode::MultiplyAssign:
        return OperatorCode::Multiply;
    case OperatorCode::DivideAssign:
        return OperatorCode::Divide;
    case OperatorCode::RemainderAssign:
        return OperatorCode::Remainder;
    case OperatorCode::LogicalAndAssign:
        return OperatorCode::LogicalAnd;
    case OperatorCode::LogicalOrAssign:
        return OperatorCode::LogicalOr;
    case OperatorCode::BitwiseAndAssign:
        return OperatorCode::BitwiseAnd;
    case OperatorCode::BitwiseOrAssign:
        return OperatorCode::BitwiseOr;
    case OperatorCode::BitwiseXorAssign:
        return OperatorCode::BitwiseXor;
    case OperatorCode::LeftShiftAssign:
        return OperatorCode::LeftShift;
    case OperatorCode::RightShiftAssign:
        return OperatorCode::RightShift;
    default:
        UNREACHABLE();
    }
}

constexpr auto GetOperatorString(OperatorCode opcode) -> std::string_view {
    switch (opcode) {
    case OperatorCode::Add:
        return "+";
    case OperatorCode::Subtract:
    case OperatorCode::Negate:
        return "-";
    case OperatorCode::Multiply:
        return "*";
    case OperatorCode::Divide:
        return "/";
    case OperatorCode::Remainder:
        return "%";
    case OperatorCode::Increment:
        return "++";
    case OperatorCode::Decrement:
        return "--";
    case OperatorCode::Equal:
        return "==";
    case OperatorCode::NotEqual:
        return "!=";
    case OperatorCode::LessThan:
        return "<";
    case OperatorCode::LessEqual:
        return "<=";
    case OperatorCode::GreaterThan:
        return ">";
    case OperatorCode::GreaterEqual:
        return ">=";
    case OperatorCode::LogicalAnd:
        return "&&";
    case OperatorCode::LogicalOr:
        return "||";
    case OperatorCode::LogicalNot:
        return "!";
    case OperatorCode::BitwiseAnd:
        return "&";
    case OperatorCode::BitwiseOr:
        return "|";
    case OperatorCode::BitwiseXor:
        return "^";
    case OperatorCode::BitwiseNot:
        return "~";
    case OperatorCode::LeftShift:
        return "<<";
    case OperatorCode::RightShift:
        return ">>";
    case OperatorCode::Assign:
        return "=";
    case OperatorCode::AddAssign:
        return "+=";
    case OperatorCode::SubtractAssign:
        return "-=";
    case OperatorCode::MultiplyAssign:
        return "*=";
    case OperatorCode::DivideAssign:
        return "/=";
    case OperatorCode::RemainderAssign:
        return "%=";
    case OperatorCode::LogicalAndAssign:
        return "&&=";
    case OperatorCode::LogicalOrAssign:
        return "||=";
    case OperatorCode::BitwiseAndAssign:
        return "&=";
    case OperatorCode::BitwiseOrAssign:
        return "|=";
    case OperatorCode::BitwiseXorAssign:
        return "^=";
    case OperatorCode::LeftShiftAssign:
        return "<<=";
    case OperatorCode::RightShiftAssign:
        return ">>=";
    default:
        UNREACHABLE();
    }
}
