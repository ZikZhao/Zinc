#include <string>
#include <iostream>
#include <utility>
#include "ast.hpp"
#include "value.hpp"
#include "exception.hpp"
using namespace std::literals::string_literals;

std::ostream& operator<< (std::ostream& os, const Location& loc) {
    return os << loc.begin.line << ":" << loc.begin.column << "-" << loc.end.line << ":" << loc.end.column;
}

ValueRef LeftShiftFunctor::operator()(const ValueRef &left, const ValueRef &right) const {
    return left << right;
}

ValueRef RightShiftFunctor::operator()(const ValueRef &left, const ValueRef &right) const {
    return left >> right;
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

ASTConstant::ASTConstant(const ASTToken& token, LiteralType type)
    : ASTValueExpression(token.location), type(type), literal(token.str) {}
void ASTConstant::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Constant(" << literal << ")" << std::endl;
}
ValueRef ASTConstant::eval(Context& globals, Context& locals) const {
    return ValueRef(type, literal);
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
    for (const auto& arg : arguments) {
        delete arg;
    }
}
void ASTFunctionCallArguments::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "FunctionCallArguments"s << std::endl;
    for (const auto& arg : arguments) {
        arg->print(os, indent + 2);
    }
}
void ASTFunctionCallArguments::execute(Context& globals, Context& locals) const {
    throw std::runtime_error("Cannot execute function call arguments");
}
ASTFunctionCallArguments& ASTFunctionCallArguments::push_back(const ASTValueExpression* arg) {
    arguments.push_back(arg);
    return *this;
}
decltype(auto) ASTFunctionCallArguments::size() const {
    return arguments.size();
}
decltype(auto) ASTFunctionCallArguments::operator[] (uint64_t index) const {
    return arguments[index];
}
decltype(auto) ASTFunctionCallArguments::begin() {
    return arguments.begin();
}
decltype(auto) ASTFunctionCallArguments::begin() const {
    return arguments.begin();
}
decltype(auto) ASTFunctionCallArguments::end() {
    return arguments.end();
}
decltype(auto) ASTFunctionCallArguments::end() const {
    return arguments.end();
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
    const FunctionValue* func;
    if (not (func = dynamic_cast<const FunctionValue*>(&*result))) {
        throw std::runtime_error("Value is not callable");
    }
    return func->definition->call(globals, *arguments);
}

ASTDeclaration::ASTDeclaration(const Location& location, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const)
    : ASTNode(location), is_const(is_const), type(nullptr), identifier(identifier), expr(expr), inferred_type() {} // TODO
ASTDeclaration::ASTDeclaration(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* identifier, const ASTValueExpression* expr, const bool is_const)
    : ASTNode(location), is_const(is_const), type(type), identifier(identifier), expr(expr), inferred_type() {} // TODO
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

ASTFunctionParameter::ASTFunctionParameter(const Location& location, const ASTTypeExpression* type, const ASTIdentifier* name)
    : ASTNode(location), type(type), identifier(name) {}
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
    : ASTNode(location), parameters{new ASTFunctionParameter(location, first_type, first_name)} {}
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
ValueRef ASTFunctionDefinition::call(Context& globals, const ASTFunctionCallArguments& arguments) const {
    Context new_locals = prepare_locals(globals, arguments);
    try {
        execute(globals, new_locals);
    }
    catch (ReturnException& e) {
        return e.return_value;
    }
    std::unreachable();
}
Context ASTFunctionDefinition::prepare_locals(Context& globals, const ASTFunctionCallArguments& arguments) const {
    Context new_locals;
    const auto& params = signature->parameters;
    uint64_t index = 0;
    for (auto it = params.begin(); it != params.end(); ++it, ++index) {
        new_locals.emplace((*it)->identifier->name, arguments.size() > index ? arguments[index]->eval(globals, new_locals) : ValueRef());
    }
    return new_locals;
}

const std::map<std::string, ASTBuiltinFunctionDefinition> ASTBuiltinFunctionDefinition::BuiltinFunctions = {
    {"print", ASTBuiltinFunctionDefinition("print", [] (const std::vector<ValueRef>& args) {
        for (const auto& arg : args) {
            std::cout << static_cast<std::string>(arg) << " ";
        }
        std::cout << std::endl;
        return Constants::Null;
    })},
};
Context ASTBuiltinFunctionDefinition::InitGlobals() {
    Context globals;
    for (const auto& [name, func] : BuiltinFunctions) {
        globals.emplace(name, ValueRef(std::make_shared<FunctionValue>(&func)));
    }
    return globals;
}
ASTBuiltinFunctionDefinition::ASTBuiltinFunctionDefinition(const char *name, FuncType func)
    : ASTFunctionDefinition(name), func(func) {}
void ASTBuiltinFunctionDefinition::execute(Context& globals, Context& locals) const {}
void ASTBuiltinFunctionDefinition::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "BuiltinFunctionDefinition("s + name + ")"s << std::endl;
}
ValueRef ASTBuiltinFunctionDefinition::call(Context& globals, const ASTFunctionCallArguments& arguments) const {
    Context new_locals = prepare_locals(globals, arguments);
    // TODO: enable variadic parameters
    // new_locals.emplace("args", ValueRef(std::make_shared<ListValue>(std::vector<ValueRef>())));
    // return func(new_locals);
}
