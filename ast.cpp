#include "pch.hpp"
#include "ast.hpp"
#include "value.hpp"
#include "exception.hpp"

std::ostream& operator << (std::ostream& os, const Location& loc) {
    return os << loc.begin.line << ":" << loc.begin.column << "-" << loc.end.line << ":" << loc.end.column;
}

ASTNode::ASTNode(const Location& location) : location(location) {}

std::vector<ASTToken> ASTToken::Instances;
ASTToken::ASTToken(const Location& location, const char* str, int64_t type) : ASTNode(location), str(str), type(type) {}
void ASTToken::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Token("s + str + ")"s << std::endl;
}
void ASTToken::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute a token");
}

ASTStatements::ASTStatements(const Location& location)
    : ASTNode(location) {}
ASTStatements::ASTStatements(const Location& location, ASTNode* node)
    : ASTNode(location) {
    statements.push_back(node);
}
ASTStatements::~ASTStatements() {
    for (const auto& stmt : statements) {
        delete stmt;
    }
}
void ASTStatements::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Statements"s << std::endl;
    for (const auto& stmt : statements) {
        stmt->print(os, indent + 2);
    }
}
void ASTStatements::execute(Context& globals, Context& locals) const {
    for (const auto& stmt : statements) {
        stmt->execute(globals, locals);
    }
}
ASTStatements& ASTStatements::push(const Location& new_location, ASTNode* node) {
    statements.push_back(node);
    location = new_location;
    return *this;
}

ASTIdentifier::ASTIdentifier(const ASTToken& token)
    : ASTValueExpression(token.location), name(token.str) {}
void ASTIdentifier::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Identifier("s + name + ")"s << std::endl;
}
ValueRef ASTIdentifier::eval(Context& globals, Context& locals) const {
    return locals.contains(name) ? locals.at(name) : globals.at(name);
}

ASTFunctionCallArguments::ASTFunctionCallArguments() : ASTNode({{0, 0}, {0, 0}}) {}
ASTFunctionCallArguments::ASTFunctionCallArguments(const Location &location, const ASTValueExpression *first_arg)
    : ASTNode(location), arguments{first_arg} {}
ASTFunctionCallArguments::~ASTFunctionCallArguments() {
    for (const auto& expr : arguments) {
        delete expr;
    }
}
void ASTFunctionCallArguments::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionCallArguments"s << std::endl;
    for (const auto& expr : arguments) {
        expr->print(os, indent + 2);
    }
}
void ASTFunctionCallArguments::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute function call arguments");
}
ASTFunctionCallArguments& ASTFunctionCallArguments::push_back(const Location& new_location, const ASTValueExpression* arg) {
    arguments.push_back(arg);
    location = new_location;
    return *this;
}
Arguments ASTFunctionCallArguments::eval_arguments(Context& globals, Context& locals) const {
    Arguments values;
    for (const auto& expr : arguments) {
        values.emplace_back(expr->eval(globals, locals));
    }
    return values;
}

ASTFunctionCall::ASTFunctionCall(const Location& location, const ASTValueExpression* function, const ASTFunctionCallArguments* arguments)
    : ASTValueExpression(location), function(function), arguments(arguments ? arguments : new ASTFunctionCallArguments()) {}
ASTFunctionCall::~ASTFunctionCall() {
    delete function;
    delete arguments;
}
void ASTFunctionCall::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionCall"s << std::endl;
    function->print(os, indent + 2);
    arguments->print(os, indent + 2);
}
ValueRef ASTFunctionCall::eval(Context& globals, Context& locals) const {
    ValueRef result = function->eval(globals, locals);
    return result(globals, arguments->eval_arguments(globals, locals));
}

ASTDeclaration::ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const)
    : ASTNode(location), is_const(is_const), type(nullptr), identifier(identifier), expr(expr), inferred_type() {} // TODO
ASTDeclaration::ASTDeclaration(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const)
    : ASTNode(location), is_const(is_const), type(type), identifier(identifier), expr(expr), inferred_type() {} // TODO
ASTDeclaration::~ASTDeclaration() {
    delete type;
    delete identifier;
    delete expr;
}
void ASTDeclaration::execute(Context& globals, Context& locals) const {
    locals.emplace(identifier->name, expr ? expr->eval(globals, locals) : ValueRef());
}
void ASTDeclaration::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Declaration"s << std::endl;
    identifier->print(os, indent + 2);
    expr->print(os, indent + 2);
}

ASTIfStatement::ASTIfStatement(const Location& location, const ASTValueExpression* condition, const ASTStatements* const if_block, const ASTStatements* const else_block)
    : ASTNode(location), condition(condition), if_block(if_block), else_block(else_block) {}
ASTIfStatement::~ASTIfStatement() {
    delete condition;
    delete if_block;
    delete else_block;
}
void ASTIfStatement::execute(Context& globals, Context& locals) const {
    if (condition->eval(globals, locals).is_truthy()) {
        if_block->execute(globals, locals);
    } else if (else_block) {
        else_block->execute(globals, locals);
    }
}
void ASTIfStatement::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "IfStatement"s << std::endl;
    condition->print(os, indent + 2);
    if_block->print(os, indent + 2);
    if (else_block) {
        else_block->print(os, indent + 2);
    }
}

ASTForStatement::ASTForStatement(const Location &location, const ASTNode *initializer, const ASTValueExpression *condition, const ASTValueExpression *increment, const ASTStatements *body)
    : ASTNode(location), initializer(initializer), condition(condition), increment(increment), body(body) {}
ASTForStatement::~ASTForStatement() {
    delete initializer;
    delete condition;
    delete increment;
    delete body;
}
void ASTForStatement::execute(Context& globals, Context& locals) const {
    if (initializer) {
        initializer->execute(globals, locals);
    }
    while (condition ? condition->eval(globals, locals).is_truthy() : true) {
        try {
            body->execute(globals, locals);
        }
        catch (BreakException&) {
            break;
        }
        catch (ContinueException&) {}
        if (increment) {
            increment->eval(globals, locals);
        }
    }
}
void ASTForStatement::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "ForStatement"s << std::endl;
    if (initializer) {
        initializer->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No initializer"s << std::endl;
    }
    if (condition) {
        condition->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No condition"s << std::endl;
    }
    if (increment) {
        increment->print(os, indent + 2);
    } else {
        os << std::string(indent + 2, ' ') << "No increment"s << std::endl;
    }
    body->print(os, indent + 2);
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

ASTFunctionParameter::ASTFunctionParameter(const Location& location, const ASTIdentifier* identifier, const ASTTypeExpression* type)
    : ASTNode(location), identifier(identifier), type(type) {}
ASTFunctionParameter::~ASTFunctionParameter() {
    delete type;
    delete identifier;
}
void ASTFunctionParameter::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute a function parameter");
}
void ASTFunctionParameter::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionParameter"s << std::endl;
    type->print(os, indent + 2);
    identifier->print(os, indent + 2);
}

ASTFunctionSignature::ASTFunctionSignature(const Location& location, const ASTTypeExpression* first_type, const ASTIdentifier* first_name)
    : ASTNode(location), parameters{new ASTFunctionParameter(location, first_name, first_type)} {}
ASTFunctionSignature::~ASTFunctionSignature() {
    for (const auto& param : parameters) {
        delete param;
    }
}
void ASTFunctionSignature::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute a function signature");
}
void ASTFunctionSignature::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionSignature"s << std::endl;
    for (const auto& param : parameters) {
        param->print(os, indent + 2);
    }
}
ASTFunctionSignature& ASTFunctionSignature::push(const Location& new_location, ASTFunctionParameter* param) {
    parameters.push_back(param);
    location = new_location;
    return *this;
}
Context ASTFunctionSignature::collect_arguments(const Arguments &raw) const {
    Context context;
    auto param_it = parameters.begin();
    auto arg_it = raw.begin();
    for (; param_it != parameters.end() && arg_it != raw.end(); ++param_it, ++arg_it) {
        context.emplace((*param_it)->identifier->name, *arg_it);
    }
    if (param_it != parameters.end()) {
        throw ArgumentException("Not enough arguments provided to function call"s);
    }
    if (spread_param) {
        std::vector<ValueRef> spread_args;
        for (; arg_it != raw.end(); ++arg_it) {
            spread_args.emplace_back(*arg_it);
        }
        context.emplace(spread_param->identifier->name, new ListValue(std::move(spread_args)));
    } else if (arg_it != raw.end()) {
        throw ArgumentException("Too many arguments provided to function call"s);
    }
    return context;
}

ASTFunctionDefinition::ASTFunctionDefinition(const char* name)
    : ASTNode({{0, 0}, {0, 0}}), name(name), signature(nullptr), body(nullptr) {}
ASTFunctionDefinition::ASTFunctionDefinition(const Location& location, const ASTIdentifier* name, const ASTFunctionSignature* signature, const ASTStatements* body)
    : ASTNode(location), name(name->name), signature(signature), body(body) {}
ASTFunctionDefinition::~ASTFunctionDefinition() {
    delete signature;
    delete body;
}
void ASTFunctionDefinition::execute(Context& globals, Context& locals) const {}
void ASTFunctionDefinition::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionDefinition("s + name + ")"s << std::endl;
    signature->print(os, indent + 2);
    body->print(os, indent + 2);
}
