#include "ast.hpp"

#include "pch.hpp"
#include <string_view>

#include "diagnosis.hpp"
#include "object.hpp"

Scope::Scope(std::string_view name, Scope& parent) noexcept : parent_(&parent), name_(name) {
    if (name.empty()) {
        parent_->anonymous_children_.push_back(this);
    } else {
        parent_->children_.insert(name, this);
    }
}
void Scope::add_type(std::string_view identifier, const ASTTypeExpression* expr) {
    types_.insert(identifier, expr);
}
void Scope::add_variable(std::string_view identifier, ObjectRef expr) {
    if (types_.contains(identifier)) {
        throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
    }
    variables_.insert(identifier, expr);
}

TypeChecker::TypeChecker(Scope& root, const OpDispatcher& ops, TypeRegistry& types) noexcept
    : current_scope_(&root), ops_(ops), types_(types) {}
void TypeChecker::add_variable(std::string_view identifier, ObjectRef expr) {
    current_scope_->add_variable(identifier, expr);
}
void TypeChecker::enter(const ASTCodeBlock* child) noexcept {
    assert(
        child->name_.empty()
            ? std::ranges::contains(current_scope_->anonymous_children_, child->local_scope_.get())
            : current_scope_->children_.at(child->name_) == child->local_scope_.get()
    );
    current_scope_ = child->local_scope_.get();
}
void TypeChecker::exit() noexcept { current_scope_ = current_scope_->parent_; }
ObjectRef TypeChecker::resolve(std::string_view identifier) {
    return resolve_in(identifier, *current_scope_);
}
ExprResult TypeChecker::type_of(std::string_view identifier) {
    return type_of_in(identifier, *current_scope_);
}
ObjectRef TypeChecker::resolve_in(std::string_view identifier, Scope& scope) {
    // Check cache
    auto& record = cache_[std::pair(&scope, identifier)];
    if (record.is_resolving) {
        if (record.result) {
            return record.result;
        }
        throw UnlocatedProblem::make<CircularTypeDependencyError>(identifier);
    }
    // Cache miss; resolve
    record.is_resolving = true;
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        record.result = it_type->second->eval(*this);
        return record.result;
    }
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        if (it_var->second.is_type()) {
            throw UnlocatedProblem::make<NotConstantExpressionError>();
        } else {
            // constant (it_var->second is the value stored)
            record.result = types_.of(it_var->second.as_value());
            return record.result;
        }
    }
    if (scope.parent_ != nullptr) {
        record.result = resolve_in(identifier, *scope.parent_);
        return record.result;
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}
ExprResult TypeChecker::type_of_in(std::string_view identifier, Scope& scope) {
    if (auto it_var = scope.variables_.find(identifier); it_var != scope.variables_.end()) {
        TypeRef type_ref = it_var->second.is_type() ? it_var->second.as_type()
                                                    : types_.of(it_var->second.as_value());
        return ExprResult{type_ref, it_var->second.is_type()};
    }
    if (auto it_type = scope.types_.find(identifier); it_type != scope.types_.end()) {
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    }
    if (scope.parent_ != nullptr) {
        return type_of_in(identifier, *scope.parent_);
    }
    throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
}

ASTNode::ASTNode(const Location& loc) noexcept : location_(loc) {}
void ASTNode::collect_types(Scope& scope, OpDispatcher& ops) {}
void ASTNode::check_types(TypeChecker& checker) {}

ASTRecursiveNode::ASTRecursiveNode(const Location& loc) noexcept : ASTNode(loc) {}
void ASTRecursiveNode::collect_types(Scope& scope, OpDispatcher& ops) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->collect_types(scope, ops);
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
void ASTCodeBlock::collect_types(Scope& scope, OpDispatcher& ops) {
    local_scope_ = std::make_unique<Scope>(name_, scope);
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
void ASTExpression::check_types(TypeChecker& checker) { std::ignore = get_result_type(checker); }

ASTIdentifier::ASTIdentifier(const Location& loc, std::string_view name) noexcept
    : ASTExpression(loc), name_(name) {}
ObjectRef ASTIdentifier::eval(TypeChecker& checker) const { return checker.resolve(name_); }
ExprResult ASTIdentifier::get_result_type(TypeChecker& checker) const {
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
ObjectRef ASTFunctionType::eval(TypeChecker& checker) const {
    if (std::holds_alternative<TypeRef>(representation_)) {
        return std::get<TypeRef>(representation_);
    }
    const auto& comps = std::get<Components>(representation_);
    if (Type* return_type = std::get<0>(comps)->eval(checker).as_type()) {
        auto param_rng =
            std::get<1>(comps) | std::views::transform([&](const auto& param_expr) {
                if (Type* param_type = param_expr->eval(checker).as_type()) {
                    return param_type;
                }
                Diagnostic::report(SymbolCategoryMismatchError(param_expr->location_, true));
            });
        ComparableSpan<Type*> param_types = GlobalMemory::collect_range<Type*>(param_rng);
        TypeRef variadic_type;  // TODO: check if TypeRef compiles
        if (const auto& opt_variadic = std::get<2>(comps)) {
            if (variadic_type = opt_variadic->eval(checker).as_type(); variadic_type) {
                throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
            }
        }
        return checker.types_.get<FunctionType>(return_type, param_types, variadic_type);
    }
    Diagnostic::report(SymbolCategoryMismatchError(std::get<0>(comps)->location_, true));
}

ExprResult ASTFunctionType::get_result_type(TypeChecker& checker) const {
    Diagnostic::report(SymbolCategoryMismatchError(location_, false));
}

ASTRecordType::ASTRecordType(
    const Location& loc, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields
) noexcept
    : ASTTypeExpression(loc), fields_(std::move(fields)) {}
ObjectRef ASTRecordType::eval(TypeChecker& checker) const {
    auto rng = fields_ | std::views::transform([&](const auto& decl) {
                   if (TypeRef type = checker.resolve(decl->identifier_).as_type()) {
                       return std::pair(decl->identifier_, type);
                   }
                   throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
               });
    FlatMap<std::string_view, Type*> field_types(std::from_range, std::move(rng));
    return checker.types_.get<RecordType>(field_types);
}
ExprResult ASTRecordType::get_result_type(TypeChecker& checker) const {
    Diagnostic::report(SymbolCategoryMismatchError(location_, false));
}

ASTDeclaration::ASTDeclaration(
    const Location& loc,
    std::unique_ptr<ASTIdentifier> identifier,
    std::unique_ptr<ASTTypeExpression> type,
    std::unique_ptr<ASTValueExpression> expr,
    bool is_mutable
) noexcept
    : ASTNode(loc),
      identifier_(std::move(identifier)),
      type_(std::move(type)),
      expr_(std::move(expr)),
      is_mutable_(is_mutable) {}
void ASTDeclaration::check_types(TypeChecker& checker) {
    if (is_mutable_) {
        ExprResult inferred_type = expr_->get_result_type(checker);
        TypeRef declared_type = get_declared_type(checker, inferred_type.type_ref);
        checker.add_variable(identifier_->name_, declared_type);
    } else {
        ObjectRef value = expr_->eval(checker);
        if (value.is_type()) {
            Diagnostic::report(SymbolCategoryMismatchError(expr_->location_, false));
        }
        std::ignore = get_declared_type(checker, checker.types_.of(value.as_value()));
        checker.add_variable(identifier_->name_, value);
    }
}
TypeRef ASTDeclaration::get_declared_type(TypeChecker& checker, TypeRef inferred_type) const {
    if (type_) {
        TypeRef declared_type = type_->eval(checker).as_type();
        if (!declared_type) {
            Diagnostic::report(SymbolCategoryMismatchError(type_->location_, true));
        }
        if (!declared_type->assignable_from(*inferred_type)) {
            Diagnostic::report(
                TypeMismatchError(location_, declared_type->repr(), inferred_type->repr())
            );
        }
        return declared_type;
    } else {
        return inferred_type;
    }
}

ASTFieldDeclaration::ASTFieldDeclaration(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTTypeExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

ASTTypeAlias::ASTTypeAlias(
    const Location& loc, std::string_view identifier, std::unique_ptr<ASTExpression> type
) noexcept
    : ASTNode(loc), identifier_(std::move(identifier)), type_(std::move(type)) {}

void ASTTypeAlias::collect_types(Scope& scope, OpDispatcher& ops) {
    scope.add_type(identifier_, type_.get());
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
