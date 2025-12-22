#include "ast.hpp"

#include "pch.hpp"
#include <string_view>

#include "entity.hpp"

Context::Context(std::string_view name, Context& parent) noexcept : parent_(&parent), name_(name) {
    if (name.empty()) {
        parent_->anonymous_children_.push_back(this);
    } else {
        parent_->children_.emplace(name, this);
    }
}
Context::Context(std::vector<std::pair<std::string_view, const ASTExpression*>> builtins) noexcept {
    // for (const auto& [name, expr] : builtins) {
    //     variables_.emplace(name, expr);
    // }
}
void Context::add_type(std::string_view identifier, const ASTTypeExpression* expr) {
    types_.emplace(identifier, expr);
}
void Context::add_variable(std::string_view identifier, EntityRef expr) {
    if (types_.contains(identifier)) {
        throw std::runtime_error(
            "Variable '"s + std::string(identifier) + "' already declared as type"s
        );
    }
    variables_.emplace(identifier, expr);
}

TypeResolver::TypeResolver(
    Context& root, const OperationTable& ops, TypeFactory& type_factory
) noexcept
    : current_(&root), ops_(ops), type_factory_(type_factory) {}
void TypeResolver::add_variable(std::string_view identifier, EntityRef expr) {
    current_->add_variable(identifier, expr);
}
void TypeResolver::enter(const ASTCodeBlock* child) noexcept {
    assert(
        child->name_.empty()
            ? std::ranges::contains(current_->anonymous_children_, child->local_scope_.get())
            : current_->children_.at(child->name_) == child->local_scope_.get()
    );
    current_ = child->local_scope_.get();
}
void TypeResolver::exit() noexcept { current_ = current_->parent_; }
TypeRef TypeResolver::resolve(std::string_view identifier) {
    return resolve_in(identifier, *current_);
}
TypeRef TypeResolver::resolve_in(std::string_view identifier, Context& ctx) {
    // Check cache
    auto record = cache_[std::pair(current_, identifier)];
    if (record.is_resolving) {
        if (record.type) {
            return record.type;
        }
        throw std::runtime_error("Cyclic type dependency detected");
    }
    // Cache miss; resolve
    record.is_resolving = true;
    if (auto it_type = current_->types_.find(identifier); it_type != current_->types_.end()) {
        return it_type->second->eval_type(*this);
    }
    if (auto it_var = current_->variables_.find(identifier); it_var != current_->variables_.end()) {
        if (it_var->second->is_type()) {
            // mutable variable (it_var->second is the type of the variable)
            return it_var->second;
        } else {
            // constant (it_var->second is the value stored)
            return type_factory_.of(it_var->second);
        }
    }
    if (current_->parent_ != nullptr) {
        return resolve_in(identifier, *current_->parent_);
    }
    throw std::runtime_error("Unknown type identifier: '"s + std::string(identifier) + "'");
}

ASTNode::ASTNode(const Location& loc) noexcept : location_(loc) {}
void ASTNode::first_analyze(Context& ctx, OperationTable& ops) {}
void ASTNode::second_analyze(TypeResolver& tr) {}

ASTRecursiveNode::ASTRecursiveNode(const Location& loc) noexcept : ASTNode(loc) {}
void ASTRecursiveNode::first_analyze(Context& ctx, OperationTable& ops) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->first_analyze(ctx, ops);
    }
}
void ASTRecursiveNode::second_analyze(TypeResolver& tr) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->second_analyze(tr);
    }
}

ASTCodeBlock::ASTCodeBlock(
    const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements
) noexcept
    : ASTRecursiveNode(loc), statements_(std::move(statements)) {}
std::generator<ASTNode*> ASTCodeBlock::get_children() const noexcept {
    for (const auto& stmt : statements_) {
        co_yield stmt.get();
    }
}
void ASTCodeBlock::first_analyze(Context& ctx, OperationTable& ops) {
    local_scope_ = std::make_unique<Context>(name_, ctx);
    for (auto& stmt : statements_) {
        stmt->first_analyze(*local_scope_, ops);
    }
}
void ASTCodeBlock::second_analyze(TypeResolver& tr) {
    tr.enter(this);
    for (auto& stmt : statements_) {
        stmt->second_analyze(tr);
    }
    tr.exit();
}

ASTExpression::ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}

ASTIdentifier::ASTIdentifier(const Location& loc, std::string_view name) noexcept
    : ASTExpression(loc), name_(name) {}
TypeRef ASTIdentifier::eval_type(TypeResolver& tr) const noexcept { return tr.resolve(name_); }

ASTFunctionCall::ASTFunctionCall(
    const Location& loc,
    std::unique_ptr<ASTValueExpression> function,
    std::vector<std::unique_ptr<ASTValueExpression>> arguments
) noexcept
    : ASTExpression(loc), function_(std::move(function)), arguments_(std::move(arguments)) {}

ASTFunctionType::ASTFunctionType(TypeRef func) noexcept
    : ASTTypeExpression({0, {0, 0}, {0, 0}}), value_(func) {}
ASTFunctionType::ASTFunctionType(const Location& loc, TypeRef func) noexcept
    : ASTTypeExpression(loc), value_(func) {}
TypeRef ASTFunctionType::eval_type(TypeResolver& tr) const noexcept { return value_; }

ASTRecordType::ASTRecordType(
    const Location& loc, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
) noexcept
    : ASTTypeExpression(loc), fields_(std::move(fields)) {}

TypeRef ASTRecordType::eval_type(TypeResolver& tr) const noexcept {
    auto rng = fields_ | std::views::transform([&](const auto& decl) {
                   return std::pair(decl->identifier_, tr.resolve(decl->identifier_));
               });
    std::map<std::string, TypeRef> field_types(rng.begin(), rng.end());
    return tr.type_factory_.make<RecordType>(field_types);
}

ASTDeclaration::ASTDeclaration(
    const Location& loc,
    std::unique_ptr<ASTIdentifier> identifier,
    std::unique_ptr<ASTTypeExpression> type,
    std::unique_ptr<ASTValueExpression> expr
) noexcept
    : ASTNode(loc),
      identifier_(std::move(identifier)),
      type_(std::move(type)),
      expr_(std::move(expr)) {}
void ASTDeclaration::second_analyze(TypeResolver& tr) {
    TypeRef inferred_type = expr_->eval_type(tr);
    TypeRef declared_type = type_->eval_type(tr);
    if (!declared_type) {
        declared_type = inferred_type;
    } else if (!Type::contains(*declared_type.type(), *inferred_type.type())) {
        throw std::runtime_error(
            "Type mismatch in declaration of '"s + std::string(identifier_->name_) +
            "': expected " + declared_type->repr() + ", got " + inferred_type->repr()
        );
    }
    tr.add_variable(identifier_->name_, declared_type);
}

ASTFieldDeclaration::ASTFieldDeclaration(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTTypeExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

ASTTypeAlias::ASTTypeAlias(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

void ASTTypeAlias::first_analyze(Context& ctx, OperationTable& ops) {
    ctx.add_type(identifier_, type_.get());
}

ASTIfStatement::ASTIfStatement(
    const Location& loc,
    std::unique_ptr<ASTExpression> condition,
    std::unique_ptr<ASTCodeBlock> if_block,
    std::unique_ptr<ASTCodeBlock> else_block
) noexcept
    : ASTRecursiveNode(loc),
      condition_(std::move(condition)),
      if_block_(std::move(if_block)),
      else_block_(std::move(else_block)) {}
std::generator<ASTNode*> ASTIfStatement::get_children() const noexcept {
    co_yield condition_.get();
    co_yield if_block_.get();
    if (else_block_) {
        co_yield else_block_.get();
    }
}

ASTForStatement::ASTForStatement(
    const Location& loc,
    std::unique_ptr<ASTDeclaration> initializer,
    std::unique_ptr<ASTExpression> condition,
    std::unique_ptr<ASTExpression> increment,
    std::unique_ptr<ASTCodeBlock> body
) noexcept
    : ASTRecursiveNode(loc),
      initializer_(std::move(initializer)),
      condition_(std::move(condition)),
      increment_(std::move(increment)),
      body_(std::move(body)) {}
ASTForStatement::ASTForStatement(
    const Location& loc,
    std::unique_ptr<ASTValueExpression> initializer,
    std::unique_ptr<ASTExpression> condition,
    std::unique_ptr<ASTExpression> increment,
    std::unique_ptr<ASTCodeBlock> body
) noexcept
    : ASTRecursiveNode(loc),
      initializer_(std::move(initializer)),
      condition_(std::move(condition)),
      increment_(std::move(increment)),
      body_(std::move(body)) {}
ASTForStatement::ASTForStatement(const Location& loc, std::unique_ptr<ASTCodeBlock> body) noexcept
    : ASTRecursiveNode(loc), body_(std::move(body)) {}
std::generator<ASTNode*> ASTForStatement::get_children() const noexcept {
    if (initializer_) {
        co_yield initializer_.get();
    }
    if (condition_) {
        co_yield condition_.get();
    }
    if (increment_) {
        co_yield increment_.get();
    }
    co_yield body_.get();
}

ASTContinueStatement::ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}

ASTBreakStatement::ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}

ASTReturnStatement::ASTReturnStatement(
    const Location& loc, std::unique_ptr<ASTExpression> expr
) noexcept
    : ASTNode(loc), expr_(std::move(expr)) {}

ASTFunctionParameter::ASTFunctionParameter(
    const Location& loc,
    std::unique_ptr<const ASTIdentifier> identifier,
    std::unique_ptr<const ASTExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

ASTFunctionSignature::ASTFunctionSignature(
    const Location& loc,
    std::unique_ptr<ASTExpression> first_type,
    std::unique_ptr<ASTIdentifier> first_name
) noexcept
    : ASTNode(loc) {
    parameters_.push_back(
        std::make_unique<ASTFunctionParameter>(loc, std::move(first_name), std::move(first_type))
    );
}

ASTFunctionSignature& ASTFunctionSignature::push(
    const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
) noexcept {
    parameters_.push_back(std::move(next_param));
    location_ = new_location;
    return *this;
}
ASTFunctionSignature& ASTFunctionSignature::push_spread(
    const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param
) noexcept {
    spread_ = std::move(next_param);
    location_ = new_location;
    return *this;
}
ASTFunctionSignature& ASTFunctionSignature::set_return_type(
    const Location& new_location, std::unique_ptr<ASTExpression> return_type
) noexcept {
    return_type_ = std::move(return_type);
    location_ = new_location;
    return *this;
}

ASTFunctionDefinition::ASTFunctionDefinition(
    const Location& loc,
    std::unique_ptr<const ASTIdentifier> name,
    std::unique_ptr<const ASTFunctionSignature> signature,
    std::unique_ptr<const ASTCodeBlock> body
) noexcept
    : ASTNode(loc), name_(name->name_), signature_(std::move(signature)), body_(std::move(body)) {}
