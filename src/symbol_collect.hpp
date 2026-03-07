#pragma once
#include "pch.hpp"

#include "ast.hpp"
#include "object.hpp"
#include "visitor.hpp"

class SymbolCollectVisitor : public ASTVisitor {
private:
    Scope& current_scope_;
    MemberAccessHandler& sema_;

public:
    SymbolCollectVisitor(Scope& scope, MemberAccessHandler& sema) noexcept
        : current_scope_(scope), sema_(sema) {}

    void operator()(const ASTNode* node) { node->accept(*this); }

    // Root and blocks
    void visit(const ASTRoot* node) override {
        for (auto& child : node->statements_) {
            child->accept(*this);
        }
    }

    void visit(const ASTLocalBlock* node) override {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollectVisitor local_visitor(local_scope, sema_);
        for (auto& stmt : node->statements_) {
            stmt->accept(local_visitor);
        }
    }

    // Declarations
    void visit(const ASTDeclaration* node) override {
        try {
            current_scope_.add_variable(node->identifier_, node);
        } catch (UnlocatedProblem& e) {
            e.report_at(node->location_);
        }
    }

    void visit(const ASTTypeAlias* node) override {
        current_scope_.add_alias(node->identifier_, node->type_);
    }

    // Statements
    void visit(const ASTIfStatement* node) override {
        Scope& condition_scope = Scope::make(current_scope_, node);
        SymbolCollectVisitor condition_visitor(condition_scope, sema_);
        node->condition_->accept(condition_visitor);
        node->if_block_->accept(condition_visitor);
        if (node->else_block_) {
            node->else_block_->accept(condition_visitor);
        }
    }

    void visit(const ASTForStatement* node) override {
        Scope& local_scope = Scope::make(current_scope_, node);
        SymbolCollectVisitor local_visitor(local_scope, sema_);
        if (node->initializer_) {
            node->initializer_->accept(local_visitor);
        }
        /// TODO: refactor to local scope
        for (auto& stmt : node->body_) {
            stmt->accept(local_visitor);
        }
    }

    // Functions
    void visit(const ASTFunctionDefinition* node) override {
        current_scope_.add_function(node->identifier_, node);
        if (current_scope_.self_type_ == nullptr ||
            (node->parameters_.size() ? node->parameters_[0]->identifier_ != "self" : true)) {
            const_cast<ASTFunctionDefinition*>(node)->is_static_ = true;
        }
        if (node->is_static_ && current_scope_.parent_ == nullptr && node->identifier_ == "main") {
            const_cast<ASTFunctionDefinition*>(node)->is_main_ = true;
        }
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters_) {
            current_scope_.add_variable(param->identifier_, param);
        }
        SymbolCollectVisitor local_visitor(local_scope, sema_);
        for (auto& stmt : node->body_) {
            stmt->accept(local_visitor);
        }
    }

    void visit(const ASTConstructorDestructorDefinition* node) override {
        Scope& local_scope = Scope::make(current_scope_, node);
        for (auto& param : node->parameters_) {
            current_scope_.add_variable(param->identifier_, param);
        }
        SymbolCollectVisitor local_visitor(local_scope, sema_);
        for (auto& stmt : node->body_) {
            stmt->accept(local_visitor);
        }
    }

    // Classes
    void visit(const ASTClassDefinition* node) override {
        auto signature = new ASTClassSignature(node);
        current_scope_.add_alias(node->identifier_, signature);
        Scope& class_scope = Scope::make(current_scope_, node);
        class_scope.self_type_ = reinterpret_cast<const Type*>(1);
        SymbolCollectVisitor class_visitor(class_scope, sema_);
        for (auto& ctor : node->constructors_) {
            ctor->accept(class_visitor);
        }
        if (node->destructor_) {
            node->destructor_->accept(class_visitor);
        }
        for (auto& func : node->functions_) {
            func->accept(class_visitor);
        }
        class_scope.self_type_ = nullptr;
    }

    // Namespaces
    void visit(const ASTNamespaceDefinition* node) override {
        Scope& namespace_scope = Scope::make(current_scope_, node);
        SymbolCollectVisitor namespace_visitor(namespace_scope, sema_);
        for (auto& item : node->items_) {
            item->accept(namespace_visitor);
        }
        current_scope_.add_namespace(node->identifier_, namespace_scope);
    }

    // Templates
    void visit(const ASTTemplateDefinition* node) override {
        current_scope_.add_template(node->identifier_, node);
        Scope& template_scope = Scope::make(current_scope_, node);
        SymbolCollectVisitor template_visitor(template_scope, sema_);
        node->target_node_->accept(template_visitor);
    }
};
