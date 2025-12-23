#include "ast.hpp"

#include "pch.hpp"
#include <string_view>

#include "entity.hpp"

Scope::Scope(std::string_view name, Scope& parent) noexcept : parent_(&parent), name_(name) {
    if (name.empty()) {
        parent_->anonymous_children_.push_back(this);
    } else {
        parent_->children_.emplace(name, this);
    }
}
Scope::Scope(std::vector<std::pair<std::string_view, const ASTExpression*>> builtins) noexcept {
    // for (const auto& [name, expr] : builtins) {
    //     variables_.emplace(name, expr);
    // }
}
void Scope::add_type(std::string_view identifier, const ASTTypeExpression* expr) {
    types_.emplace(identifier, expr);
}
void Scope::add_variable(std::string_view identifier, EntityRef expr) {
    if (types_.contains(identifier)) {
        throw std::runtime_error(
            "Variable '"s + std::string(identifier) + "' already declared as type"s
        );
    }
    variables_.emplace(identifier, expr);
}

TypeChecker::TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& type_factory) noexcept
    : current_(&root), ops_(ops), type_factory_(type_factory) {}
void TypeChecker::add_variable(std::string_view identifier, EntityRef expr) {
    current_->add_variable(identifier, expr);
}
void TypeChecker::enter(const ASTCodeBlock* child) noexcept {
    assert(
        child->name_.empty()
            ? std::ranges::contains(current_->anonymous_children_, child->local_scope_.get())
            : current_->children_.at(child->name_) == child->local_scope_.get()
    );
    current_ = child->local_scope_.get();
}
void TypeChecker::exit() noexcept { current_ = current_->parent_; }
EntityRef TypeChecker::resolve(std::string_view identifier) {
    return resolve_in(identifier, *current_);
}
TypeRef TypeChecker::type_of(std::string_view identifier) {
    if (auto it_var = current_->variables_.find(identifier); it_var != current_->variables_.end()) {
        return it_var->second;
    }
    if (auto it_type = current_->types_.find(identifier); it_type != current_->types_.end()) {
        throw std::runtime_error(
            "Identifier '"s + std::string(identifier) + "' is a type, not a variable"s
        );
    }
    throw std::runtime_error("Unknown identifier: '"s + std::string(identifier) + "'"s);
}
EntityRef TypeChecker::resolve_in(std::string_view identifier, Scope& ctx) {
    // Check cache
    auto& record = cache_[std::pair(current_, identifier)];
    if (record.is_resolving) {
        if (record.result) {
            return record.result;
        }
        throw std::runtime_error("Cyclic type dependency detected");
    }
    // Cache miss; resolve
    record.is_resolving = true;
    if (auto it_type = current_->types_.find(identifier); it_type != current_->types_.end()) {
        return it_type->second->eval(*this);
    }
    if (auto it_var = current_->variables_.find(identifier); it_var != current_->variables_.end()) {
        if (it_var->second.is_type()) {
            throw std::runtime_error(
                "Identifier '"s + std::string(identifier) + "' is a mutable variable"s
            );
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
void ASTNode::collect_types(Scope& ctx, OpDispatcher& ops) {}
void ASTNode::check_types(TypeChecker& checker) {}

ASTRecursiveNode::ASTRecursiveNode(const Location& loc) noexcept : ASTNode(loc) {}
void ASTRecursiveNode::collect_types(Scope& ctx, OpDispatcher& ops) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->collect_types(ctx, ops);
    }
}
void ASTRecursiveNode::check_types(TypeChecker& checker) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->check_types(checker);
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
void ASTCodeBlock::collect_types(Scope& ctx, OpDispatcher& ops) {
    local_scope_ = std::make_unique<Scope>(name_, ctx);
    for (auto& stmt : statements_) {
        stmt->collect_types(*local_scope_, ops);
    }
}
void ASTCodeBlock::check_types(TypeChecker& checker) {
    checker.enter(this);
    for (auto& stmt : statements_) {
        stmt->check_types(checker);
    }
    checker.exit();
}

ASTExpression::ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}

ASTIdentifier::ASTIdentifier(const Location& loc, std::string_view name) noexcept
    : ASTExpression(loc), name_(name) {}
EntityRef ASTIdentifier::eval(TypeChecker& checker) const { return checker.resolve(name_); }
TypeRef ASTIdentifier::get_result_type(TypeChecker& checker) const {
    return checker.type_of(name_);
}

ASTFunctionCall::ASTFunctionCall(
    const Location& loc,
    std::unique_ptr<ASTValueExpression> function,
    std::vector<std::unique_ptr<ASTValueExpression>> arguments
) noexcept
    : ASTExpression(loc), function_(std::move(function)), arguments_(std::move(arguments)) {}

ASTFunctionType::ASTFunctionType(
    const Location& loc,
    std::unique_ptr<ASTTypeExpression> return_type,
    ComparableSpan<std::unique_ptr<ASTTypeExpression>> parameter_types,
    std::unique_ptr<ASTTypeExpression> variadic_type
) noexcept
    : ASTTypeExpression(loc),
      representation_(
          Components(std::move(return_type), std::move(parameter_types), std::move(variadic_type))
      ) {}
ASTFunctionType::ASTFunctionType(TypeRef func) noexcept
    : ASTTypeExpression({}), representation_(func) {}
EntityRef ASTFunctionType::eval(TypeChecker& checker) const {
    if (std::holds_alternative<TypeRef>(representation_)) {
        return std::get<TypeRef>(representation_);
    } else {
        const auto& comps = std::get<Components>(representation_);
        Type* return_type = std::get<0>(comps)->eval(checker).type();
        auto param_rng = std::get<1>(comps) | std::views::transform([&](const auto& param_expr) {
                             return param_expr->eval(checker).type();
                         });
        ComparableSpan<Type*> param_types = GlobalMemory::collect_range<Type*>(param_rng);
        Type* variadic_type = nullptr;
        if (const auto& opt_variadic = std::get<2>(comps)) {
            variadic_type = opt_variadic->eval(checker).type();
        }
        return checker.type_factory_.get<FunctionType>(return_type, param_types, variadic_type);
    }
}
TypeRef ASTFunctionType::get_result_type(TypeChecker& checker) const {
    throw std::runtime_error("Function types cannot be used as values"s);
}

ASTRecordType::ASTRecordType(
    const Location& loc, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
) noexcept
    : ASTTypeExpression(loc), fields_(std::move(fields)) {}
EntityRef ASTRecordType::eval(TypeChecker& checker) const {
    auto rng = fields_ | std::views::transform([&](const auto& decl) {
                   return std::pair(decl->identifier_, checker.resolve(decl->identifier_).type());
               });
    FlatMap<std::string_view, Type*> field_types(std::from_range, std::move(rng));
    return checker.type_factory_.get<RecordType>(field_types);
}
TypeRef ASTRecordType::get_result_type(TypeChecker& checker) const {
    throw std::runtime_error("Record types cannot be used as values"s);
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
void ASTDeclaration::check_types(TypeChecker& checker) {
    TypeRef inferred_type = expr_->get_result_type(checker);
    TypeRef declared_type = type_->eval(checker);
    if (!declared_type) {
        declared_type = inferred_type;
    } else if (!declared_type.type()->assignable_from(*inferred_type.type())) {
        throw std::runtime_error(
            "Type mismatch in declaration of '"s + std::string(identifier_->name_) +
            "': expected " + declared_type->repr() + ", got " + inferred_type->repr()
        );
    }
    checker.add_variable(identifier_->name_, declared_type);
}

ASTFieldDeclaration::ASTFieldDeclaration(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTTypeExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

ASTTypeAlias::ASTTypeAlias(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

void ASTTypeAlias::collect_types(Scope& ctx, OpDispatcher& ops) {
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
