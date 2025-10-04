#include <string>
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
