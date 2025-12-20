#include <string_view>

#include "StainlessBaseVisitor.h"
#include "StainlessLexer.h"
#include "StainlessParser.h"
#include "ast.hpp"
#include "object.hpp"
#include "pch.hpp"

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
            (static_cast<std::size_t>(!!contexts) + ...) == 1 &&
            "Exactly one context must be non-null"
        );
        antlr4::tree::ParseTree* context;
        std::ignore = (... || (contexts ? (context = contexts, true) : false));
        return transform(context);
    }
    template <typename Target = ASTNode>
        requires(std::is_base_of_v<ASTNode, Target>)
    std::vector<std::unique_ptr<Target>> transform_list(const auto& contexts) noexcept {
        return contexts | std::views::transform([this](auto ctx) {
                   return StaticUniqueCast<Target>(transform(ctx));
               }) |
               std::ranges::to<std::vector>();
    }
    Location loc(const antlr4::ParserRuleContext* context) noexcept {
        assert(context != nullptr);
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
    std::string_view text(const antlr4::ParserRuleContext* context) noexcept {
        assert(context != nullptr);
        auto start = context->getStart();
        auto stop = context->getStop();
        const std::string& source =
            source_manager_[source_manager_.index(start->getTokenSource()->getSourceName())];
        std::size_t begin_offset = start->getStartIndex();
        std::size_t end_offset = stop->getStopIndex() + 1;
        return std::string_view(source.data() + begin_offset, end_offset - begin_offset);
    }

public:
    ASTBuilder(const SourceManager& source_manager) noexcept : source_manager_(source_manager) {}
    std::unique_ptr<ASTCodeBlock> operator()(antlr4::tree::ParseTree* root) noexcept {
        return StaticUniqueCast<ASTCodeBlock>(transform(root));
    }
    antlrcpp::Any visitProgram(StainlessParser::ProgramContext* context) noexcept final {
        std::vector<std::unique_ptr<ASTNode>> nodes =
            context->statements_ |
            std::views::transform([this](auto ctx) { return transform(ctx); }) |
            std::ranges::to<std::vector>();
        last_visited_ = std::make_unique<ASTCodeBlock>(loc(context), std::move(nodes));
        return {};
    }
    antlrcpp::Any visitStatement(StainlessParser::StatementContext* context) noexcept final {
        last_visited_ = transform(context->children[0]);
        return {};
    }
    antlrcpp::Any visitCode_block(StainlessParser::Code_blockContext* context) noexcept final {
        std::vector<std::unique_ptr<ASTNode>> nodes =
            context->statements_ |
            std::views::transform([this](auto ctx) { return transform(ctx); }) |
            std::ranges::to<std::vector>();
        last_visited_ = std::make_unique<ASTCodeBlock>(loc(context), std::move(nodes));
        return {};
    }
    antlrcpp::Any visitExpr_statement(
        StainlessParser::Expr_statementContext* context
    ) noexcept final {
        last_visited_ = transform(context->expr());
        return {};
    }
    antlrcpp::Any visitDeclaration_statement(
        StainlessParser::Declaration_statementContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTDeclaration>(
            loc(context),
            StaticUniqueCast<ASTIdentifier>(transform(context->identifier_)),
            StaticUniqueCast<ASTTypeExpression>(transform(context->type_)),
            StaticUniqueCast<ASTValueExpression>(transform(context->value_))
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(StainlessParser::If_statementContext* context) noexcept final {
        last_visited_ = std::make_unique<ASTIfStatement>(
            loc(context),
            StaticUniqueCast<ASTExpression>(transform(context->condition_)),
            StaticUniqueCast<ASTCodeBlock>(transform(context->then_)),
            StaticUniqueCast<ASTCodeBlock>(transform(context->else_))
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(
        StainlessParser::For_statementContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTForStatement>(
            loc(context),
            transform_group(context->init_decl_, context->init_expr_),
            StaticUniqueCast<ASTExpression>(transform(context->condition_)),
            StaticUniqueCast<ASTExpression>(transform(context->update_)),
            StaticUniqueCast<ASTCodeBlock>(transform(context->body_))
        );
        return {};
    }
    antlrcpp::Any visitBreak_statement(
        StainlessParser::Break_statementContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTBreakStatement>(loc(context));
        return {};
    }
    antlrcpp::Any visitContinue_statement(
        StainlessParser::Continue_statementContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTContinueStatement>(loc(context));
        return {};
    }
    antlrcpp::Any visitReturn_statement(
        StainlessParser::Return_statementContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTReturnStatement>(
            loc(context), StaticUniqueCast<ASTExpression>(transform(context->expr_))
        );
        return {};
    }
    antlrcpp::Any visitType_alias_declaration(
        StainlessParser::Type_alias_declarationContext* context
    ) noexcept final {
        last_visited_ = std::make_unique<ASTTypeAlias>(
            loc(context),
            context->identifier_->getText(),
            StaticUniqueCast<ASTTypeExpression>(transform(context->type_))
        );
        return {};
    }
    antlrcpp::Any visitAssignExpr(StainlessParser::AssignExprContext* context) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAssignOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_ADD_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAddAssignOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_SUB_ASSIGN:
            last_visited_ = std::make_unique<ASTSubtractAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_MUL_ASSIGN:
            last_visited_ = std::make_unique<ASTMultiplyAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_DIV_ASSIGN:
            last_visited_ = std::make_unique<ASTDivideAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_REM_ASSIGN:
            last_visited_ = std::make_unique<ASTRemainderAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_AND_ASSIGN:
            last_visited_ = std::make_unique<ASTLogicalAndAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_OR_ASSIGN:
            last_visited_ = std::make_unique<ASTLogicalOrAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_BITAND_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseAndAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_BITOR_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseOrAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_BITXOR_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseXorAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_LSHIFT_ASSIGN:
            last_visited_ = std::make_unique<ASTLeftShiftAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        case StainlessParser::OP_RSHIFT_ASSIGN:
            last_visited_ = std::make_unique<ASTRightShiftAssignOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitEqualityExpr(StainlessParser::EqualityExprContext* context) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_EQ:
            last_visited_ =
                std::make_unique<ASTEqualOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_NEQ:
            last_visited_ =
                std::make_unique<ASTNotEqualOp>(loc(context), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitRelationalExpr(
        StainlessParser::RelationalExprContext* context
    ) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_LT:
            last_visited_ =
                std::make_unique<ASTLessThanOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_LTE:
            last_visited_ =
                std::make_unique<ASTLessEqualOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_GT:
            last_visited_ =
                std::make_unique<ASTGreaterThanOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_GTE:
            last_visited_ = std::make_unique<ASTGreaterEqualOp>(
                loc(context), std::move(left), std::move(right)
            );
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitShiftExpr(StainlessParser::ShiftExprContext* context) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_LSHIFT:
            last_visited_ =
                std::make_unique<ASTLeftShiftOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_RSHIFT:
            last_visited_ =
                std::make_unique<ASTRightShiftOp>(loc(context), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitAdditiveExpr(StainlessParser::AdditiveExprContext* context) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_ADD:
            last_visited_ =
                std::make_unique<ASTAddOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_SUB:
            last_visited_ =
                std::make_unique<ASTSubtractOp>(loc(context), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitMultiplicativeExpr(
        StainlessParser::MultiplicativeExprContext* context
    ) noexcept final {
        std::unique_ptr<ASTExpression> left =
            StaticUniqueCast<ASTExpression>(transform(context->left_));
        std::unique_ptr<ASTExpression> right =
            StaticUniqueCast<ASTExpression>(transform(context->right_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_MUL:
            last_visited_ =
                std::make_unique<ASTMultiplyOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_DIV:
            last_visited_ =
                std::make_unique<ASTDivideOp>(loc(context), std::move(left), std::move(right));
            break;
        case StainlessParser::OP_REM:
            last_visited_ =
                std::make_unique<ASTRemainderOp>(loc(context), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitUnaryExpr(StainlessParser::UnaryExprContext* context) noexcept final {
        std::unique_ptr<ASTExpression> expr =
            StaticUniqueCast<ASTExpression>(transform(context->expr_));
        switch (context->op_->getType()) {
        case StainlessParser::OP_INC:
            last_visited_ = std::make_unique<ASTIncrementOp>(loc(context), std::move(expr));
            break;
        case StainlessParser::OP_DEC:
            last_visited_ = std::make_unique<ASTDecrementOp>(loc(context), std::move(expr));
            break;
        case StainlessParser::OP_SUB:
            last_visited_ = std::make_unique<ASTNegateOp>(loc(context), std::move(expr));
            break;
        case StainlessParser::OP_NOT:
            last_visited_ = std::make_unique<ASTLogicalNotOp>(loc(context), std::move(expr));
            break;
        case StainlessParser::OP_BITNOT:
            last_visited_ = std::make_unique<ASTBitwiseNotOp>(loc(context), std::move(expr));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitConstExpr(StainlessParser::ConstExprContext* context) noexcept final {
        last_visited_ = transform(context->constant_);
        return {};
    }
    antlrcpp::Any visitCallExpr(StainlessParser::CallExprContext* context) noexcept final {
        last_visited_ = std::make_unique<ASTFunctionCall>(
            loc(context),
            StaticUniqueCast<ASTValueExpression>(transform(context->func_)),
            transform_list<ASTValueExpression>(context->arguments_)
        );
        return {};
    }
    antlrcpp::Any visitParenExpr(StainlessParser::ParenExprContext* context) noexcept final {
        last_visited_ = transform(context->inner_expr_);
        return {};
    }
    antlrcpp::Any visitIdentifierExpr(
        StainlessParser::IdentifierExprContext* context
    ) noexcept final {
        last_visited_ = transform(context->identifier_);
        return {};
    }
    antlrcpp::Any visitIdentifier(StainlessParser::IdentifierContext* context) noexcept final {
        last_visited_ = std::make_unique<ASTIdentifier>(loc(context), context->name_->getText());
        return {};
    }
    antlrcpp::Any visitConstant(StainlessParser::ConstantContext* context) noexcept final {
        switch (context->value_->getType()) {
        case StainlessParser::T_INT:
            last_visited_ = std::make_unique<ASTConstant<IntegerValue>>(
                loc(context), context->value_->getText()
            );
            break;
        case StainlessParser::T_FLOAT:
            last_visited_ =
                std::make_unique<ASTConstant<FloatValue>>(loc(context), context->value_->getText());
            break;
        case StainlessParser::T_STRING:
            last_visited_ = std::make_unique<ASTConstant<StringValue>>(
                loc(context), context->value_->getText()
            );
            break;
        case StainlessParser::T_BOOL:
            last_visited_ = std::make_unique<ASTConstant<BooleanValue>>(
                loc(context), context->value_->getText()
            );
            break;
        case StainlessParser::KW_NULL:
            last_visited_ =
                std::make_unique<ASTConstant<NullValue>>(loc(context), context->value_->getText());
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitPrimitiveType(
        StainlessParser::PrimitiveTypeContext* context
    ) noexcept final {
        switch (context->primitive_->getType()) {
        case StainlessParser::KW_NULL:
            last_visited_ = std::make_unique<ASTPrimitiveType<NullType>>(loc(context));
            break;
        case StainlessParser::KW_INT:
            last_visited_ = std::make_unique<ASTPrimitiveType<IntegerType>>(loc(context));
            break;
        case StainlessParser::KW_FLOAT:
            last_visited_ = std::make_unique<ASTPrimitiveType<FloatType>>(loc(context));
            break;
        case StainlessParser::KW_STRING:
            last_visited_ = std::make_unique<ASTPrimitiveType<StringType>>(loc(context));
            break;
        case StainlessParser::KW_BOOL:
            last_visited_ = std::make_unique<ASTPrimitiveType<BooleanType>>(loc(context));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitParenType(StainlessParser::ParenTypeContext* context) noexcept final {
        last_visited_ = transform(context->inner_type_);
        return {};
    }
    antlrcpp::Any visitIdentifierType(
        StainlessParser::IdentifierTypeContext* context
    ) noexcept final {
        last_visited_ = transform(context->identifier_);
        return {};
    }
    antlrcpp::Any visitRecordType(StainlessParser::RecordTypeContext* context) noexcept final {
        last_visited_ = std::make_unique<ASTRecordType>(
            loc(context), transform_list<ASTFieldDeclaration>(context->fields_)
        );
        return {};
    }
    antlrcpp::Any visitRecord_field(StainlessParser::Record_fieldContext* context) noexcept final {
        last_visited_ = std::make_unique<ASTFieldDeclaration>(
            loc(context),
            context->identifier_->getText(),
            StaticUniqueCast<ASTTypeExpression>(transform(context->type_))
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
