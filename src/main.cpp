#include "pch.hpp"
#include <string_view>

#include "StainlessBaseVisitor.h"
#include "StainlessLexer.h"
#include "StainlessParser.h"
#include "ast.hpp"
#include "exception.hpp"
#include "object.hpp"

class ASTBuilder final : private StainlessBaseVisitor {
private:
    const SourceManager& source_manager_;
    std::unique_ptr<ASTNode> last_visited_;

private:
    std::unique_ptr<ASTNode> transform(antlr4::tree::ParseTree* ctx) noexcept {
        if (ctx) {
            StainlessBaseVisitor::visit(ctx);
            return std::move(last_visited_);
        } else {
            return nullptr;
        }
    }
    template <typename... Args>
        requires(sizeof...(Args) > 1)
    std::unique_ptr<ASTNode> transform_group(Args... contexts) noexcept {
        assert(
            (static_cast<std::size_t>(!!contexts) + ...) == 1 && "Exactly one ctx must be non-null"
        );
        antlr4::tree::ParseTree* ctx = nullptr;
        std::ignore = (... || (contexts ? (ctx = contexts, true) : false));
        return transform(ctx);
    }
    template <typename Target = ASTNode>
        requires(std::is_base_of_v<ASTNode, Target>)
    std::vector<std::unique_ptr<Target>> transform_list(const auto& contexts) noexcept {
        auto rng = contexts | std::views::transform([this](auto ctx) {
                       return StaticUniqueCast<Target>(transform(ctx));
                   });
        return std::vector(rng.begin(), rng.end());
    }
    Location loc(const antlr4::ParserRuleContext* ctx) noexcept {
        assert(ctx != nullptr);
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        Location location;
        location.id = source_manager_.index(start->getTokenSource()->getSourceName());
        location.begin.line = start->getLine();
        location.begin.column = start->getCharPositionInLine();
        location.end.line = stop->getLine();
        location.end.column =
            stop->getCharPositionInLine() + stop->getStopIndex() - stop->getStartIndex();
        return location;
    }
    std::string_view text(const antlr4::ParserRuleContext* ctx) noexcept {
        assert(ctx != nullptr);
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        const std::string& source =
            source_manager_[source_manager_.index(start->getTokenSource()->getSourceName())];
        std::size_t begin_offset = start->getStartIndex();
        std::size_t end_offset = stop->getStopIndex() + 1;
        return std::string_view(source.data() + begin_offset, end_offset - begin_offset);
    }
    std::string_view text(const antlr4::Token* token) noexcept {
        assert(token != nullptr);
        const std::string& source =
            source_manager_[source_manager_.index(token->getTokenSource()->getSourceName())];
        std::size_t begin_offset = token->getStartIndex();
        std::size_t end_offset = token->getStopIndex() + 1;
        return std::string_view(source.data() + begin_offset, end_offset - begin_offset);
    }

public:
    ASTBuilder(const SourceManager& source_manager) noexcept : source_manager_(source_manager) {}
    std::unique_ptr<ASTCodeBlock> operator()(antlr4::tree::ParseTree* root) noexcept {
        return StaticUniqueCast<ASTCodeBlock>(transform(root));
    }
    antlrcpp::Any visitProgram(StainlessParser::ProgramContext* ctx) noexcept final {
        auto rng = ctx->statements_ |
                   std::views::transform([this](auto child) { return transform(child); });
        std::vector<std::unique_ptr<ASTNode>> nodes(rng.begin(), rng.end());
        last_visited_ = std::make_unique<ASTCodeBlock>(loc(ctx), std::move(nodes));
        return {};
    }
    antlrcpp::Any visitStatement(StainlessParser::StatementContext* ctx) noexcept final {
        last_visited_ = transform(ctx->children[0]);
        return {};
    }
    antlrcpp::Any visitCode_block(StainlessParser::Code_blockContext* ctx) noexcept final {
        auto rng = ctx->statements_ |
                   std::views::transform([this](auto child) { return transform(child); });
        std::vector<std::unique_ptr<ASTNode>> nodes(rng.begin(), rng.end());
        last_visited_ = std::make_unique<ASTCodeBlock>(loc(ctx), std::move(nodes));
        return {};
    }
    antlrcpp::Any visitExpr_statement(StainlessParser::Expr_statementContext* ctx) noexcept final {
        last_visited_ = transform(ctx->expr());
        return {};
    }
    antlrcpp::Any visitDeclaration_statement(
        StainlessParser::Declaration_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTDeclaration>(
            loc(ctx),
            StaticUniqueCast<ASTIdentifier>(transform(ctx->identifier_)),
            StaticUniqueCast<ASTTypeExpression>(transform(ctx->type_)),
            StaticUniqueCast<ASTValueExpression>(transform(ctx->value_))
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(StainlessParser::If_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTIfStatement>(
            loc(ctx),
            StaticUniqueCast<ASTExpression>(transform(ctx->condition_)),
            StaticUniqueCast<ASTCodeBlock>(transform(ctx->then_)),
            StaticUniqueCast<ASTCodeBlock>(transform(ctx->else_))
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(StainlessParser::For_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTForStatement>(
            loc(ctx),
            transform_group(ctx->init_decl_, ctx->init_expr_),
            StaticUniqueCast<ASTExpression>(transform(ctx->condition_)),
            StaticUniqueCast<ASTExpression>(transform(ctx->update_)),
            StaticUniqueCast<ASTCodeBlock>(transform(ctx->body_))
        );
        return {};
    }
    antlrcpp::Any visitBreak_statement(
        StainlessParser::Break_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTBreakStatement>(loc(ctx));
        return {};
    }
    antlrcpp::Any visitContinue_statement(
        StainlessParser::Continue_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTContinueStatement>(loc(ctx));
        return {};
    }
    antlrcpp::Any visitReturn_statement(
        StainlessParser::Return_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTReturnStatement>(
            loc(ctx), StaticUniqueCast<ASTExpression>(transform(ctx->expr_))
        );
        return {};
    }
    antlrcpp::Any visitType_alias_declaration(
        StainlessParser::Type_alias_declarationContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTTypeAlias>(
            loc(ctx),
            text(ctx->identifier_),
            StaticUniqueCast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitAssignExpr(StainlessParser::AssignExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_ADD_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAddAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_SUB_ASSIGN:
            last_visited_ =
                std::make_unique<ASTSubtractAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_MUL_ASSIGN:
            last_visited_ =
                std::make_unique<ASTMultiplyAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_DIV_ASSIGN:
            last_visited_ =
                std::make_unique<ASTDivideAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_REM_ASSIGN:
            last_visited_ =
                std::make_unique<ASTRemainderAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_AND_ASSIGN:
            last_visited_ = std::make_unique<ASTLogicalAndAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_OR_ASSIGN:
            last_visited_ =
                std::make_unique<ASTLogicalOrAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_BITAND_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseAndAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_BITOR_ASSIGN:
            last_visited_ =
                std::make_unique<ASTBitwiseOrAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_BITXOR_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseXorAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_LSHIFT_ASSIGN:
            last_visited_ =
                std::make_unique<ASTLeftShiftAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_RSHIFT_ASSIGN:
            last_visited_ = std::make_unique<ASTRightShiftAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitEqualityExpr(StainlessParser::EqualityExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_EQ:
            last_visited_ =
                std::make_unique<ASTEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_NEQ:
            last_visited_ =
                std::make_unique<ASTNotEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitRelationalExpr(StainlessParser::RelationalExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_LT:
            last_visited_ =
                std::make_unique<ASTLessThanOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_LTE:
            last_visited_ =
                std::make_unique<ASTLessEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_GT:
            last_visited_ =
                std::make_unique<ASTGreaterThanOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_GTE:
            last_visited_ =
                std::make_unique<ASTGreaterEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitShiftExpr(StainlessParser::ShiftExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_LSHIFT:
            last_visited_ =
                std::make_unique<ASTLeftShiftOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_RSHIFT:
            last_visited_ =
                std::make_unique<ASTRightShiftOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitAdditiveExpr(StainlessParser::AdditiveExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_ADD:
            last_visited_ = std::make_unique<ASTAddOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_SUB:
            last_visited_ =
                std::make_unique<ASTSubtractOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitMultiplicativeExpr(
        StainlessParser::MultiplicativeExprContext* ctx
    ) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_MUL:
            last_visited_ =
                std::make_unique<ASTMultiplyOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_DIV:
            last_visited_ =
                std::make_unique<ASTDivideOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_REM:
            last_visited_ =
                std::make_unique<ASTRemainderOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitUnaryExpr(StainlessParser::UnaryExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> expr =
            StaticUniqueCast<ASTExpression>(transform(ctx->expr_));
        switch (ctx->op_->getType()) {
        case StainlessParser::OP_INC:
            last_visited_ = std::make_unique<ASTIncrementOp>(loc(ctx), std::move(expr));
            break;
        case StainlessParser::OP_DEC:
            last_visited_ = std::make_unique<ASTDecrementOp>(loc(ctx), std::move(expr));
            break;
        case StainlessParser::OP_SUB:
            last_visited_ = std::make_unique<ASTNegateOp>(loc(ctx), std::move(expr));
            break;
        case StainlessParser::OP_NOT:
            last_visited_ = std::make_unique<ASTLogicalNotOp>(loc(ctx), std::move(expr));
            break;
        case StainlessParser::OP_BITNOT:
            last_visited_ = std::make_unique<ASTBitwiseNotOp>(loc(ctx), std::move(expr));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitConstExpr(StainlessParser::ConstExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->constant_);
        return {};
    }
    antlrcpp::Any visitCallExpr(StainlessParser::CallExprContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFunctionCall>(
            loc(ctx),
            StaticUniqueCast<ASTValueExpression>(transform(ctx->func_)),
            transform_list<ASTValueExpression>(ctx->arguments_)
        );
        return {};
    }
    antlrcpp::Any visitParenExpr(StainlessParser::ParenExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->inner_expr_);
        return {};
    }
    antlrcpp::Any visitIdentifierExpr(StainlessParser::IdentifierExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->identifier_);
        return {};
    }
    antlrcpp::Any visitIdentifier(StainlessParser::IdentifierContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTIdentifier>(loc(ctx), text(ctx->name_));
        return {};
    }
    antlrcpp::Any visitConstant(StainlessParser::ConstantContext* ctx) noexcept final {
        switch (ctx->value_->getType()) {
        case StainlessParser::T_INT:
            last_visited_ =
                std::make_unique<ASTConstant<IntegerValue>>(loc(ctx), text(ctx->value_));
            break;
        case StainlessParser::T_FLOAT:
            last_visited_ = std::make_unique<ASTConstant<FloatValue>>(loc(ctx), text(ctx->value_));
            break;
        case StainlessParser::T_STRING:
            last_visited_ = std::make_unique<ASTConstant<StringValue>>(loc(ctx), text(ctx->value_));
            break;
        case StainlessParser::T_BOOL:
            last_visited_ =
                std::make_unique<ASTConstant<BooleanValue>>(loc(ctx), text(ctx->value_));
            break;
        case StainlessParser::KW_NULL:
            last_visited_ = std::make_unique<ASTConstant<NullValue>>(loc(ctx), text(ctx->value_));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitPrimitiveType(StainlessParser::PrimitiveTypeContext* ctx) noexcept final {
        switch (ctx->primitive_->getType()) {
        case StainlessParser::KW_NULL:
            last_visited_ = std::make_unique<ASTPrimitiveType<NullType>>(loc(ctx));
            break;
        case StainlessParser::KW_INT:
            last_visited_ = std::make_unique<ASTPrimitiveType<IntegerType>>(loc(ctx));
            break;
        case StainlessParser::KW_FLOAT:
            last_visited_ = std::make_unique<ASTPrimitiveType<FloatType>>(loc(ctx));
            break;
        case StainlessParser::KW_STRING:
            last_visited_ = std::make_unique<ASTPrimitiveType<StringType>>(loc(ctx));
            break;
        case StainlessParser::KW_BOOL:
            last_visited_ = std::make_unique<ASTPrimitiveType<BooleanType>>(loc(ctx));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitParenType(StainlessParser::ParenTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->inner_type_);
        return {};
    }
    antlrcpp::Any visitIdentifierType(StainlessParser::IdentifierTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->identifier_);
        return {};
    }
    antlrcpp::Any visitRecordType(StainlessParser::RecordTypeContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTRecordType>(
            loc(ctx), transform_list<ASTFieldDeclaration>(ctx->fields_)
        );
        return {};
    }
    antlrcpp::Any visitRecord_field(StainlessParser::Record_fieldContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFieldDeclaration>(
            loc(ctx),
            text(ctx->identifier_),
            StaticUniqueCast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
};

namespace Builtins {
struct BuiltinFunction {
    const std::string_view name;
    const TypeRef type;
    const std::function<ValueRef(const Arguments&)> func;
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
// Context GetBuiltinsScope() {
//     return Context(
//         AllBuiltins | std::views::transform([](BuiltinFunction* builtin) {
//             return std::pair(std::string_view(builtin->name), builtin->type);
//         }) |
//         std::ranges::to<std::vector<std::pair<std::string_view, TypeRef>>>()
//     );
// }
}  // namespace Builtins

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
    std::unique_ptr<ASTCodeBlock> root = builder(tree);

    // Context ctx = Builtins::GetBuiltinsScope();
    Context ctx;
    root->analyze(ctx);
    TypeResolver tr(ctx);
}
