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
struct ASTParenExpr;
struct ASTConstant;
struct ASTSelfExpr;
struct ASTIdentifier;
template <OperatorCode op>
struct ASTUnaryOp;
template <OperatorCode op>
struct ASTBinaryOp;

using ASTAddOp = ASTBinaryOp<OperatorCode::Add>;
using ASTSubtractOp = ASTBinaryOp<OperatorCode::Subtract>;
using ASTNegateOp = ASTUnaryOp<OperatorCode::Negate>;
using ASTMultiplyOp = ASTBinaryOp<OperatorCode::Multiply>;
using ASTDivideOp = ASTBinaryOp<OperatorCode::Divide>;
using ASTRemainderOp = ASTBinaryOp<OperatorCode::Remainder>;
using ASTIncrementOp = ASTUnaryOp<OperatorCode::Increment>;
using ASTDecrementOp = ASTUnaryOp<OperatorCode::Decrement>;

using ASTEqualOp = ASTBinaryOp<OperatorCode::Equal>;
using ASTNotEqualOp = ASTBinaryOp<OperatorCode::NotEqual>;
using ASTLessThanOp = ASTBinaryOp<OperatorCode::LessThan>;
using ASTLessEqualOp = ASTBinaryOp<OperatorCode::LessEqual>;
using ASTGreaterThanOp = ASTBinaryOp<OperatorCode::GreaterThan>;
using ASTGreaterEqualOp = ASTBinaryOp<OperatorCode::GreaterEqual>;

using ASTLogicalAndOp = ASTBinaryOp<OperatorCode::LogicalAnd>;
using ASTLogicalOrOp = ASTBinaryOp<OperatorCode::LogicalOr>;
using ASTLogicalNotOp = ASTUnaryOp<OperatorCode::LogicalNot>;

using ASTBitwiseAndOp = ASTBinaryOp<OperatorCode::BitwiseAnd>;
using ASTBitwiseOrOp = ASTBinaryOp<OperatorCode::BitwiseOr>;
using ASTBitwiseXorOp = ASTBinaryOp<OperatorCode::BitwiseXor>;
using ASTBitwiseNotOp = ASTUnaryOp<OperatorCode::BitwiseNot>;
using ASTLeftShiftOp = ASTBinaryOp<OperatorCode::LeftShift>;
using ASTRightShiftOp = ASTBinaryOp<OperatorCode::RightShift>;

using ASTAssignOp = ASTBinaryOp<OperatorCode::Assign>;
using ASTAddAssignOp = ASTBinaryOp<OperatorCode::AddAssign>;
using ASTSubtractAssignOp = ASTBinaryOp<OperatorCode::SubtractAssign>;
using ASTMultiplyAssignOp = ASTBinaryOp<OperatorCode::MultiplyAssign>;
using ASTDivideAssignOp = ASTBinaryOp<OperatorCode::DivideAssign>;
using ASTRemainderAssignOp = ASTBinaryOp<OperatorCode::RemainderAssign>;
using ASTLogicalAndAssignOp = ASTBinaryOp<OperatorCode::LogicalAndAssign>;
using ASTLogicalOrAssignOp = ASTBinaryOp<OperatorCode::LogicalOrAssign>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorCode::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp = ASTBinaryOp<OperatorCode::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorCode::BitwiseXorAssign>;
using ASTLeftShiftAssignOp = ASTBinaryOp<OperatorCode::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorCode::RightShiftAssign>;

struct ASTMemberAccess;
struct ASTFieldInitialization;
struct ASTStructInitialization;
struct ASTFunctionCall;
struct ASTPrimitiveType;
struct ASTFunctionType;
struct ASTStructType;
struct ASTMutableType;
struct ASTReferenceType;
struct ASTPointerType;
struct ASTTemplateInstantiation;
struct ASTTemplateMemberAccessInstantiation;
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
struct ASTClassDefinition;
struct ASTNamespaceDefinition;
struct ASTTemplateParameter;
struct ASTTemplateDefinition;
struct ASTTemplateSpecialization;

// All AST node types (base classes and final classes)
using ASTNodeVariant = std::variant<
    std::monostate,
    // Base classes
    ASTNode*,
    ASTExpression*,
    ASTExplicitTypeExpr*,
    // Root and blocks
    ASTRoot*,
    ASTLocalBlock*,
    // Expressions
    ASTParenExpr*,
    ASTConstant*,
    ASTSelfExpr*,
    ASTIdentifier*,
    // Unary operators
    ASTNegateOp*,
    ASTIncrementOp*,
    ASTDecrementOp*,
    ASTLogicalNotOp*,
    ASTBitwiseNotOp*,
    // Binary operators - arithmetic
    ASTAddOp*,
    ASTSubtractOp*,
    ASTMultiplyOp*,
    ASTDivideOp*,
    ASTRemainderOp*,
    // Binary operators - comparison
    ASTEqualOp*,
    ASTNotEqualOp*,
    ASTLessThanOp*,
    ASTLessEqualOp*,
    ASTGreaterThanOp*,
    ASTGreaterEqualOp*,
    // Binary operators - logical
    ASTLogicalAndOp*,
    ASTLogicalOrOp*,
    // Binary operators - bitwise
    ASTBitwiseAndOp*,
    ASTBitwiseOrOp*,
    ASTBitwiseXorOp*,
    ASTLeftShiftOp*,
    ASTRightShiftOp*,
    // Binary operators - assignment
    ASTAssignOp*,
    ASTAddAssignOp*,
    ASTSubtractAssignOp*,
    ASTMultiplyAssignOp*,
    ASTDivideAssignOp*,
    ASTRemainderAssignOp*,
    ASTLogicalAndAssignOp*,
    ASTLogicalOrAssignOp*,
    ASTBitwiseAndAssignOp*,
    ASTBitwiseOrAssignOp*,
    ASTBitwiseXorAssignOp*,
    ASTLeftShiftAssignOp*,
    ASTRightShiftAssignOp*,
    // Member access and calls
    ASTMemberAccess*,
    ASTStructInitialization*,
    ASTFunctionCall*,
    // Type expressions
    ASTPrimitiveType*,
    ASTFunctionType*,
    ASTFieldDeclaration*,
    ASTStructType*,
    ASTMutableType*,
    ASTReferenceType*,
    ASTPointerType*,
    ASTTemplateInstantiation*,
    ASTTemplateMemberAccessInstantiation*,
    // Statements
    ASTExpressionStatement*,
    ASTDeclaration*,
    ASTTypeAlias*,
    ASTIfStatement*,
    ASTForStatement*,
    ASTContinueStatement*,
    ASTBreakStatement*,
    ASTReturnStatement*,
    // Functions and classes
    ASTFunctionDefinition*,
    ASTConstructorDestructorDefinition*,
    ASTClassDefinition*,
    ASTNamespaceDefinition*,
    // Templates
    ASTTemplateDefinition*,
    ASTTemplateSpecialization*>;

// All expression node types (all ASTExpression derivatives)
using ASTExprVariant = std::variant<
    std::monostate,
    // Base classes
    ASTExpression*,
    ASTExplicitTypeExpr*,
    // Basic expressions
    ASTParenExpr*,
    ASTConstant*,
    ASTSelfExpr*,
    ASTIdentifier*,
    // Unary operators
    ASTNegateOp*,
    ASTIncrementOp*,
    ASTDecrementOp*,
    ASTLogicalNotOp*,
    ASTBitwiseNotOp*,
    // Binary operators - arithmetic
    ASTAddOp*,
    ASTSubtractOp*,
    ASTMultiplyOp*,
    ASTDivideOp*,
    ASTRemainderOp*,
    // Binary operators - comparison
    ASTEqualOp*,
    ASTNotEqualOp*,
    ASTLessThanOp*,
    ASTLessEqualOp*,
    ASTGreaterThanOp*,
    ASTGreaterEqualOp*,
    // Binary operators - logical
    ASTLogicalAndOp*,
    ASTLogicalOrOp*,
    // Binary operators - bitwise
    ASTBitwiseAndOp*,
    ASTBitwiseOrOp*,
    ASTBitwiseXorOp*,
    ASTLeftShiftOp*,
    ASTRightShiftOp*,
    // Binary operators - assignment
    ASTAssignOp*,
    ASTAddAssignOp*,
    ASTSubtractAssignOp*,
    ASTMultiplyAssignOp*,
    ASTDivideAssignOp*,
    ASTRemainderAssignOp*,
    ASTLogicalAndAssignOp*,
    ASTLogicalOrAssignOp*,
    ASTBitwiseAndAssignOp*,
    ASTBitwiseOrAssignOp*,
    ASTBitwiseXorAssignOp*,
    ASTLeftShiftAssignOp*,
    ASTRightShiftAssignOp*,
    // Member access and calls
    ASTMemberAccess*,
    ASTStructInitialization*,
    ASTFunctionCall*,
    // Type expressions
    ASTPrimitiveType*,
    ASTFunctionType*,
    ASTStructType*,
    ASTMutableType*,
    ASTReferenceType*,
    ASTPointerType*,
    ASTTemplateInstantiation*,
    ASTTemplateMemberAccessInstantiation*>;

constexpr auto nonnull(auto&& variant) -> bool {
    return !std::holds_alternative<std::monostate>(std::forward<decltype(variant)>(variant));
}

template <typename T>
concept ASTUnaryOpClass = requires(T node) {
    { T::opcode } -> std::convertible_to<OperatorCode>;
    { node.expr } -> std::convertible_to<ASTExprVariant>;
};

template <typename T>
concept ASTBinaryOpClass = requires(T node) {
    { T::opcode } -> std::convertible_to<OperatorCode>;
    { node.left } -> std::convertible_to<ASTExprVariant>;
    { node.right } -> std::convertible_to<ASTExprVariant>;
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

struct ASTParenExpr final : public ASTExpression {
    ASTExprVariant inner;
};

struct ASTConstant final : public ASTExpression {
    const Value* value;
};

struct ASTSelfExpr final : public ASTExpression {
    bool is_type;
};

struct ASTIdentifier final : public ASTExpression {
    std::string_view str;
};

template <OperatorCode op>
struct ASTUnaryOp final : public ASTExpression {
    static constexpr OperatorCode opcode = op;
    ASTExprVariant expr;
};

template <OperatorCode op>
struct ASTBinaryOp final : public ASTExpression {
    static constexpr OperatorCode opcode = op;
    ASTExprVariant left;
    ASTExprVariant right;
};

struct ASTMemberAccess final : public ASTExpression {
    ASTExprVariant target;
    std::span<std::string_view> members;
};

struct ASTFieldInitialization final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant value;
};

struct ASTStructInitialization final : public ASTExpression {
    ASTExprVariant struct_type;
    std::span<ASTFieldInitialization> field_inits;
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

struct ASTTemplateInstantiation final : public ASTExpression {
    std::string_view template_identifier;
    std::span<ASTExprVariant> arguments;

    // public:
    //     ASTTemplateInstantiation(
    //         const Location& loc, std::string_view template_name, std::span<ASTExpression*>
    //         arguments
    //     ) noexcept
    //         : ASTExpression(loc), template_name_(template_name), arguments_(arguments) {}
    //     TermWithReceiver eval_term(
    //         TypeChecker& checker, const Type* expected, bool comptime
    //     ) const noexcept final {
    //         GlobalMemory::Vector<Term> args_terms =
    //             arguments_ | std::views::transform([&](ASTExpression* arg) {
    //                 return arg->eval_term(checker, nullptr, comptime).subject;
    //             }) |
    //             GlobalMemory::collect<GlobalMemory::Vector<Term>>();
    //         return {checker.lookup_term_instatiation(template_name_, args_terms), {}};
    //     }
    //     void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

    // protected:
    //     void do_accept(ASTVisitor& visitor) const final;

    // private:
    //     void do_eval_type(
    //         TypeChecker& checker, TypeResolution& out, bool require_complete
    //     ) const noexcept final {
    //         GlobalMemory::Vector<Term> args_terms =
    //             arguments_ | std::views::transform([&](ASTExpression* arg) {
    //                 return arg->eval_term(checker, nullptr, false).subject;
    //             }) |
    //             GlobalMemory::collect<GlobalMemory::Vector<Term>>();
    //         out = checker.lookup_type_instatiation(template_name_, args_terms);
    //     }
};

struct ASTTemplateMemberAccessInstantiation final : public ASTExpression {
    ASTExprVariant target;
    std::string_view member;
    std::span<ASTExprVariant> arguments;

    // public:
    //     ASTTemplateMemberAccessInstantiation(
    //         const Location& loc,
    //         ASTExpression* target,
    //         std::string_view member,
    //         std::span<ASTExpression*> arguments
    //     ) noexcept
    //         : ASTExpression(loc), target_(target), member_(member), arguments_(arguments) {}
    //     TermWithReceiver eval_term(
    //         TypeChecker& checker, const Type* expected, bool comptime
    //     ) const noexcept final {
    //         // TODO
    //         return {Term::unknown(), {}};
    //     }

    // protected:
    //     void do_accept(ASTVisitor& visitor) const final;
    //     void do_eval_type(
    //         TypeChecker& checker, TypeResolution& out, bool require_complete
    //     ) const noexcept final {
    //         UNREACHABLE();
    //     }
};

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
    ASTLocalBlock* if_block;
    ASTLocalBlock* else_block;
};

struct ASTForStatement final : public ASTNode {
    ASTDeclaration* initializer_decl;
    ASTExprVariant initializer_expr;
    ASTExprVariant condition;
    ASTExprVariant increment;
    ASTLocalBlock* body;
};

struct ASTContinueStatement final : public ASTNode {};

struct ASTBreakStatement final : public ASTNode {};

struct ASTReturnStatement final : public ASTNode {
    ASTExprVariant expr;
};

struct ASTFunctionParameter final : public ASTNode {
    std::string_view identifier;
    ASTExprVariant type;
};

struct ASTFunctionDefinition final : public ASTNode {
    std::string_view identifier;
    std::span<ASTFunctionParameter> parameters;
    ASTExprVariant return_type;
    std::span<ASTNodeVariant> body;
    bool is_const;
    bool is_static;
    bool is_decl_only;
    bool is_main = false;  // Updated by SymbolCollector
};

struct ASTConstructorDestructorDefinition final : public ASTNode {
    bool is_constructor;
    std::span<ASTFunctionParameter> parameters;
    std::span<ASTNodeVariant> body;
};

struct ASTClassDefinition final : public ASTNode {
    std::string_view identifier;
    std::string_view extends;
    std::span<std::string_view> implements;
    std::span<ASTConstructorDestructorDefinition*> constructors;
    ASTConstructorDestructorDefinition* destructor;
    std::span<ASTDeclaration*> fields;
    std::span<ASTTypeAlias*> aliases;
    std::span<ASTFunctionDefinition*> functions;
    std::span<ASTClassDefinition*> classes;
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

struct ASTTemplateSpecialization final : public ASTNode {};

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
