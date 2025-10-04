#include "ast.hpp"

using namespace std::literals::string_literals;

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

ASTConstant::ASTConstant(const ASTToken& token)
    : ASTValueExpression(token.location), ref(token.type, token.str.c_str()) {}
void ASTConstant::print(std::ostream& os, uint64_t indent) const {
    os << std::string(indent, ' ') << "Constant("s + static_cast<std::string>(ref) + ")"s << std::endl;
}
ValueRef ASTConstant::eval(Context& globals, Context& locals) const {
    return ref;
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
    locals.insert({identifier->name, expr ? expr->eval(globals, locals) : ValueRef()});
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