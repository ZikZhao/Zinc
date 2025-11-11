#include "pch.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "exception.hpp"

void TypeSystem::add(const ASTTypeExpression* expr) {
    types_.emplace_back(expr);
}
void TypeSystem::resolve() {
    std::map<const ASTTypeExpression*, std::set<const ASTTypeExpression*>> dependency_map;
    std::multimap<const ASTTypeExpression*, const ASTTypeExpression*> reverse_dependency_map;
    for (const auto& type_expr : types_) {
        // generator -> set is not compilable in G++ 14
        auto& deps = dependency_map[type_expr];
        for (const auto& dep : type_expr->get_dependencies()) {
            deps.insert(dep);
            reverse_dependency_map.emplace(dep, type_expr);
        }
    }
    while (!dependency_map.empty()) {
        size_t prev_size = dependency_map.size();
        for (auto it = dependency_map.begin(); it != dependency_map.end(); ) {
            const auto& [type, dependents] = *it;
            if (dependents.empty()) {
                type_map_[type] = type->eval(*this);
                auto range = reverse_dependency_map.equal_range(type);
                for (auto rev_it = range.first; rev_it != range.second; ++rev_it) {
                    dependency_map[rev_it->second].erase(type);
                }
                reverse_dependency_map.erase(range.first, range.second);
                dependency_map.erase(it++);
            }
            else {
                ++it;
            }
        }
        if (dependency_map.size() == prev_size) {
            throw std::runtime_error("Cyclic type dependencies detected");
        }
    }
}
TypeRef TypeSystem::eval(const ASTTypeExpression* expr) const noexcept {
    return type_map_.at(expr);
}

Scope::Scope(Scope& parent) noexcept : parent_(&parent) {}
Scope::Scope(Scope& enclosing, int) noexcept : enclosing_(&enclosing) {}
Scope::Scope(const std::vector<std::pair<std::string, TypeRef>>& builtins) noexcept {
    for (const auto& [name, type] : builtins) {
        symbols_.emplace_back(name, nullptr);
    }
}
std::uint64_t Scope::get_length() const noexcept {
    return symbols_.size();
}
void Scope::add(std::string name, const ASTExpression* symbol) {
    for (const auto& [key, _] : symbols_) {
        if (key == name) {
            throw std::runtime_error("Symbol already defined in this scope: "s + name);
        }
    }
    symbols_.emplace_back(std::move(name), symbol);
}
std::pair<std::uint64_t, bool> Scope::get_offset(std::string_view name) const {
    for (auto it = symbols_.begin(); it != symbols_.end(); ++it) {
        const auto& [key, symbol] = *it;
        if (key == name) {
            const std::uint64_t offset = get_parent_offset();
            return {
                static_cast<std::uint64_t>(std::distance(symbols_.begin(), it)) + offset,
                is_global()
            };
        }
    }
    if (not parent_) {
        throw VariableException(name);
    }
    return parent_->get_offset(name);
}
void Scope::update_parent_shadow() const noexcept {
    assert(parent_ != nullptr);
    parent_->shadow_count_ = std::max(parent_->shadow_count_, symbols_.size() + shadow_count_);
}
std::uint64_t Scope::get_parent_offset() const noexcept {
    if (parent_) {
        return parent_->get_parent_offset() + parent_->symbols_.size();
    }
    else {
        return 0;
    }
}
bool Scope::is_global() const noexcept {
    if (enclosing_) {
        return false;
    } else if (not parent_) {
        return true;
    } else {
        return parent_->is_global();
    }
}

ValueRef RuntimeStack::operator [] (std::uint64_t index) noexcept {
    assert(index < symbols_.size());
    return symbols_[index];
}
void RuntimeStack::enter(const Scope& scope) noexcept {
    for (std::uint64_t i = 0; i < scope.get_length(); ++i) {
        symbols_.emplace_back(nullptr);
    }
}
void RuntimeStack::exit(const Scope& scope) noexcept {
    for (std::uint64_t i = 0; i < scope.get_length(); ++i) {
        symbols_.pop_back();
    }
}

ASTNode::ASTNode(const Location& loc) noexcept : location_(loc) {}
std::generator<ASTNode*> ASTNode::get_children() const noexcept {
    co_return;
}
void ASTNode::first_analyze(TypeSystem& ts) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->first_analyze(ts);
    }
}
void ASTNode::second_analyze(TypeSystem& ts, Scope& scope) {
    for (const auto& child : get_children()) {
        if (child == nullptr) {
            continue;
        }
        child->second_analyze(ts, scope);
    }
}
void ASTNode::execute(RuntimeStack& globals, RuntimeStack& locals) const {}

ASTCodeBlock::ASTCodeBlock(const Location& loc, std::vector<std::unique_ptr<ASTNode>> statements) noexcept
    : ASTNode(loc), statements_(std::move(statements)) {}
std::generator<ASTNode*> ASTCodeBlock::get_children() const noexcept {
    for (const auto& stmt : statements_) {
        co_yield stmt.get();
    }
}
void ASTCodeBlock::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "Statements"s << std::endl;
    for (const auto& stmt : statements_) {
        stmt->print(os, indent + 2);
    }
}
void ASTCodeBlock::second_analyze(TypeSystem& ts, Scope& scope) {
    local_scope_ = std::make_unique<Scope>(scope);
    for (auto& stmt : statements_) {
        stmt->second_analyze(ts, *local_scope_);
    }
}
void ASTCodeBlock::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    locals.enter(*local_scope_);
    for (const auto& stmt : statements_) {
        stmt->execute(globals, locals);
    }
    locals.exit(*local_scope_);
}
ASTCodeBlock& ASTCodeBlock::push(const Location& new_location, std::unique_ptr<ASTNode> node) noexcept {
    statements_.push_back(std::move(node));
    location_ = new_location;
    return *this;
}

ASTIdentifier::ASTIdentifier(const Location& loc, std::string name) noexcept
    : ASTExpression(loc), name_(std::move(name)) {}
void ASTIdentifier::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "Identifier("s + name_ + ")"s << std::endl;
}
void ASTIdentifier::second_analyze(TypeSystem& ts, Scope& scope) {
    try {
        auto [index, is_global] = scope.get_offset(name_);
        this->index_ = index;
        this->is_global_ = is_global;
    } catch (TypeException& e) {
        e.location_ = location_;
        throw;
    } catch (VariableException& e) {
        e.location_ = location_;
        throw;
    }
}
ValueRef ASTIdentifier::eval(RuntimeStack& globals, RuntimeStack& locals) const noexcept {
    return is_global_ ? globals[index_] : locals[index_];
}

ASTFunctionCall::ASTFunctionCall(const Location& location, std::unique_ptr<ASTValueExpression> function, std::vector<std::unique_ptr<ASTValueExpression>> arguments) noexcept
    : ASTExpression(location), function_(std::move(function)), arguments_(std::move(arguments)) {}
std::generator<ASTNode*> ASTFunctionCall::get_children() const noexcept {
    co_yield function_.get();
    for (const auto& arg : arguments_) {
        co_yield arg.get();
    }
}
void ASTFunctionCall::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "FunctionCall"s << std::endl;
    function_->print(os, indent + 2);
    for (const auto& arg : arguments_) {
        arg->print(os, indent + 2);
    }
}
ValueRef ASTFunctionCall::eval(RuntimeStack& globals, RuntimeStack& locals) const {
    ValueRef result = function_->eval(globals, locals);
    std::vector<ValueRef> arguments_ref = arguments_
        | std::views::transform([&](const auto& arg) { return arg->eval(globals, locals); })
        | std::ranges::to<std::vector<ValueRef>>();
    assert(result->kind_ == KIND::KIND_FUNCTION);
    return static_cast<FunctionValue&>(*result)(arguments_ref);
}

ASTRecordType::ASTRecordType(const Location& location, std::vector<std::unique_ptr<ASTFieldDeclaration>> fields) noexcept
    : ASTTypeExpression(location), fields_(std::move(fields)) {}
std::generator<ASTNode*> ASTRecordType::get_children() const noexcept {
    for (const auto& field : fields_) {
        co_yield field.get();
    }
}
void ASTRecordType::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "RecordType"s << std::endl;
    // TODO
}
void ASTRecordType::first_analyze(TypeSystem& ts) {
    ts.add(this);
    for (const auto& field : fields_) {
        field->first_analyze(ts);
    }
}
TypeRef ASTRecordType::eval(const TypeSystem& ts) const noexcept {
    std::map<std::string, TypeRef> field_types = fields_
        | std::views::transform([&](const auto& decl) { return std::pair(decl->identifier_, ts.eval(decl->type_.get())); })
        | std::ranges::to<std::map>();
    return new RecordType(field_types);
}
std::generator<const ASTTypeExpression*> ASTRecordType::get_dependencies() const noexcept {
    for (const auto& field : fields_) {
        co_yield field->type_.get();
    }
}

ASTDeclaration::ASTDeclaration(const Location& location, std::unique_ptr<ASTIdentifier> identifier, std::unique_ptr<ASTTypeExpression> type, std::unique_ptr<ASTValueExpression> expr) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)), expr_(std::move(expr)), inferred_type_() {} // TODO
std::generator<ASTNode*> ASTDeclaration::get_children() const noexcept {
    co_yield type_.get();
    if (expr_) {
        co_yield expr_.get();
    }
}
void ASTDeclaration::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "Declaration"s << std::endl;
    os << std::string(indent + 2, ' ') << "Identifier: " << identifier_ << std::endl;
    expr_->print(os, indent + 2);
}
void ASTDeclaration::second_analyze(TypeSystem& ts, Scope& scope) {
    scope.add(identifier_->name_, type_.get());
    identifier_->second_analyze(ts, scope);
}
void ASTDeclaration::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    ValueRef value = expr_->eval(globals, locals);
    identifier_->eval(globals, locals) = value;
}

ASTFieldDeclaration::ASTFieldDeclaration(const Location& location, std::string identifier, std::unique_ptr<ASTTypeExpression> type) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}
std::generator<ASTNode*> ASTFieldDeclaration::get_children() const noexcept {
    co_yield type_.get();
}
void ASTFieldDeclaration::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "FieldDeclaration"s << std::endl;
    os << std::string(indent + 2, ' ') << "Identifier: " << identifier_ << std::endl;
    type_->print(os, indent + 2);
}

ASTTypeAlias::ASTTypeAlias(const Location &location, std::string identifier, std::unique_ptr<ASTExpression> type) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}
std::generator<ASTNode*> ASTTypeAlias::get_children() const noexcept {
    co_yield type_.get();
}
void ASTTypeAlias::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "TypeAlias"s << std::endl;
    os << std::string(indent + 2, ' ') << "Identifier: " << identifier_ << std::endl;
    type_->print(os, indent + 2);
}

ASTIfStatement::ASTIfStatement(const Location& location, std::unique_ptr<ASTExpression> condition, std::unique_ptr<ASTCodeBlock> if_block, std::unique_ptr<ASTCodeBlock> else_block) noexcept
    : ASTNode(location), condition_(std::move(condition)), if_block_(std::move(if_block)), else_block_(std::move(else_block)) {}
std::generator<ASTNode*> ASTIfStatement::get_children() const noexcept {
    co_yield condition_.get();
    co_yield if_block_.get();
    if (else_block_) {
        co_yield else_block_.get();
    }
}
void ASTIfStatement::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "IfStatement"s << std::endl;
    condition_->print(os, indent + 2);
    if_block_->print(os, indent + 2);
    if (else_block_) {
        else_block_->print(os, indent + 2);
    }
}
void ASTIfStatement::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    if (condition_->eval(globals, locals).is_truthy()) {
        if_block_->execute(globals, locals);
    } else if (else_block_) {
        else_block_->execute(globals, locals);
    }
}

ASTForStatement::ASTForStatement(const Location &location, std::unique_ptr<ASTNode> initializer, std::unique_ptr<ASTExpression> condition, std::unique_ptr<ASTExpression> increment, std::unique_ptr<ASTCodeBlock> body) noexcept
    : ASTNode(location), initializer_(std::move(initializer)), condition_(std::move(condition)), increment_(std::move(increment)), body_(std::move(body)) {}
ASTForStatement::ASTForStatement(const Location &location, std::unique_ptr<ASTCodeBlock> body) noexcept
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
void ASTForStatement::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "ForStatement"s << std::endl;
    if (initializer_) {
        initializer_->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No initializer"s << std::endl;
    }
    if (condition_) {
        condition_->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No condition"s << std::endl;
    }
    if (increment_) {
        increment_->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No increment"s << std::endl;
    }
    body_->print(os, indent + 2);
}
void ASTForStatement::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    if (initializer_) {
        initializer_->execute(globals, locals);
    }
    while (condition_ ? condition_->eval(globals, locals).is_truthy() : true) {
        try {
            body_->execute(globals, locals);
        }
        catch (BreakException&) {
            break;
        }
        catch (ContinueException&) {}
        if (increment_) {
            increment_->eval(globals, locals);
        }
    }
}

ASTContinueStatement::ASTContinueStatement(const Location& location) noexcept
    : ASTNode(location) {}
void ASTContinueStatement::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "ContinueStatement"s << std::endl;
}
void ASTContinueStatement::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    throw ContinueException();
}

ASTBreakStatement::ASTBreakStatement(const Location& location) noexcept
    : ASTNode(location) {}
void ASTBreakStatement::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "BreakStatement"s << std::endl;
}
void ASTBreakStatement::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    throw BreakException();
}

ASTReturnStatement::ASTReturnStatement(const Location& location, std::unique_ptr<ASTExpression> expr) noexcept
    : ASTNode(location), expr_(std::move(expr)) {}
std::generator<ASTNode*> ASTReturnStatement::get_children() const noexcept {
    if (expr_) {
        co_yield expr_.get();
    }
}
void ASTReturnStatement::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "ReturnStatement"s << std::endl;
    if (expr_) {
        expr_->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No return value"s << std::endl;
    }
}
void ASTReturnStatement::execute(RuntimeStack& globals, RuntimeStack& locals) const {
    ValueRef return_value = expr_ ? expr_->eval(globals, locals) : nullptr;
    throw ReturnException(return_value);
}

ASTFunctionParameter::ASTFunctionParameter(const Location& location, std::unique_ptr<const ASTIdentifier> identifier, std::unique_ptr<const ASTExpression> type) noexcept
    : ASTNode(location), identifier_(std::move(identifier)), type_(std::move(type)) {}
void ASTFunctionParameter::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "FunctionParameter"s << std::endl;
    type_->print(os, indent + 2);
    identifier_->print(os, indent + 2);
}

ASTFunctionSignature::ASTFunctionSignature(const Location& location, std::unique_ptr<ASTExpression> first_type, std::unique_ptr<ASTIdentifier> first_name) noexcept
    : ASTNode(location) {
    parameters_.push_back(std::make_unique<ASTFunctionParameter>(location, std::move(first_name), std::move(first_type)));
}
void ASTFunctionSignature::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "FunctionSignature"s << std::endl;
    for (const auto& param : parameters_) {
        param->print(os, indent + 2);
    }
}
ASTFunctionSignature& ASTFunctionSignature::push(const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param) noexcept {
    parameters_.push_back(std::move(next_param));
    location_ = new_location;
    return *this;
}
ASTFunctionSignature& ASTFunctionSignature::push_spread(const Location& new_location, std::unique_ptr<ASTFunctionParameter> next_param) noexcept {
    spread_ = std::move(next_param);
    location_ = new_location;
    return *this;
}
ASTFunctionSignature& ASTFunctionSignature::set_return_type(const Location& new_location, std::unique_ptr<ASTExpression> return_type) noexcept {
    return_type_ = std::move(return_type);
    location_ = new_location;
    return *this;
}
RuntimeStack ASTFunctionSignature::collect_arguments(const Arguments &raw) const {
    RuntimeStack stack;
    auto param_it = parameters_.begin();
    auto arg_it = raw.begin();
    // TODO: Collect regular parameters
    if (param_it != parameters_.end()) {
        throw ArgumentException("Not enough arguments provided to function call"s);
    }
    if (spread_) {
        std::vector<ValueRef> spread_args;
        for (; arg_it != raw.end(); ++arg_it) {
            spread_args.emplace_back(*arg_it);
        }
        // ScopeStorage.emplace_back(new ListValue(std::move(spread_args)));
    } else if (arg_it != raw.end()) {
        throw ArgumentException("Too many arguments provided to function call"s);
    }
    return stack;
}

ASTFunctionDefinition::ASTFunctionDefinition(const Location& location, std::unique_ptr<const ASTIdentifier> name, std::unique_ptr<const ASTFunctionSignature> signature, std::unique_ptr<const ASTCodeBlock> body) noexcept
    : ASTNode(location), name_(name->name_), signature_(std::move(signature)), body_(std::move(body)) {}
void ASTFunctionDefinition::print(std::ostream& os, uint64_t indent) const noexcept {
    os << std::string(indent, ' ') << "FunctionDefinition("s + name_ + ")"s << std::endl;
    signature_->print(os, indent + 2);
    body_->print(os, indent + 2);
}
