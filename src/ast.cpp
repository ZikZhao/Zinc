#include "ast.hpp"

#include "object.hpp"
#include "pch.hpp"

ConstantStringPool StringPool;

Context::Context(Context& parent) noexcept : parent_(&parent) {}
Context::Context(std::string_view name, Context& parent) noexcept : parent_(&parent), name_(name) {}
Context::Context(std::vector<std::pair<std::string_view, const ASTExpression*>> builtins) noexcept {
    for (const auto& [name, expr] : builtins) {
        symbols_.emplace(name, expr);
    }
}
void Context::add_symbol(std::string_view identifier, const ASTValueExpression* expr) noexcept {
    symbols_.emplace(identifier, expr);
}
void Context::add_type(std::string_view identifier, const ASTTypeExpression* expr) noexcept {
    types_.emplace(identifier, expr);
}

TypeResolver::TypeResolver(const Context& root) noexcept : current_(&root) {}
void TypeResolver::enter(const ASTCodeBlock* child) noexcept {
    current_ = child->local_scope_.get();
}
void TypeResolver::exit() noexcept { current_ = current_->parent_; }
TypeRef TypeResolver::resolve(std::string_view identifier) {
    auto record = cache_[std::pair(current_, identifier)];
    if (record.is_resolving) {
        if (!record.type_ref.null()) {
            return record.type_ref;
        }
        throw std::runtime_error("Cyclic type dependency detected");
    }
    record.is_resolving = true;
    record.type_ref = current_->types_.at(identifier)->eval(*this);
    return record.type_ref;
}

ASTNode::ASTNode(const Location& loc) noexcept : location_(loc) {}
std::generator<ASTNode*> ASTNode::get_children() const noexcept { co_return; }
void ASTNode::analyze(Context& ctx) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->analyze(ctx);
    }
}

ASTCodeBlock::ASTCodeBlock(
    const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements
) noexcept
    : ASTNode(loc), statements_(std::move(statements)) {}
std::generator<ASTNode*> ASTCodeBlock::get_children() const noexcept {
    for (const auto& stmt : statements_) {
        co_yield stmt.get();
    }
}
void ASTCodeBlock::analyze(Context& ctx) {
    local_scope_ = std::make_unique<Context>(name_, ctx);
    for (auto& stmt : statements_) {
        stmt->analyze(*local_scope_);
    }
}

ASTIdentifier::ASTIdentifier(const Location& loc, const std::string& name) noexcept
    : ASTExpression(loc), name_(StringPool.make(name)) {}

ASTFunctionCall::ASTFunctionCall(
    const Location& location,
    std::unique_ptr<ASTValueExpression> function,
    std::vector<std::unique_ptr<ASTValueExpression>> arguments
) noexcept
    : ASTExpression(location), function_(std::move(function)), arguments_(std::move(arguments)) {}
std::generator<ASTNode*> ASTFunctionCall::get_children() const noexcept {
    co_yield function_.get();
    for (const auto& arg : arguments_) {
        co_yield arg.get();
    }
}

ASTRecordType::ASTRecordType(
    const Location& location, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
) noexcept
    : ASTTypeExpression(location), fields_(std::move(fields)) {}
std::generator<ASTNode*> ASTRecordType::get_children() const noexcept {
    for (const auto& field : fields_) {
        co_yield field.get();
    }
}

TypeRef ASTRecordType::eval(TypeResolver& tr) const noexcept {
    std::map<std::string, TypeRef> field_types =
        fields_ | std::views::transform([&](const auto& decl) {
            return std::pair(decl->identifier_, tr.resolve(decl->identifier_));
        }) |
        std::ranges::to<std::map>();
    return new RecordType(field_types);
}
std::generator<const ASTIdentifier*> ASTRecordType::get_dependencies() const noexcept {
    for (const auto& field : fields_) {
        for (const auto& dep : field->type_->get_dependencies()) {
            co_yield dep;
        }
    }
}

ASTDeclaration::ASTDeclaration(
    const Location& location,
    std::unique_ptr<ASTIdentifier> identifier,
    std::unique_ptr<ASTTypeExpression> type,
    std::unique_ptr<ASTValueExpression> expr
) noexcept
    : ASTNode(location),
      identifier_(std::move(identifier)),
      type_(std::move(type)),
      expr_(std::move(expr)),
      inferred_type_() {}  // TODO
std::generator<ASTNode*> ASTDeclaration::get_children() const noexcept {
    co_yield type_.get();
    if (expr_) {
        co_yield expr_.get();
    }
}

ASTFieldDeclaration::ASTFieldDeclaration(
    const Location& location, std::string identifier, std::unique_ptr<ASTTypeExpression> type
) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}
std::generator<ASTNode*> ASTFieldDeclaration::get_children() const noexcept {
    co_yield type_.get();
}

ASTTypeAlias::ASTTypeAlias(
    const Location& location, std::string identifier, std::unique_ptr<ASTExpression> type
) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}
std::generator<ASTNode*> ASTTypeAlias::get_children() const noexcept { co_yield type_.get(); }

void ASTTypeAlias::analyze(Context& ctx) {
    ctx.add_type(identifier_, type_.get());
    type_->analyze(ctx);
}

ASTIfStatement::ASTIfStatement(
    const Location& location,
    std::unique_ptr<ASTExpression> condition,
    std::unique_ptr<ASTCodeBlock> if_block,
    std::unique_ptr<ASTCodeBlock> else_block
) noexcept
    : ASTNode(location),
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
    const Location& location,
    std::unique_ptr<ASTNode> initializer,
    std::unique_ptr<ASTExpression> condition,
    std::unique_ptr<ASTExpression> increment,
    std::unique_ptr<ASTCodeBlock> body
) noexcept
    : ASTNode(location),
      initializer_(std::move(initializer)),
      condition_(std::move(condition)),
      increment_(std::move(increment)),
      body_(std::move(body)) {}
ASTForStatement::ASTForStatement(
    const Location& location, std::unique_ptr<ASTCodeBlock> body
) noexcept
    : ASTNode(location), body_(std::move(body)) {}
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

ASTContinueStatement::ASTContinueStatement(const Location& location) noexcept : ASTNode(location) {}

ASTBreakStatement::ASTBreakStatement(const Location& location) noexcept : ASTNode(location) {}

ASTReturnStatement::ASTReturnStatement(
    const Location& location, std::unique_ptr<ASTExpression> expr
) noexcept
    : ASTNode(location), expr_(std::move(expr)) {}
std::generator<ASTNode*> ASTReturnStatement::get_children() const noexcept {
    if (expr_) {
        co_yield expr_.get();
    }
}

ASTFunctionParameter::ASTFunctionParameter(
    const Location& location,
    std::unique_ptr<const ASTIdentifier> identifier,
    std::unique_ptr<const ASTExpression> type
) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}

ASTFunctionSignature::ASTFunctionSignature(
    const Location& location,
    std::unique_ptr<ASTExpression> first_type,
    std::unique_ptr<ASTIdentifier> first_name
) noexcept
    : ASTNode(location) {
    parameters_.push_back(
        std::make_unique<ASTFunctionParameter>(
            location, std::move(first_name), std::move(first_type)
        )
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
    const Location& location,
    std::unique_ptr<const ASTIdentifier> name,
    std::unique_ptr<const ASTFunctionSignature> signature,
    std::unique_ptr<const ASTCodeBlock> body
) noexcept
    : ASTNode(location),
      name_(name->name_),
      signature_(std::move(signature)),
      body_(std::move(body)) {}
