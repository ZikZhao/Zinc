#pragma once
#include "ast.hpp"

class ASTVisitor {
public:
    virtual ~ASTVisitor() noexcept = default;

    // Base nodes
    virtual void visit(const ASTLocalBlock* node) {}
    virtual void visit(const ASTRoot* node) {}

    // Expressions
    virtual void visit(const ASTHiddenTypeExpr* node) {}
    virtual void visit(const ASTParenExpr* node) {}
    virtual void visit(const ASTConstant* node) {}
    virtual void visit(const ASTSelfExpr* node) {}
    virtual void visit(const ASTIdentifier* node) {}
    virtual void visit(const ASTMemberAccess* node) {}
    virtual void visit(const ASTFieldInitialization* node) {}
    virtual void visit(const ASTStructInitialization* node) {}
    virtual void visit(const ASTFunctionCall* node) {}
    virtual void visit(const ASTTemplateInstantiation* node) {}
    virtual void visit(const ASTTemplateMemberAccessInstantiation* node) {}

    // Unary operators
    virtual void visit(const ASTNegateOp* node) {}
    virtual void visit(const ASTIncrementOp* node) {}
    virtual void visit(const ASTDecrementOp* node) {}
    virtual void visit(const ASTLogicalNotOp* node) {}
    virtual void visit(const ASTBitwiseNotOp* node) {}

    // Binary operators - Arithmetic
    virtual void visit(const ASTAddOp* node) {}
    virtual void visit(const ASTSubtractOp* node) {}
    virtual void visit(const ASTMultiplyOp* node) {}
    virtual void visit(const ASTDivideOp* node) {}
    virtual void visit(const ASTRemainderOp* node) {}

    // Binary operators - Comparison
    virtual void visit(const ASTEqualOp* node) {}
    virtual void visit(const ASTNotEqualOp* node) {}
    virtual void visit(const ASTLessThanOp* node) {}
    virtual void visit(const ASTLessEqualOp* node) {}
    virtual void visit(const ASTGreaterThanOp* node) {}
    virtual void visit(const ASTGreaterEqualOp* node) {}

    // Binary operators - Logical
    virtual void visit(const ASTLogicalAndOp* node) {}
    virtual void visit(const ASTLogicalOrOp* node) {}

    // Binary operators - Bitwise
    virtual void visit(const ASTBitwiseAndOp* node) {}
    virtual void visit(const ASTBitwiseOrOp* node) {}
    virtual void visit(const ASTBitwiseXorOp* node) {}
    virtual void visit(const ASTLeftShiftOp* node) {}
    virtual void visit(const ASTRightShiftOp* node) {}

    // Binary operators - Assignment
    virtual void visit(const ASTAssignOp* node) {}
    virtual void visit(const ASTAddAssignOp* node) {}
    virtual void visit(const ASTSubtractAssignOp* node) {}
    virtual void visit(const ASTMultiplyAssignOp* node) {}
    virtual void visit(const ASTDivideAssignOp* node) {}
    virtual void visit(const ASTRemainderAssignOp* node) {}
    virtual void visit(const ASTLogicalAndAssignOp* node) {}
    virtual void visit(const ASTLogicalOrAssignOp* node) {}
    virtual void visit(const ASTBitwiseAndAssignOp* node) {}
    virtual void visit(const ASTBitwiseOrAssignOp* node) {}
    virtual void visit(const ASTBitwiseXorAssignOp* node) {}
    virtual void visit(const ASTLeftShiftAssignOp* node) {}
    virtual void visit(const ASTRightShiftAssignOp* node) {}

    // Type expressions
    virtual void visit(const ASTPrimitiveType* node) {}
    virtual void visit(const ASTFunctionType* node) {}
    virtual void visit(const ASTStructType* node) {}
    virtual void visit(const ASTMutableTypeExpr* node) {}
    virtual void visit(const ASTReferenceTypeExpr* node) {}
    virtual void visit(const ASTPointerTypeExpr* node) {}

    // Declarations
    virtual void visit(const ASTFieldDeclaration* node) {}
    virtual void visit(const ASTDeclaration* node) {}
    virtual void visit(const ASTTypeAlias* node) {}

    // Statements
    virtual void visit(const ASTExpressionStatement* node) {}
    virtual void visit(const ASTIfStatement* node) {}
    virtual void visit(const ASTForStatement* node) {}
    virtual void visit(const ASTContinueStatement* node) {}
    virtual void visit(const ASTBreakStatement* node) {}
    virtual void visit(const ASTReturnStatement* node) {}

    // Functions and classes
    virtual void visit(const ASTFunctionParameter* node) {}
    virtual void visit(const ASTFunctionDefinition* node) {}
    virtual void visit(const ASTConstructorDestructorDefinition* node) {}
    virtual void visit(const ASTClassSignature* node) {}
    virtual void visit(const ASTClassDefinition* node) {}
    virtual void visit(const ASTNamespaceDefinition* node) {}

    // Templates
    virtual void visit(const ASTTemplateParameter* node) {}
    virtual void visit(const ASTTemplateDefinition* node) {}
    virtual void visit(const ASTTemplateSpecialization* node) {}
};

inline void ASTLocalBlock::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTRoot::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTHiddenTypeExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTParenExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTConstant::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTSelfExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTIdentifier::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTMemberAccess::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTFieldInitialization::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTStructInitialization::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTFunctionCall::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTTemplateInstantiation::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTTemplateMemberAccessInstantiation::do_accept(ASTVisitor& visitor) const {
    visitor.visit(this);
}

// Unary operators
template <>
inline void ASTUnaryOp<OperatorFunctors::Negate>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTNegateOp*>(this));
}
template <>
inline void ASTUnaryOp<OperatorFunctors::Increment>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTIncrementOp*>(this));
}
template <>
inline void ASTUnaryOp<OperatorFunctors::Decrement>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTDecrementOp*>(this));
}
template <>
inline void ASTUnaryOp<OperatorFunctors::LogicalNot>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLogicalNotOp*>(this));
}
template <>
inline void ASTUnaryOp<OperatorFunctors::BitwiseNot>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseNotOp*>(this));
}

// Binary operators - Arithmetic
template <>
inline void ASTBinaryOp<OperatorFunctors::Add>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTAddOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::Subtract>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTSubtractOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::Multiply>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTMultiplyOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::Divide>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTDivideOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::Remainder>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTRemainderOp*>(this));
}

// Binary operators - Comparison
template <>
inline void ASTBinaryOp<OperatorFunctors::Equal>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTEqualOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::NotEqual>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTNotEqualOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LessThan>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLessThanOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LessEqual>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLessEqualOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::GreaterThan>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTGreaterThanOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::GreaterEqual>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTGreaterEqualOp*>(this));
}

// Binary operators - Logical
template <>
inline void ASTBinaryOp<OperatorFunctors::LogicalAnd>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLogicalAndOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LogicalOr>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLogicalOrOp*>(this));
}

// Binary operators - Bitwise
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseAnd>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseAndOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseOr>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseOrOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseXor>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseXorOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LeftShift>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLeftShiftOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::RightShift>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTRightShiftOp*>(this));
}

// Binary operators - Assignment
template <>
inline void ASTBinaryOp<OperatorFunctors::Assign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::AddAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTAddAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::SubtractAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTSubtractAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::MultiplyAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTMultiplyAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::DivideAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTDivideAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::RemainderAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTRemainderAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LogicalAndAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLogicalAndAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LogicalOrAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLogicalOrAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseAndAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseOrAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTBitwiseXorAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::LeftShiftAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTLeftShiftAssignOp*>(this));
}
template <>
inline void ASTBinaryOp<OperatorFunctors::RightShiftAssign>::do_accept(ASTVisitor& visitor) const {
    visitor.visit(static_cast<const ASTRightShiftAssignOp*>(this));
}

// Type expressions
inline void ASTPrimitiveType::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTFunctionType::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTStructType::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTMutableTypeExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTReferenceTypeExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTPointerTypeExpr::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }

// Declarations
inline void ASTFieldDeclaration::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTDeclaration::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTTypeAlias::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }

// Statements
inline void ASTExpressionStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTIfStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTForStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTContinueStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTBreakStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTReturnStatement::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }

// Functions and classes
inline void ASTFunctionParameter::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTFunctionDefinition::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTConstructorDestructorDefinition::do_accept(ASTVisitor& visitor) const {
    visitor.visit(this);
}
inline void ASTClassSignature::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTClassDefinition::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTNamespaceDefinition::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }

// Templates
inline void ASTTemplateParameter::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTTemplateDefinition::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
inline void ASTTemplateSpecialization::do_accept(ASTVisitor& visitor) const { visitor.visit(this); }
