#include "pch.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "StainlessLexer.h"
#include "StainlessParser.h"
#include "StainlessBaseVisitor.h"

class ASTBuilder final : public StainlessBaseVisitor {
private:
    const SourceManager& source_manager_;
    std::map<antlr4::tree::ParseTree*, std::unique_ptr<ASTNode>> node_map_;
private:
    std::unique_ptr<ASTNode> extract_node(auto ctx) {
        auto node = node_map_.extract(ctx);
        assert(node);
        return std::move(node.mapped());
    }
    Location loc(antlr4::ParserRuleContext* context) {
        auto start = context->getStart();
        auto stop = context->getStop();
        Location location;
        location.id = source_manager_.index(start->getTokenSource()->getSourceName());
        location.begin.line = start->getLine();
        location.begin.column = start->getCharPositionInLine();
        location.end.line = stop->getLine();
        location.end.column = stop->getCharPositionInLine() + stop->getText().size();
        return location;
    }
public:
    ASTBuilder(const SourceManager& source_manager) : source_manager_(source_manager) {}
    std::unique_ptr<ASTCodeBlock> extract_root() {
        return StaticUniqueCast<ASTCodeBlock>(std::move(node_map_.begin()->second));
    }
    antlrcpp::Any visitProgram(StainlessParser::ProgramContext* context) final {
        for (auto stmt_ctx : context->statement()) {
            visit(stmt_ctx);
        }
        std::vector<std::unique_ptr<ASTNode>> statements = context->statement()
            | std::views::transform([this](auto ctx) { return extract_node(ctx); })
            | std::ranges::to<std::vector>();
        node_map_[context] = std::make_unique<ASTCodeBlock>(loc(context), std::move(statements));
        return {};
    }
    antlrcpp::Any visitStatement(StainlessParser::StatementContext* context) final {
        antlr4::tree::ParseTree* dispatch_ctx = context->children[0];
        visit(dispatch_ctx);
        node_map_[context] = extract_node(dispatch_ctx);
        return {};
    }
    antlrcpp::Any visitCode_block(StainlessParser::Code_blockContext* context) final {
        const auto& statements = context->statement();
        std::vector<std::unique_ptr<ASTNode>> nodes;
        nodes.reserve(statements.size());
        for (auto stmt_ctx : statements) {
            visit(stmt_ctx);
            nodes.push_back(extract_node(stmt_ctx));
        }
        node_map_[context] = std::make_unique<ASTCodeBlock>(loc(context), std::move(nodes));
        return {};
    }
    antlrcpp::Any visitExpr_statement(StainlessParser::Expr_statementContext* context) final {
        visit(context->expr());
        node_map_[context] = extract_node(context->expr());
        return {};
    }
    antlrcpp::Any visitDeclaration(StainlessParser::DeclarationContext* context) final {
        visit(static_cast<StainlessParser::TypeContext*>(context->type()));
        visit(static_cast<StainlessParser::IdentifierContext*>(context->identifier()));
        visit(static_cast<StainlessParser::ExprContext*>(context->expr()));
        node_map_[context] = std::make_unique<ASTDeclaration>(
            loc(context),
            StaticUniqueCast<ASTTypeExpression>(extract_node(context->type())),
            StaticUniqueCast<ASTIdentifier>(extract_node(context->identifier())),
            StaticUniqueCast<ASTValueExpression>(extract_node(context->expr()))
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(StainlessParser::If_statementContext* context) final {
        visit(context->condition_);
        visit(context->then_);
        if (context->else_) {
            visit(context->else_);
        }
        node_map_[context] = std::make_unique<ASTIfStatement>(
            loc(context),
            StaticUniqueCast<ASTExpression>(extract_node(context->condition_)),
            StaticUniqueCast<ASTCodeBlock>(extract_node(context->then_)),
            context->else_ ? StaticUniqueCast<ASTCodeBlock>(extract_node(context->else_)) : nullptr
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(StainlessParser::For_statementContext* context) final {
        std::unique_ptr<ASTNode> initializer;
        if (context->init_decl_) {
            visit(context->init_decl_);
            initializer = extract_node(context->init_decl_);
        } else if (context->init_expr_) {
            visit(context->init_expr_);
            initializer = extract_node(context->init_expr_);
        }
        visit(context->condition_);
        visit(context->update_);
        visit(context->body_);
        node_map_[context] = std::make_unique<ASTForStatement>(
            loc(context),
            std::move(initializer),
            StaticUniqueCast<ASTExpression>(extract_node(context->condition_)),
            StaticUniqueCast<ASTExpression>(extract_node(context->update_)),
            StaticUniqueCast<ASTCodeBlock>(extract_node(context->body_))
        );
        return {};
    }
    antlrcpp::Any visitBreak_statement(StainlessParser::Break_statementContext* context) final {
        node_map_[context] = std::make_unique<ASTBreakStatement>(loc(context));
        return {};
    }
    antlrcpp::Any visitContinue_statement(StainlessParser::Continue_statementContext* context) final {
        node_map_[context] = std::make_unique<ASTContinueStatement>(loc(context));
        return {};
    }
    antlrcpp::Any visitReturn_statement(StainlessParser::Return_statementContext* context) final {
        if (context->expr()) {
            visit(context->expr());
            node_map_[context] = std::make_unique<ASTReturnStatement>(
                loc(context),
                StaticUniqueCast<ASTExpression>(extract_node(context->expr()))
            );
        } else {
            node_map_[context] = std::make_unique<ASTReturnStatement>(
                loc(context),
                nullptr
            );
        }
        return {};
    }
    antlrcpp::Any visitAssignExpr(StainlessParser::AssignExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_ASSIGN:
                node_map_[context] = std::make_unique<ASTAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_ADD_ASSIGN:
                node_map_[context] = std::make_unique<ASTAddAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_SUB_ASSIGN:
                node_map_[context] = std::make_unique<ASTSubtractAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_MUL_ASSIGN:
                node_map_[context] = std::make_unique<ASTMultiplyAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_DIV_ASSIGN:
                node_map_[context] = std::make_unique<ASTDivideAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_REM_ASSIGN:
                node_map_[context] = std::make_unique<ASTRemainderAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_AND_ASSIGN:
                node_map_[context] = std::make_unique<ASTLogicalAndAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_OR_ASSIGN:
                node_map_[context] = std::make_unique<ASTLogicalOrAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_BITAND_ASSIGN:
                node_map_[context] = std::make_unique<ASTBitwiseAndAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_BITOR_ASSIGN:
                node_map_[context] = std::make_unique<ASTBitwiseOrAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_BITXOR_ASSIGN:
                node_map_[context] = std::make_unique<ASTBitwiseXorAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_LSHIFT_ASSIGN:
                node_map_[context] = std::make_unique<ASTLeftShiftAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_RSHIFT_ASSIGN:
                node_map_[context] = std::make_unique<ASTRightShiftAssignOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitEqualityExpr(StainlessParser::EqualityExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_EQ:
                node_map_[context] = std::make_unique<ASTEqualOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_NEQ:
                node_map_[context] = std::make_unique<ASTNotEqualOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitRelationalExpr(StainlessParser::RelationalExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_LT:
                node_map_[context] = std::make_unique<ASTLessThanOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_LTE:
                node_map_[context] = std::make_unique<ASTLessEqualOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_GT:
                node_map_[context] = std::make_unique<ASTGreaterThanOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_GTE:
                node_map_[context] = std::make_unique<ASTGreaterEqualOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitShiftExpr(StainlessParser::ShiftExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_LSHIFT:
                node_map_[context] = std::make_unique<ASTLeftShiftOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_RSHIFT:
                node_map_[context] = std::make_unique<ASTRightShiftOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitAdditiveExpr(StainlessParser::AdditiveExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_ADD:
                node_map_[context] = std::make_unique<ASTAddOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_SUB:
                node_map_[context] = std::make_unique<ASTSubtractOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitMultiplicativeExpr(StainlessParser::MultiplicativeExprContext* context) final {
        visit(context->left_);
        visit(context->right_);
        std::unique_ptr<ASTExpression> left = StaticUniqueCast<ASTExpression>(extract_node(context->left_));
        std::unique_ptr<ASTExpression> right = StaticUniqueCast<ASTExpression>(extract_node(context->right_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_MUL:
                node_map_[context] = std::make_unique<ASTMultiplyOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_DIV:
                node_map_[context] = std::make_unique<ASTDivideOp>(loc(context), std::move(left), std::move(right));
                break;
            case StainlessParser::OP_REM:
                node_map_[context] = std::make_unique<ASTRemainderOp>(loc(context), std::move(left), std::move(right));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitUnaryExpr(StainlessParser::UnaryExprContext* context) final {
        visit(context->expr_);
        std::unique_ptr<ASTExpression> expr = StaticUniqueCast<ASTExpression>(extract_node(context->expr_));
        switch (context->op_->getType()) {
            case StainlessParser::OP_INC:
                node_map_[context] = std::make_unique<ASTIncrementOp>(loc(context), std::move(expr));
                break;
            case StainlessParser::OP_DEC:
                node_map_[context] = std::make_unique<ASTDecrementOp>(loc(context), std::move(expr));
                break;
            case StainlessParser::OP_SUB:
                node_map_[context] = std::make_unique<ASTNegateOp>(loc(context), std::move(expr));
                break;
            case StainlessParser::OP_NOT:
                node_map_[context] = std::make_unique<ASTLogicalNotOp>(loc(context), std::move(expr));
                break;
            case StainlessParser::OP_BITNOT:
                node_map_[context] = std::make_unique<ASTBitwiseNotOp>(loc(context), std::move(expr));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitConstExpr(StainlessParser::ConstExprContext* context) final {
        visit(context->constant());
        node_map_[context] = extract_node(context->constant());
        return {};
    }
    antlrcpp::Any visitCallExpr(StainlessParser::CallExprContext* context) final {
        visit(context->func_);
        const auto& arguments = context->arguments_;
        std::vector<std::unique_ptr<ASTValueExpression>> nodes;
        nodes.reserve(arguments.size());
        for (auto arg_ctx : arguments) {
            visit(arg_ctx);
            nodes.push_back(StaticUniqueCast<ASTValueExpression>(extract_node(arg_ctx)));
        }
        node_map_[context] = std::make_unique<ASTFunctionCall>(
            loc(context),
            StaticUniqueCast<ASTValueExpression>(extract_node(context->func_)),
            std::move(nodes)
        );
        return {};
    }
    antlrcpp::Any visitParenExpr(StainlessParser::ParenExprContext* context) final {
        visit(context->inner_expr_);
        node_map_[context] = extract_node(context->inner_expr_);
        return {};
    }
    antlrcpp::Any visitIdentifierExpr(StainlessParser::IdentifierExprContext* context) final {
        visit(context->identifier_);
        node_map_[context] = extract_node(context->identifier_);
        return {};
    }
    antlrcpp::Any visitIdentifier(StainlessParser::IdentifierContext* context) final {
        node_map_[context] = std::make_unique<ASTIdentifier>(loc(context), context->name_->getText());
        return {};
    }
    antlrcpp::Any visitConstant(StainlessParser::ConstantContext* context) final {
        switch (context->value_->getType()) {
            case StainlessParser::T_INT:
                node_map_[context] = std::make_unique<ASTConstant<IntegerValue>>(
                    loc(context), context->value_->getText());
                break;
            case StainlessParser::T_FLOAT:
                node_map_[context] = std::make_unique<ASTConstant<FloatValue>>(
                    loc(context), context->value_->getText());
                break;
            case StainlessParser::T_STRING:
                node_map_[context] = std::make_unique<ASTConstant<StringValue>>(
                    loc(context), context->value_->getText());
                break;
            case StainlessParser::T_BOOL:
                node_map_[context] = std::make_unique<ASTConstant<BooleanValue>>(
                    loc(context), context->value_->getText());
                break;
            case StainlessParser::KW_NULL:
                node_map_[context] = std::make_unique<ASTConstant<NullValue>>(
                    loc(context), context->value_->getText());
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitPrimitiveType(StainlessParser::PrimitiveTypeContext* context) final {
        switch (context->primitive_->getType()) {
            case StainlessParser::KW_NULL:
                node_map_[context] = std::make_unique<ASTPrimitiveType<NullType>>(loc(context));
                break;
            case StainlessParser::KW_INT:
                node_map_[context] = std::make_unique<ASTPrimitiveType<IntegerType>>(loc(context));
                break;
            case StainlessParser::KW_FLOAT:
                node_map_[context] = std::make_unique<ASTPrimitiveType<FloatType>>(loc(context));
                break;
            case StainlessParser::KW_STRING:
                node_map_[context] = std::make_unique<ASTPrimitiveType<StringType>>(loc(context));
                break;
            case StainlessParser::KW_BOOL:
                node_map_[context] = std::make_unique<ASTPrimitiveType<BooleanType>>(loc(context));
                break;
            default:
                assert(false);
                std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitParenType(StainlessParser::ParenTypeContext* context) final {
        visit(context->inner_type_);
        node_map_[context] = extract_node(context->inner_type_);
        return {};
    }
    antlrcpp::Any visitIdentifierType(StainlessParser::IdentifierTypeContext* context) final {
        visit(context->identifier_);
        node_map_[context] = extract_node(context->identifier_);
        return {};
    }
};

namespace Builtins {
    struct BuiltinFunction {
        const std::string_view name;
        const TypeRef type;
        const std::function<ValueRef (const Arguments&)> func;
    };
    BuiltinFunction Print = {
        "print",
        new FunctionType({}, TypeRef(new AnyType()), TypeRef(new NullType())),
        [](const Arguments& args) -> ValueRef {
            try {
                std::cout << args.at(0)->repr() << std::endl;
                return new NullValue();
            } catch (const std::out_of_range& e) {
                throw ArgumentException(e.what());
            }
        },
    };
    const std::vector<BuiltinFunction*> AllBuiltins = {
        &Print,
    };
    ScopeDefinition GetBuiltinsScope() {
        return ScopeDefinition(AllBuiltins | std::views::transform([](BuiltinFunction* builtin) {
            return std::pair(std::string(builtin->name), builtin->type);
        }) | std::ranges::to<std::vector<std::pair<std::string, TypeRef>>>());
    }
    ScopeStorage GetBuiltinsScopeStorage() {
        return AllBuiltins | std::views::transform([](BuiltinFunction* builtin) {
            return ValueRef(new FunctionValue(builtin->func, builtin->type));
        }) | std::ranges::to<std::vector<ValueRef>>();
    }
}

int main(int argc, char* argv[]) {

    SourceManager source_manager;

    std::string_view input_path = (argc > 1) ? argv[1] : "<stdin>";
    auto file = source_manager[input_path];
    antlr4::ANTLRInputStream stream(file.content);
    stream.name = file.path;
    StainlessLexer lexer(&stream);
    antlr4::CommonTokenStream tokens(&lexer);
    StainlessParser parser(&tokens);

    StainlessParser::ProgramContext* tree = parser.program();
    std::cout << tree->toStringTree(&parser, true) << std::endl;

    ASTBuilder builder(source_manager);
    builder.visit(tree);
    std::unique_ptr<ASTCodeBlock> root = builder.extract_root();

    ScopeDefinition builtins = Builtins::GetBuiltinsScope();
    root->first_analyze(builtins);
    root->second_analyze(builtins);
    ScopeStorage globals = Builtins::GetBuiltinsScopeStorage();
    root->execute(globals, globals);
}