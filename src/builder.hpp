#pragma once
#include "pch.hpp"
#include <type_traits>

#include "StainlessBaseVisitor.h"
#include "StainlessLexer.h"
#include "StainlessParser.h"
#include "ast.hpp"
#include "object.hpp"

using ImportResult = std::unique_ptr<ASTRoot>;
using ImportFuture = std::shared_future<ImportResult>;

class ASTBuilder final : private StainlessBaseVisitor {
private:
    const SourceManager::File& file_;
    const std::function<ImportFuture(std::string_view)> import_callback_;
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
    template <typename Target = ASTNode>
        requires(std::is_base_of_v<ASTNode, Target>)
    std::vector<std::unique_ptr<Target>> transform_list(const auto& contexts) noexcept {
        auto rng = contexts | std::views::transform([this](auto ctx) {
                       return static_unique_cast<Target>(transform(ctx));
                   });
        return std::vector(rng.begin(), rng.end());
    }
    Location loc(const antlr4::ParserRuleContext* ctx) noexcept {
        assert(ctx != nullptr);
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        Location location{
            .id = file_.id,
            .begin = static_cast<std::uint32_t>(start->getStartIndex()),
            .end = static_cast<std::uint32_t>(stop->getStopIndex() + 1)
        };
        return location;
    }
    std::string_view text(const antlr4::ParserRuleContext* ctx) noexcept {
        assert(ctx != nullptr);
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        std::size_t begin_offset = start->getStartIndex();
        std::size_t end_offset = stop->getStopIndex() + 1;
        return std::string_view(file_.content.data() + begin_offset, end_offset - begin_offset);
    }
    std::string_view text(const antlr4::Token* token) noexcept {
        assert(token != nullptr);
        std::size_t begin_offset = token->getStartIndex();
        std::size_t end_offset = token->getStopIndex() + 1;
        return std::string_view(file_.content.data() + begin_offset, end_offset - begin_offset);
    }

public:
    ASTBuilder(const SourceManager::File& file, decltype(import_callback_) import_callback) noexcept
        : file_(file), import_callback_(std::move(import_callback)) {}
    std::unique_ptr<ASTRoot> operator()(antlr4::tree::ParseTree* root) noexcept {
        return static_unique_cast<ASTRoot>(transform(root));
    }

private:
    antlrcpp::Any visitProgram(StainlessParser::ProgramContext* ctx) noexcept final {
        auto rng = ctx->statements_ |
                   std::views::transform([this](auto child) { return transform(child); });
        std::vector<std::unique_ptr<ASTNode>> nodes(rng.begin(), rng.end());
        last_visited_ = std::make_unique<ASTRoot>(loc(ctx), std::move(nodes));
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
            static_unique_cast<ASTIdentifier>(transform(ctx->identifier_)),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_)),
            static_unique_cast<ASTValueExpression>(transform(ctx->value_)),
            ctx->KW_MUT() != nullptr,
            false
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(StainlessParser::If_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTIfStatement>(
            loc(ctx),
            static_unique_cast<ASTExpression>(transform(ctx->condition_)),
            static_unique_cast<ASTCodeBlock>(transform(ctx->then_)),
            static_unique_cast<ASTCodeBlock>(transform(ctx->else_))
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(StainlessParser::For_statementContext* ctx) noexcept final {
        if (ctx->init_decl_) {
            last_visited_ = std::make_unique<ASTForStatement>(
                loc(ctx),
                static_unique_cast<ASTDeclaration>(transform(ctx->init_decl_)),
                static_unique_cast<ASTExpression>(transform(ctx->condition_)),
                static_unique_cast<ASTExpression>(transform(ctx->update_)),
                static_unique_cast<ASTCodeBlock>(transform(ctx->body_))
            );
        } else if (ctx->init_expr_) {
            last_visited_ = std::make_unique<ASTForStatement>(
                loc(ctx),
                static_unique_cast<ASTValueExpression>(transform(ctx->init_expr_)),
                static_unique_cast<ASTExpression>(transform(ctx->condition_)),
                static_unique_cast<ASTExpression>(transform(ctx->update_)),
                static_unique_cast<ASTCodeBlock>(transform(ctx->body_))
            );
        } else {
            assert(false);
            std::unreachable();
        }
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
            loc(ctx), static_unique_cast<ASTExpression>(transform(ctx->expr_))
        );
        return {};
    }
    antlrcpp::Any visitType_alias_declaration(
        StainlessParser::Type_alias_declarationContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTTypeAlias>(
            loc(ctx),
            text(ctx->identifier_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitAssignExpr(StainlessParser::AssignExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
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
            static_unique_cast<ASTExpression>(transform(ctx->expr_));
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
            static_unique_cast<ASTValueExpression>(transform(ctx->func_)),
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
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<IntegerValue>{}
            );
            break;
        case StainlessParser::T_FLOAT:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<FloatValue>{}
            );
            break;
        case StainlessParser::T_STRING:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<StringValue>{}
            );
            break;
        case StainlessParser::T_BOOL:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<BooleanValue>{}
            );
            break;
        case StainlessParser::KW_NULL:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<NullValue>{}
            );
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
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &NullType::instance);
            break;
        case StainlessParser::KW_INT8:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i8_instance);
            break;
        case StainlessParser::KW_INT16:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i16_instance);
            break;
        case StainlessParser::KW_INT32:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i32_instance);
            break;
        case StainlessParser::KW_INT64:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i64_instance);
            break;
        case StainlessParser::KW_UINT8:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u8_instance);
            break;
        case StainlessParser::KW_UINT16:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u16_instance);
            break;
        case StainlessParser::KW_UINT32:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u32_instance);
            break;
        case StainlessParser::KW_UINT64:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u64_instance);
            break;
        case StainlessParser::KW_FLOAT32:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &FloatType::f32_instance);
            break;
        case StainlessParser::KW_FLOAT64:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &FloatType::f64_instance);
            break;
        case StainlessParser::KW_STRING:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &StringType::instance);
            break;
        case StainlessParser::KW_BOOL:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &BooleanType::instance);
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
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
};

class ImportManager {
private:
    SourceManager& sources_;
    std::mutex import_mutex_;
    FlatMap<std::string_view, std::shared_future<ImportResult>> import_map_;

private:
    ImportResult import_worker(SourceManager& sources, std::string_view path) {
        auto file_id = sources.load(path);
        if (!file_id) {
            return nullptr;
        }

        const auto& file = sources[*file_id];
        antlr4::ANTLRInputStream stream(file.content);
        StainlessLexer lexer(&stream);
        antlr4::CommonTokenStream tokens(&lexer);
        StainlessParser parser(&tokens);

        StainlessParser::ProgramContext* tree = parser.program();
        ASTBuilder builder(file, [this](std::string_view import_path) {
            return this->import(import_path);
        });
        return builder(tree);
    }

public:
    ImportManager(SourceManager& sources) noexcept : sources_(sources) {}

    ImportFuture import(std::string_view path) {
        std::lock_guard lock(import_mutex_);
        if (auto it = import_map_.find(path); it != import_map_.end()) {
            return it->second;
        } else {
            auto import_future = std::async(
                                     std::launch::async,
                                     &ImportManager::import_worker,
                                     this,
                                     std::ref(sources_),
                                     path
            )
                                     .share();
            import_map_.insert(path, import_future);
            return import_map_.at(path);
        }
    }
};
