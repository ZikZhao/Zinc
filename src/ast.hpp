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
    PostIncrement,
    Decrement,
    PostDecrement,
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
    Call,
    Index,
    Deref,
    Pointer
};

struct ASTNode;
struct ASTRoot;
struct ASTLocalBlock;

struct ASTExpression;
struct ASTExplicitTypeExpr;
struct ASTSelfExpr;
struct ASTConstant;
struct ASTStringConstant;
struct ASTIdentifier;
struct ASTMemberAccess;
struct ASTPointerAccess;
struct ASTIndexAccess;
struct ASTParenExpr;
struct ASTAddressOfExpr;
struct ASTDereference;
struct ASTUnaryOp;
struct ASTBinaryOp;

struct ASTFieldInitialization;
struct ASTStructInitialization;
struct ASTArrayInitialization;
struct ASTMoveExpr;
struct ASTFunctionCall;
struct ASTTemplateInstantiation;
struct ASTTernaryOp;
struct ASTAs;
struct ASTLambda;
struct ASTPrimitiveType;
struct ASTStringViewType;
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
struct ASTSwitchCase;
struct ASTSwitchStatement;
struct ASTForStatement;
struct ASTContinueStatement;
struct ASTBreakStatement;
struct ASTReturnStatement;
struct ASTFunctionParameter;
struct ASTFunctionDefinition;
struct ASTCtorDtorDefinition;
struct ASTOperatorDefinition;
struct ASTClassDefinition;
struct ASTEnumDefinition;
struct ASTNamespaceDefinition;
struct ASTTemplateParameter;
struct ASTTemplateDefinition;
struct ASTTemplateSpecialization;
struct ASTThrowStatement;
struct ASTStaticAssertStatement;
struct ASTImportStatement;
struct ASTCppBlock;

// All AST node types (base classes and final classes)
using ASTNodeVariant = std::variant<
    std::monostate,
    // Root and blocks
    const ASTRoot*,
    const ASTLocalBlock*,
    // Statements
    const ASTExpressionStatement*,
    const ASTDeclaration*,
    const ASTTypeAlias*,
    const ASTIfStatement*,
    const ASTSwitchStatement*,
    const ASTForStatement*,
    const ASTContinueStatement*,
    const ASTBreakStatement*,
    const ASTReturnStatement*,
    // Functions and classes
    const ASTFunctionDefinition*,
    const ASTCtorDtorDefinition*,
    const ASTOperatorDefinition*,
    const ASTClassDefinition*,
    const ASTEnumDefinition*,
    const ASTNamespaceDefinition*,
    // Templates
    const ASTTemplateDefinition*,
    const ASTTemplateSpecialization*,
    // Static asserts
    const ASTStaticAssertStatement*,
    // Error handling
    const ASTThrowStatement*,
    // Import
    const ASTImportStatement*,
    // C++ block
    const ASTCppBlock*>;

// All expression node types (all ASTExpression derivatives)
using ASTExprVariant = std::variant<
    std::monostate,
    // Basic expressions
    const ASTParenExpr*,
    const ASTConstant*,
    const ASTStringConstant*,
    const ASTSelfExpr*,
    const ASTIdentifier*,
    const ASTMemberAccess*,
    const ASTPointerAccess*,
    const ASTIndexAccess*,
    const ASTAddressOfExpr*,
    const ASTDereference*,
    const ASTUnaryOp*,
    const ASTBinaryOp*,
    // Complex expressions
    const ASTStructInitialization*,
    const ASTArrayInitialization*,
    const ASTMoveExpr*,
    const ASTFunctionCall*,
    const ASTTemplateInstantiation*,
    const ASTTernaryOp*,
    const ASTAs*,
    const ASTLambda*,
    // Type expressions
    const ASTPrimitiveType*,
    const ASTStringViewType*,
    const ASTArrayType*,
    const ASTFunctionType*,
    const ASTStructType*,
    const ASTMutableType*,
    const ASTReferenceType*,
    const ASTPointerType*>;

struct ASTNodePtrGetter {
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
    mutable Scope* scope = nullptr;
    mutable bool type_checked = false;
};

struct ASTExpression : public ASTNode {};

struct ASTExplicitTypeExpr : public ASTExpression {};

struct ASTSelfExpr final : public ASTExpression {
    bool is_type;
};

struct ASTIdentifier final : public ASTExpression {
    strview str;
};

struct ASTMemberAccess final : public ASTExpression {
    ASTExprVariant base;
    strview member;
};

struct ASTPointerAccess final : public ASTExpression {
    ASTExprVariant base;
    strview member;
};

struct ASTIndexAccess final : public ASTExpression {
    ASTExprVariant base;
    ASTExprVariant index;
};

struct ASTConstant final : public ASTExpression {
    Value* value;
};

struct ASTStringConstant final : public ASTExpression {
    strview value;
};

struct ASTParenExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTAddressOfExpr final : public ASTExpression {
    ASTExprVariant operand;
    bool is_mutable;
};

struct ASTDereference final : public ASTExpression {
    ASTExprVariant operand;
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
    strview identifier;
    ASTExprVariant value;
};

struct ASTStructInitialization final : public ASTExpression {
    ASTExprVariant struct_type;
    std::span<ASTFieldInitialization> field_inits;
};

struct ASTArrayInitialization final : public ASTExpression {
    std::span<ASTExprVariant> elements;
};

struct ASTMoveExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTFunctionCall final : public ASTExpression {
    ASTExprVariant function;
    std::span<ASTExprVariant> arguments;
};

struct ASTTemplateInstantiation final : public ASTExpression {
    ASTExprVariant template_expr;
    std::span<ASTExprVariant> arguments;
};

struct ASTTernaryOp final : public ASTExpression {
    ASTExprVariant condition;
    ASTExprVariant true_expr;
    ASTExprVariant false_expr;
};

struct ASTAs final : public ASTExpression {
    ASTExprVariant expr;
    ASTExprVariant target_type;
    bool is_dynamic;
};

struct ASTLambda final : public ASTExpression {
    std::span<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    std::variant<ASTNodeVariant, ASTExprVariant> body;
    mutable bool visited = false;
};

struct ASTPrimitiveType final : public ASTExplicitTypeExpr {
    const Type* type;
};

struct ASTStringViewType final : public ASTExplicitTypeExpr {};

struct ASTFunctionType final : public ASTExplicitTypeExpr {
    std::span<ASTExprVariant> parameter_types;
    ASTExprVariant return_type;
};

struct ASTFieldDeclaration final : public ASTNode {
    strview identifier;
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
//     strview template_identifier;
//     std::span<ASTExprVariant> arguments;
// };

// struct ASTTemplateMemberAccessInstantiation final : public ASTExpression {
//     ASTExprVariant target;
//     strview member;
//     std::span<ASTExprVariant> arguments;
// };

struct ASTExpressionStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTDeclaration final : public ASTNode {
    strview identifier;
    ASTExprVariant declared_type;
    ASTExprVariant expr;
    bool is_mutable;
    bool is_constant;
    bool declared_static;
};

struct ASTTypeAlias final : public ASTNode {
    strview identifier;
    ASTExprVariant type;
};

struct ASTIfStatement final : public ASTNode {
    ASTExprVariant condition;
    const ASTLocalBlock* if_block;
    ASTNodeVariant else_block;
};

struct ASTSwitchCase final : public ASTNode {
    ASTExprVariant value;
    ASTNodeVariant body;
};

struct ASTSwitchStatement final : public ASTNode {
    ASTExprVariant condition;
    std::span<ASTSwitchCase> cases;
};

struct ASTForStatement final : public ASTNode {
    std::variant<const ASTDeclaration*, ASTExprVariant> initializer;
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
    strview identifier;
    ASTExprVariant type;
    ASTExprVariant default_value;
    bool is_mutable;
    bool is_variadic;
};

struct ASTFunctionDefinition final : public ASTNode {
    strview identifier;
    std::span<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    std::span<ASTNodeVariant> body;
    bool declared_const;
    bool declared_static;
};

struct ASTCtorDtorDefinition final : public ASTNode {
    std::span<ASTFunctionParameter> parameters;
    std::span<ASTNodeVariant> body;
    bool is_constructor;
    bool declared_const;
};

struct ASTOperatorDefinition final : public ASTNode {
    OperatorCode opcode;
    ASTFunctionParameter left;
    ASTFunctionParameter* right;
    ASTExprVariant return_type;
    std::span<ASTNodeVariant> body;
    bool declared_const;
};

struct ASTClassDefinition final : public ASTNode {
    strview identifier;
    ASTExprVariant extends;
    std::span<ASTExprVariant> implements;
    std::span<ASTNodeVariant> fields;
    std::span<ASTNodeVariant> scope_items;
};

struct ASTEnumDefinition final : public ASTNode {
    strview identifier;
    std::span<strview> enumerators;
};

struct ASTNamespaceDefinition final : public ASTNode {
    strview identifier;
    std::span<ASTNodeVariant> items;
};

struct ASTTemplateParameter final : public ASTNode {
    bool is_nttp;  // true if non-type template parameter, false if type template parameter
    bool is_variadic;
    strview identifier;
    ASTExprVariant constraint;
    ASTExprVariant default_value;
};

struct ASTTemplateDefinition final : public ASTNode {
    strview identifier;
    std::span<ASTTemplateParameter> parameters;
    ASTNodeVariant target_node;
};

struct ASTTemplateSpecialization final : public ASTNode {
    strview identifier;
    std::span<ASTTemplateParameter> parameters;
    std::span<ASTExprVariant> patterns;
    ASTNodeVariant target_node;
};

struct ASTThrowStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTStaticAssertStatement final : public ASTNode {
    ASTExprVariant condition;
    ASTExprVariant message;
};

struct ASTImportStatement final : public ASTNode {
    strview path;
    strview alias;
    const ASTRoot* module_root;
};

struct ASTCppBlock final : public ASTNode {
    strview code;
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
    case OperatorCode::PostIncrement:
    case OperatorCode::Decrement:
    case OperatorCode::PostDecrement:
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
    case OperatorCode::LeftShift:
    case OperatorCode::RightShift:
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

constexpr auto GetOperatorString(OperatorCode opcode) -> strview {
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
    case OperatorCode::PostIncrement:
        return "++";
    case OperatorCode::Decrement:
    case OperatorCode::PostDecrement:
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
    case OperatorCode::Call:
        return "()";
    case OperatorCode::Index:
        return "[]";
    case OperatorCode::Deref:
        return "*";
    case OperatorCode::Pointer:
        return "->";
    default:
        UNREACHABLE();
    }
}
