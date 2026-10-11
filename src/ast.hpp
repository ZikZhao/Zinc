#pragma once
#include "pch.hpp"

#include "object.hpp"

enum class AccessSpecifier : std::uint8_t { Public, Protected, Private };

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
struct ASTTypeExpr;
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
struct ASTDynamicType;

struct ASTExpressionStatement;
struct ASTDeclaration;
struct ASTFieldDeclaration;
struct ASTTypeAlias;
struct ASTIfStatement;
struct ASTSwitchCase;
struct ASTSwitchStatement;
struct ASTMatchCase;
struct ASTMatchStatement;
struct MatchCaseVarType;
struct ASTForStatement;
struct ASTContinueStatement;
struct ASTBreakStatement;
struct ASTReturnStatement;
struct ASTFunctionParameter;
struct ASTFunctionDefinition;
struct ASTCtorDtorDefinition;
struct ASTOperatorDefinition;
struct ASTInterfaceDefinition;
struct ASTClassDefinition;
struct ASTNamespaceDefinition;
struct ASTTemplateParameter;
struct ASTTemplateDefinition;
struct ASTTemplateSpecialization;
struct ASTThrowStatement;
struct ASTAwaitStatement;
struct ASTYieldStatement;
struct ASTImportStatement;
struct ASTCppBlock;

// All AST node types (base classes and final classes)
using ASTNodeVariant = std::variant<
    std::monostate,
    // Root and blocks
    std::unique_ptr<ASTRoot>,
    std::unique_ptr<ASTLocalBlock>,
    // Statements
    std::unique_ptr<ASTExpressionStatement>,
    std::unique_ptr<ASTDeclaration>,
    std::unique_ptr<ASTTypeAlias>,
    std::unique_ptr<ASTIfStatement>,
    std::unique_ptr<ASTSwitchStatement>,
    std::unique_ptr<ASTMatchStatement>,
    std::unique_ptr<ASTForStatement>,
    std::unique_ptr<ASTContinueStatement>,
    std::unique_ptr<ASTBreakStatement>,
    std::unique_ptr<ASTReturnStatement>,
    // Functions and classes
    std::unique_ptr<ASTFunctionDefinition>,
    std::unique_ptr<ASTCtorDtorDefinition>,
    std::unique_ptr<ASTOperatorDefinition>,
    std::unique_ptr<ASTClassDefinition>,
    std::unique_ptr<ASTInterfaceDefinition>,
    std::unique_ptr<ASTNamespaceDefinition>,
    // Templates
    std::unique_ptr<ASTTemplateDefinition>,
    std::unique_ptr<ASTTemplateSpecialization>,
    // Async
    std::unique_ptr<ASTAwaitStatement>,
    std::unique_ptr<ASTYieldStatement>,
    // Error handling
    std::unique_ptr<ASTThrowStatement>,
    // Import
    std::unique_ptr<ASTImportStatement>,
    // C++ block
    std::unique_ptr<ASTCppBlock>>;

// All expression node types (all ASTExpression derivatives)
using ASTExprVariant = std::variant<
    std::monostate,
    // Basic expressions
    std::unique_ptr<ASTParenExpr>,
    std::unique_ptr<ASTConstant>,
    std::unique_ptr<ASTStringConstant>,
    std::unique_ptr<ASTSelfExpr>,
    std::unique_ptr<ASTIdentifier>,
    std::unique_ptr<ASTMemberAccess>,
    std::unique_ptr<ASTPointerAccess>,
    std::unique_ptr<ASTIndexAccess>,
    std::unique_ptr<ASTAddressOfExpr>,
    std::unique_ptr<ASTDereference>,
    std::unique_ptr<ASTUnaryOp>,
    std::unique_ptr<ASTBinaryOp>,
    // Complex expressions
    std::unique_ptr<ASTStructInitialization>,
    std::unique_ptr<ASTArrayInitialization>,
    std::unique_ptr<ASTMoveExpr>,
    std::unique_ptr<ASTFunctionCall>,
    std::unique_ptr<ASTTemplateInstantiation>,
    std::unique_ptr<ASTTernaryOp>,
    std::unique_ptr<ASTAs>,
    std::unique_ptr<ASTLambda>,
    // Type expressions
    std::unique_ptr<ASTPrimitiveType>,
    std::unique_ptr<ASTStringViewType>,
    std::unique_ptr<ASTArrayType>,
    std::unique_ptr<ASTFunctionType>,
    std::unique_ptr<ASTStructType>,
    std::unique_ptr<ASTMutableType>,
    std::unique_ptr<ASTReferenceType>,
    std::unique_ptr<ASTPointerType>,
    std::unique_ptr<ASTDynamicType>,
    // Match case variable type
    std::unique_ptr<MatchCaseVarType>>;

template <typename T>
concept ASTClass = std::derived_from<T, ASTNode>;

struct ASTNode {
    Location location;
    ASTNode(const Location& loc) noexcept : location(loc) {}
};

struct ASTLocalBlock final : public ASTNode {
    Array<ASTNodeVariant> statements;
};

struct ASTRoot final : public ASTNode {
    Array<ASTNodeVariant> statements;
    Array<ASTNodeVariant> cpp_blocks;
    mutable Scope* scope = nullptr;
    mutable bool type_checked = false;
};

struct ASTExpression : public ASTNode {};

struct ASTTypeExpr : public ASTExpression {};

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
    std::string value;
};

struct ASTParenExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTAddressOfExpr final : public ASTExpression {
    ASTExprVariant operand;
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
    Array<ASTFieldInitialization> field_inits;
};

struct ASTArrayInitialization final : public ASTExpression {
    Array<ASTExprVariant> elements;
};

struct ASTMoveExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTFunctionCall final : public ASTExpression {
    ASTExprVariant function;
    Array<ASTExprVariant> arguments;
};

struct ASTTemplateInstantiation final : public ASTExpression {
    ASTExprVariant template_expr;
    Array<ASTExprVariant> arguments;
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
    Array<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    std::variant<ASTNodeVariant, ASTExprVariant> body;
    mutable bool visited = false;
};

struct ASTPrimitiveType final : public ASTTypeExpr {
    const Type* type;
};

struct ASTStringViewType final : public ASTTypeExpr {};

struct ASTFunctionType final : public ASTTypeExpr {
    Array<ASTExprVariant> parameter_types;
    ASTExprVariant return_type;
};

struct ASTFieldDeclaration final : public ASTNode {
    strview identifier;
    ASTExprVariant type;
};

struct ASTStructType final : public ASTTypeExpr {
    Array<ASTFieldDeclaration> fields;
};

struct ASTArrayType final : public ASTTypeExpr {
    ASTExprVariant element_type;
    ASTExprVariant length;
};

struct ASTMutableType final : public ASTTypeExpr {
    ASTExprVariant inner;
};

struct ASTReferenceType final : public ASTTypeExpr {
    ASTExprVariant inner;
    bool is_mutable;
    bool is_moved;
};

struct ASTPointerType final : public ASTTypeExpr {
    ASTExprVariant inner;
    bool is_mutable;
};

struct ASTDynamicType final : public ASTTypeExpr {
    ASTExprVariant inner;
    bool is_mutable;
};

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
    Array<ASTSwitchCase> cases;
};

struct ASTMatchCase final : public ASTNode {
    strview identifier;
    ASTExprVariant type;
    Array<ASTNodeVariant> body;
};

struct ASTMatchStatement final : public ASTNode {
    ASTExprVariant value;
    Array<ASTMatchCase> cases;
};

struct MatchCaseVarType final : public ASTTypeExpr {
    const Type* type;
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
    Array<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    Array<ASTNodeVariant> body;
    bool declared_static;
    bool declared_async;
};

struct ASTCtorDtorDefinition final : public ASTNode {
    Array<ASTFunctionParameter> parameters;
    Array<ASTNodeVariant> body;
    bool is_constructor;
};

struct ASTOperatorDefinition final : public ASTNode {
    OperatorCode opcode;
    Array<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    Array<ASTNodeVariant> body;
};

struct ASTInterfaceDefinition final : public ASTNode {
    strview identifier;
    Array<ASTExprVariant> extends;
    Array<ASTNodeVariant> scope_items;
};

struct ASTClassDefinition final : public ASTNode {
    strview identifier;
    Array<ASTExprVariant> implements;
    Array<ASTDeclaration> fields;  // only instance fields, not static fields
    Array<ASTNodeVariant> scope_items;
    // Array<std::pair<std::size_t, AccessSpecifier>> access_specifiers;
};

struct ASTNamespaceDefinition final : public ASTNode {
    strview identifier;
    Array<ASTNodeVariant> items;
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
    Array<ASTTemplateParameter> parameters;
    ASTNodeVariant target_node;
};

struct ASTTemplateSpecialization final : public ASTNode {
    strview identifier;
    Array<ASTTemplateParameter> parameters;
    Array<ASTExprVariant> patterns;
    ASTNodeVariant target_node;
};

struct ASTThrowStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTAwaitStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTYieldStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTImportStatement final : public ASTNode {
    strview path;
    strview alias;
    const ASTRoot* module_root;
};

struct ASTCppBlock final : public ASTNode {
    strview code;
};

struct ASTNodePtrGetter {
    auto operator()(ASTNodeVariant variant) -> const ASTNode* { return std::visit(*this, variant); }
    auto operator()(ASTExprVariant variant) -> const ASTNode* { return std::visit(*this, variant); }
    template <typename T>
    auto operator()(std::unique_ptr<T>& node) -> const ASTNode* {
        return node.get();
    }
    auto operator()(std::monostate) -> const ASTNode* { UNREACHABLE(); }
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
    Special
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
    case OperatorCode::Call:
    case OperatorCode::Index:
    case OperatorCode::Deref:
    case OperatorCode::Pointer:
        return OperatorGroup::Special;
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
