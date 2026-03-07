#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"
#include "visitor.hpp"

class TypeCheckVisitor : public ASTVisitor {
private:
    TypeChecker& checker_;

public:
    TypeCheckVisitor(TypeChecker& checker) noexcept : checker_(checker) {}

    void operator()(const ASTNode* node) {
        const ASTNode* prev_node = std::exchange(checker_.current_node_, node);
        node->accept(*this);
        checker_.current_node_ = prev_node;
    }

    // Root and blocks
    void visit(const ASTRoot* node) override {
        for (const auto* stmt : node->statements_) {
            stmt->accept(*this);
        }
    }

    void visit(const ASTLocalBlock* node) override {
        TypeChecker::Guard guard(checker_, node);
        for (const auto* stmt : node->statements_) {
            stmt->accept(*this);
        }
    }

#define EXPR_VISIT(name)                                                 \
    void visit(const AST##name* node) override {                         \
        std::ignore = node->eval_term(checker_, nullptr, false).subject; \
    }

    // Expressions - all expressions evaluate their term
    EXPR_VISIT(Constant)
    EXPR_VISIT(SelfExpr)
    EXPR_VISIT(Identifier)
    EXPR_VISIT(ParenExpr)
    EXPR_VISIT(MemberAccess)
    EXPR_VISIT(StructInitialization)
    EXPR_VISIT(FunctionCall)
    EXPR_VISIT(TemplateInstantiation)
    EXPR_VISIT(TemplateMemberAccessInstantiation)

    // Unary operators
    EXPR_VISIT(NegateOp)
    EXPR_VISIT(IncrementOp)
    EXPR_VISIT(DecrementOp)
    EXPR_VISIT(LogicalNotOp)
    EXPR_VISIT(BitwiseNotOp)

    // Binary operators - Arithmetic
    EXPR_VISIT(AddOp)
    EXPR_VISIT(SubtractOp)
    EXPR_VISIT(MultiplyOp)
    EXPR_VISIT(DivideOp)
    EXPR_VISIT(RemainderOp)

    // Binary operators - Comparison
    EXPR_VISIT(EqualOp)
    EXPR_VISIT(NotEqualOp)
    EXPR_VISIT(LessThanOp)
    EXPR_VISIT(LessEqualOp)
    EXPR_VISIT(GreaterThanOp)
    EXPR_VISIT(GreaterEqualOp)

    // Binary operators - Logical
    EXPR_VISIT(LogicalAndOp)
    EXPR_VISIT(LogicalOrOp)

    // Binary operators - Bitwise
    EXPR_VISIT(BitwiseAndOp)
    EXPR_VISIT(BitwiseOrOp)
    EXPR_VISIT(BitwiseXorOp)
    EXPR_VISIT(LeftShiftOp)
    EXPR_VISIT(RightShiftOp)

    // Binary operators - Assignment
    EXPR_VISIT(AssignOp)
    EXPR_VISIT(AddAssignOp)
    EXPR_VISIT(SubtractAssignOp)
    EXPR_VISIT(MultiplyAssignOp)
    EXPR_VISIT(DivideAssignOp)
    EXPR_VISIT(RemainderAssignOp)
    EXPR_VISIT(LogicalAndAssignOp)
    EXPR_VISIT(LogicalOrAssignOp)
    EXPR_VISIT(BitwiseAndAssignOp)
    EXPR_VISIT(BitwiseOrAssignOp)
    EXPR_VISIT(BitwiseXorAssignOp)
    EXPR_VISIT(LeftShiftAssignOp)
    EXPR_VISIT(RightShiftAssignOp)

#undef EXPR_VISIT

    // Statements
    void visit(const ASTExpressionStatement* node) override { node->expr_->accept(*this); }

    void visit(const ASTDeclaration* node) override { checker_.lookup(node->identifier_); }

    void visit(const ASTTypeAlias* node) override { checker_.lookup_type(node->identifier_); }

    void visit(const ASTIfStatement* node) override {
        TypeChecker::Guard guard(checker_, node);
        std::ignore = node->condition_->eval_term(checker_, &BooleanType::instance, false).subject;
        node->if_block_->accept(*this);
        if (node->else_block_) {
            node->else_block_->accept(*this);
        }
    }

    void visit(const ASTForStatement* node) override {
        TypeChecker::Guard guard(checker_, node);
        if (node->initializer_) {
            node->initializer_->accept(*this);
        }
        if (node->condition_) {
            std::ignore =
                node->condition_->eval_term(checker_, &BooleanType::instance, false).subject;
        }
        if (node->increment_) {
            std::ignore = node->increment_->eval_term(checker_, nullptr, false).subject;
        }
        for (const auto* stmt : node->body_) {
            stmt->accept(*this);
        }
    }

    void visit(const ASTReturnStatement* node) override {
        if (node->expr_) {
            std::ignore = node->expr_->eval_term(checker_, nullptr, false).subject;
        }
    }

    // Functions and classes
    void visit(const ASTFunctionDefinition* node) override {
        TypeChecker::Guard guard(checker_, node);
        for (auto& stmt : node->body_) {
            stmt->accept(*this);
        }
    }

    void visit(const ASTConstructorDestructorDefinition* node) override {
        TypeChecker::Guard guard(checker_, node);
        for (auto& stmt : node->body_) {
            stmt->accept(*this);
        }
    }

    void visit(const ASTClassDefinition* node) override {
        checker_.lookup_type(node->identifier_);  // trigger self type injection
        TypeChecker::Guard guard(checker_, node);
        for (auto& field : node->fields_) {
            field->accept(*this);
        }
        for (auto& ctor : node->constructors_) {
            ctor->accept(*this);
        }
        if (node->destructor_) {
            node->destructor_->accept(*this);
        }
        for (auto& func : node->functions_) {
            func->accept(*this);
        }
    }

    void visit(const ASTNamespaceDefinition* node) override {
        TypeChecker::Guard guard(checker_, node);
        for (const auto& item : node->items_) {
            item->accept(*this);
        }
    }
};
