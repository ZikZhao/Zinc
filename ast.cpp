#include "pch.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "exception.hpp"

ScopeDefinition::ScopeDefinition(const ScopeDefinition* parent)
    : parent_(parent), shadow_count(0) {}
ScopeDefinition::ScopeDefinition(const std::vector<std::pair<std::string, TypeRef>>& builtins)
    : parent_(nullptr), shadow_count(0) {
    for (const auto& [name, type] : builtins) {
        symbols.emplace_back(name, nullptr);
    }
}
uint64_t ScopeDefinition::get_length() const {
    return symbols.size();
}
void ScopeDefinition::add_symbol(const std::string& name, const ASTExpression* symbol) {
    for (const auto& [key, _] : symbols) {
        if (key == name) {
            throw std::runtime_error("Symbol already defined in this scope: "s + name);
        }
    }
    symbols.emplace_back(name, symbol);
}
bool ScopeDefinition::has_var(const std::string& name) const {
    for (const auto& [key, _] : symbols) {
        if (key == name) {
            return true;
        }
    }
    if (parent_) {
        return parent_->has_var(name);
    } else {
        return false;
    }
}
std::pair<uint64_t, bool> ScopeDefinition::get_var_offset(const std::string& name) const noexcept {
    for (auto it = symbols.begin(); it != symbols.end(); ++it) {
        const auto& [key, symbol] = *it;
        if (key == name) {
            return {
                static_cast<uint64_t>(std::distance(symbols.begin(), it)) + parent_->get_full_length(),
                parent_ == nullptr
            };
        }
    }
    return parent_->get_var_offset(name);
}
void ScopeDefinition::update_shadow_count(const ScopeDefinition& child) {
    shadow_count = std::max(shadow_count, child.shadow_count);
}
uint64_t ScopeDefinition::get_full_length() const {
    // Global scope does not count towards stack frame length
    return parent_ ? parent_->get_full_length() + symbols.size() : 0;
}

ASTNode::ASTNode(const Location& location) : location_(location) {}
void ASTNode::first_analyze(ScopeDefinition* scope) {}
void ASTNode::second_analyze(ScopeDefinition* scope) {}
void ASTNode::execute(Context& globals, Context& locals) const {}

std::vector<std::unique_ptr<ASTToken>> ASTToken::Instances;
ASTToken::ASTToken(const Location& location, const char* str) : ASTNode(location), str_(str) {}
void ASTToken::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Token("s + str_ + ")"s << std::endl;
}
void ASTToken::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute a token");
}

ASTCodeBlock::ASTCodeBlock(const Location& location)
    : ASTNode(location) {}
ASTCodeBlock::ASTCodeBlock(const Location& location, ASTNode* node)
    : ASTNode(location) {
    statements_.push_back(node);
}
ASTCodeBlock::~ASTCodeBlock() {
    for (const auto& stmt : statements_) {
        delete stmt;
    }
    delete local_scope_;
}
void ASTCodeBlock::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Statements"s << std::endl;
    for (const auto& stmt : statements_) {
        stmt->print(os, indent + 2);
    }
}
void ASTCodeBlock::first_analyze(ScopeDefinition* scope) {
    local_scope_ = new ScopeDefinition(scope);
    // Register declarations and type aliases
    for (auto& stmt : statements_) {
        stmt->first_analyze(local_scope_);
    }
    // Update shadow count in parent scope
    if (scope) {
        scope->update_shadow_count(*local_scope_);
    }
}
void ASTCodeBlock::second_analyze(ScopeDefinition* scope) {
    for (auto& stmt : statements_) {
        stmt->second_analyze(local_scope_);
    }
}
void ASTCodeBlock::execute(Context& globals, Context& locals) const {
    for (uint64_t i = 0; i < local_scope_->get_length(); ++i) {
        locals.emplace_back(nullptr);
    }
    for (const auto& stmt : statements_) {
        stmt->execute(globals, locals);
    }
    for (uint64_t i = 0; i < local_scope_->get_length(); ++i) {
        locals.pop_back();
    }
}
ASTCodeBlock& ASTCodeBlock::push(const Location& new_location, ASTNode* node) {
    statements_.push_back(node);
    location_ = new_location;
    return *this;
}

ASTIdentifier::ASTIdentifier(const ASTToken& token)
    : ASTExpression(token.location_), name_(token.str_) {}
void ASTIdentifier::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Identifier("s + name_ + ")"s << std::endl;
}
void ASTIdentifier::first_analyze(ScopeDefinition* scope) {
    if (!scope->has_var(name_)) {
        throw VariableException(name_);
    }
}
void ASTIdentifier::second_analyze(ScopeDefinition* scope) {
    try {
        auto [index, is_global] = scope->get_var_offset(name_);
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
ValueRef ASTIdentifier::eval(Context& globals, Context& locals) const {
    return is_global_ ? globals[index_] : locals[index_];
}

ASTFunctionCallArguments::ASTFunctionCallArguments() : ASTNode({{0, 0}, {0, 0}}) {}
ASTFunctionCallArguments::ASTFunctionCallArguments(const Location &location, const ASTExpression *first_arg)
    : ASTNode(location), arguments_{first_arg} {}
ASTFunctionCallArguments::~ASTFunctionCallArguments() {
    for (const auto& expr : arguments_) {
        delete expr;
    }
}
void ASTFunctionCallArguments::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionCallArguments"s << std::endl;
    for (const auto& expr : arguments_) {
        expr->print(os, indent + 2);
    }
}
ASTFunctionCallArguments& ASTFunctionCallArguments::push_back(const Location& new_location, const ASTExpression* arg) {
    arguments_.push_back(arg);
    location_ = new_location;
    return *this;
}
Arguments ASTFunctionCallArguments::eval_arguments(Context& globals, Context& locals) const {
    Arguments values;
    for (const auto& expr : arguments_) {
        values.emplace_back(expr->eval(globals, locals));
    }
    return values;
}

ASTFunctionCall::ASTFunctionCall(const Location& location, const ASTExpression* function, const ASTFunctionCallArguments* arguments)
    : ASTExpression(location), function_(function), arguments_(arguments ? arguments : new ASTFunctionCallArguments()) {}
ASTFunctionCall::~ASTFunctionCall() {
    delete function_;
    delete arguments_;
}
void ASTFunctionCall::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionCall"s << std::endl;
    function_->print(os, indent + 2);
    arguments_->print(os, indent + 2);
}
ValueRef ASTFunctionCall::eval(Context& globals, Context& locals) const {
    ValueRef result = function_->eval(globals, locals);
    CHECK(result->kind_ == KIND::KIND_FUNCTION);
    return static_cast<FunctionValue&>(*result)(arguments_->eval_arguments(globals, locals));
}

ASTDeclaration::ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTExpression* expr, const bool is_const)
    : ASTNode(location), is_const_(is_const), type_(nullptr), identifier_(identifier), expr_(expr), inferred_type_() {} // TODO
ASTDeclaration::ASTDeclaration(const Location& location, const ASTExpression* type, const ASTIdentifier* identifier, const ASTExpression* expr, const bool is_const)
    : ASTNode(location), is_const_(is_const), type_(type), identifier_(identifier), expr_(expr), inferred_type_() {} // TODO
ASTDeclaration::~ASTDeclaration() {
    delete type_;
    delete identifier_;
    delete expr_;
}
void ASTDeclaration::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Declaration"s << std::endl;
    identifier_->print(os, indent + 2);
    expr_->print(os, indent + 2);
}
void ASTDeclaration::first_analyze(ScopeDefinition* scope) {
    scope->add_symbol(identifier_->name_, type_);
}

ASTTypeAlias::ASTTypeAlias(const Location &location, const ASTIdentifier *identifier, const ASTExpression *type)
    : ASTNode(location), identifier_(identifier), type_(type) {}
ASTTypeAlias::~ASTTypeAlias() {
    delete identifier_;
    delete type_;
}
void ASTTypeAlias::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "TypeAlias"s << std::endl;
    identifier_->print(os, indent + 2);
    type_->print(os, indent + 2);
}
void ASTTypeAlias::first_analyze(ScopeDefinition* scope) {
    scope->add_symbol(identifier_->name_, type_);
}

ASTIfStatement::ASTIfStatement(const Location& location, const ASTExpression* condition, const ASTCodeBlock* const if_block, const ASTCodeBlock* const else_block)
    : ASTNode(location), condition_(condition), if_block_(if_block), else_block_(else_block) {}
ASTIfStatement::~ASTIfStatement() {
    delete condition_;
    delete if_block_;
    delete else_block_;
}
void ASTIfStatement::execute(Context& globals, Context& locals) const {
    if (condition_->eval(globals, locals).is_truthy()) {
        if_block_->execute(globals, locals);
    } else if (else_block_) {
        else_block_->execute(globals, locals);
    }
}
void ASTIfStatement::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "IfStatement"s << std::endl;
    condition_->print(os, indent + 2);
    if_block_->print(os, indent + 2);
    if (else_block_) {
        else_block_->print(os, indent + 2);
    }
}

ASTForStatement::ASTForStatement(const Location &location, const ASTNode *initializer, const ASTExpression *condition, const ASTExpression *increment, const ASTCodeBlock *body)
    : ASTNode(location), initializer_(initializer), condition_(condition), increment_(increment), body_(body) {}
ASTForStatement::~ASTForStatement() {
    delete initializer_;
    delete condition_;
    delete increment_;
    delete body_;
}
void ASTForStatement::execute(Context& globals, Context& locals) const {
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
void ASTForStatement::print(std::ostream& os, uint64_t indent) const {
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

ASTContinueStatement::ASTContinueStatement(const Location& location)
    : ASTNode(location) {}
void ASTContinueStatement::execute(Context& globals, Context& locals) const {
    throw ContinueException();
}
void ASTContinueStatement::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "ContinueStatement"s << std::endl;
}

ASTBreakStatement::ASTBreakStatement(const Location& location)
    : ASTNode(location) {}
void ASTBreakStatement::execute(Context& globals, Context& locals) const {
    throw BreakException();
}
void ASTBreakStatement::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "BreakStatement"s << std::endl;
}

ASTFunctionParameter::ASTFunctionParameter(const Location& location, const ASTIdentifier* identifier, const ASTExpression* type)
    : ASTNode(location), identifier_(identifier), type_(type) {}
ASTFunctionParameter::~ASTFunctionParameter() {
    delete type_;
    delete identifier_;
}
void ASTFunctionParameter::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionParameter"s << std::endl;
    type_->print(os, indent + 2);
    identifier_->print(os, indent + 2);
}

ASTFunctionSignature::ASTFunctionSignature(const Location& location, const ASTExpression* first_type, const ASTIdentifier* first_name)
    : ASTNode(location), parameters_{new ASTFunctionParameter(location, first_name, first_type)} {}
ASTFunctionSignature::~ASTFunctionSignature() {
    for (const auto& param : parameters_) {
        delete param;
    }
}
void ASTFunctionSignature::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionSignature"s << std::endl;
    for (const auto& param : parameters_) {
        param->print(os, indent + 2);
    }
}
ASTFunctionSignature& ASTFunctionSignature::push(const Location& new_location, ASTFunctionParameter* param) {
    parameters_.push_back(param);
    location_ = new_location;
    return *this;
}
Context ASTFunctionSignature::collect_arguments(const Arguments &raw) const {
    Context context;
    auto param_it = parameters_.begin();
    auto arg_it = raw.begin();
    for (; param_it != parameters_.end() && arg_it != raw.end(); ++param_it, ++arg_it) {
        context.emplace_back(*arg_it);
    }
    if (param_it != parameters_.end()) {
        throw ArgumentException("Not enough arguments provided to function call"s);
    }
    if (spread_) {
        std::vector<ValueRef> spread_args;
        for (; arg_it != raw.end(); ++arg_it) {
            spread_args.emplace_back(*arg_it);
        }
        context.emplace_back(new ListValue(std::move(spread_args)));
    } else if (arg_it != raw.end()) {
        throw ArgumentException("Too many arguments provided to function call"s);
    }
    return context;
}

ASTFunctionDefinition::ASTFunctionDefinition(const char* name)
    : ASTNode({{0, 0}, {0, 0}}), name_(name), signature_(nullptr), body_(nullptr) {}
ASTFunctionDefinition::ASTFunctionDefinition(const Location& location, const ASTIdentifier* name, const ASTFunctionSignature* signature, const ASTCodeBlock* body)
    : ASTNode(location), name_(name->name_), signature_(signature), body_(body) {}
ASTFunctionDefinition::~ASTFunctionDefinition() {
    delete signature_;
    delete body_;
}
void ASTFunctionDefinition::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionDefinition("s + name_ + ")"s << std::endl;
    signature_->print(os, indent + 2);
    body_->print(os, indent + 2);
}
