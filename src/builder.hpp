#pragma once
#include "pch.hpp"

#include "ZincBaseVisitor.h"
#include "ZincLexer.h"
#include "ZincParser.h"
#include "ast.hpp"
#include "builtins.hpp"
#include "object.hpp"
#include "source.hpp"

class ASTBuilder final : private ZincBaseVisitor {
private:
    const SourceManager::File& file_;
    ImportManager<ASTRoot>& importer_;
    std::unique_ptr<ASTNode> last_visited_;

private:
    std::shared_future<ASTRoot> import_module(std::string_view path) {
        return importer_.import(path, [this](const SourceManager::File& file) {
            return std::move(*ASTBuilder(file, importer_)());
        });
    }
    std::unique_ptr<ASTNode> transform(antlr4::tree::ParseTree* ctx) noexcept {
        if (ctx) {
            ZincBaseVisitor::visit(ctx);
            return std::move(last_visited_);
        } else {
            return nullptr;
        }
    }
    template <std::derived_from<ASTNode> T = ASTNode>
    ComparableSpan<std::unique_ptr<T>> transform_list(const auto& contexts) noexcept {
        return contexts | std::views::transform([this](auto ctx) {
                   return static_unique_cast<T>(transform(ctx));
               }) |
               GlobalMemory::collect<ComparableSpan<std::unique_ptr<T>>>();
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
    ASTBuilder(const SourceManager::File& file, ImportManager<ASTRoot>& importer) noexcept
        : file_(file), importer_(importer) {}
    std::unique_ptr<ASTRoot> operator()() noexcept {
        antlr4::ANTLRInputStream input(file_.content.data(), file_.content.size());
        ZincLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        ZincParser parser(&tokens);
        antlr4::tree::ParseTree* tree = parser.program();
        return static_unique_cast<ASTRoot>(transform(tree));
    }

private:
    antlrcpp::Any visitProgram(ZincParser::ProgramContext* ctx) noexcept final {
        ComparableSpan statements =
            ctx->statements_ |
            std::views::transform([this](auto child) { return transform(child); }) |
            GlobalMemory::collect<ComparableSpan<std::unique_ptr<ASTNode>>>();
        last_visited_ = std::make_unique<ASTRoot>(loc(ctx), std::move(statements));
        return {};
    }
    antlrcpp::Any visitTop_level_statement(
        ZincParser::Top_level_statementContext* ctx
    ) noexcept final {
        last_visited_ = transform(ctx->children[0]);
        return {};
    }
    antlrcpp::Any visitStatement(ZincParser::StatementContext* ctx) noexcept final {
        last_visited_ = transform(ctx->children[0]);
        return {};
    }
    antlrcpp::Any visitLocal_block(ZincParser::Local_blockContext* ctx) noexcept final {
        ComparableSpan statements =
            ctx->statements_ |
            std::views::transform([this](auto child) { return transform(child); }) |
            GlobalMemory::collect<ComparableSpan<std::unique_ptr<ASTNode>>>();
        last_visited_ = std::make_unique<ASTLocalBlock>(loc(ctx), statements);
        return {};
    }
    antlrcpp::Any visitExpr_statement(ZincParser::Expr_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTExpressionStatement>(
            loc(ctx), static_unique_cast<ASTExpression>(transform(ctx->expr()))
        );
        return {};
    }
    antlrcpp::Any visitDeclaration_statement(
        ZincParser::Declaration_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTDeclaration>(
            loc(ctx),
            text(ctx->identifier_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_)),
            static_unique_cast<ASTValueExpression>(transform(ctx->value_)),
            ctx->KW_MUT() != nullptr,
            false
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(ZincParser::If_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTIfStatement>(
            loc(ctx),
            static_unique_cast<ASTExpression>(transform(ctx->condition_)),
            transform_list(ctx->then_),
            transform_list(ctx->else_)
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(ZincParser::For_statementContext* ctx) noexcept final {
        if (ctx->init_decl_) {
            last_visited_ = std::make_unique<ASTForStatement>(
                loc(ctx),
                static_unique_cast<ASTDeclaration>(transform(ctx->init_decl_)),
                static_unique_cast<ASTExpression>(transform(ctx->condition_)),
                static_unique_cast<ASTExpression>(transform(ctx->update_)),
                transform_list(ctx->body_)
            );
        } else if (ctx->init_expr_) {
            last_visited_ = std::make_unique<ASTForStatement>(
                loc(ctx),
                static_unique_cast<ASTValueExpression>(transform(ctx->init_expr_)),
                static_unique_cast<ASTExpression>(transform(ctx->condition_)),
                static_unique_cast<ASTExpression>(transform(ctx->update_)),
                transform_list(ctx->body_)
            );
        } else {
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitBreak_statement(ZincParser::Break_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTBreakStatement>(loc(ctx));
        return {};
    }
    antlrcpp::Any visitContinue_statement(
        ZincParser::Continue_statementContext* ctx
    ) noexcept final {
        last_visited_ = std::make_unique<ASTContinueStatement>(loc(ctx));
        return {};
    }
    antlrcpp::Any visitReturn_statement(ZincParser::Return_statementContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTReturnStatement>(
            loc(ctx), static_unique_cast<ASTExpression>(transform(ctx->expr_))
        );
        return {};
    }
    antlrcpp::Any visitType_alias(ZincParser::Type_aliasContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTTypeAlias>(
            loc(ctx),
            text(ctx->identifier_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitFunction_definition(
        ZincParser::Function_definitionContext* ctx
    ) noexcept final {
        ComparableSpan<std::unique_ptr<ASTFunctionParameter>> parameters =
            transform_list<ASTFunctionParameter>(ctx->parameters_);
        std::string_view identifier = text(ctx->identifier_);
        std::unique_ptr return_type =
            static_unique_cast<ASTTypeExpression>(transform(ctx->return_type_));
        ComparableSpan<std::unique_ptr<ASTNode>> body = transform_list(ctx->body_);
        last_visited_ = std::make_unique<ASTFunctionDefinition>(
            loc(ctx),
            identifier,
            parameters,
            std::move(return_type),
            body,
            ctx->KW_CONST() != nullptr,
            ctx->KW_STATIC() != nullptr
        );
        return {};
    }
    antlrcpp::Any visitParameter(ZincParser::ParameterContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFunctionParameter>(
            loc(ctx),
            text(ctx->identifier_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitClass_definition(ZincParser::Class_definitionContext* ctx) noexcept final {
        ComparableSpan<std::string_view> implements =
            ctx->implements_ | std::views::transform([this](auto child) { return text(child); }) |
            GlobalMemory::collect<ComparableSpan<std::string_view>>();
        ComparableSpan<std::unique_ptr<ASTFieldDeclaration>> fields =
            transform_list<ASTFieldDeclaration>(ctx->fields_);
        ComparableSpan<std::unique_ptr<ASTTypeAlias>> types =
            transform_list<ASTTypeAlias>(ctx->types_);
        ComparableSpan<std::unique_ptr<ASTFunctionDefinition>> functions =
            transform_list<ASTFunctionDefinition>(ctx->functions_);
        ComparableSpan<std::unique_ptr<ASTClassDefinition>> classes =
            transform_list<ASTClassDefinition>(ctx->classes_);
        last_visited_ = std::make_unique<ASTClassDefinition>(
            loc(ctx),
            text(ctx->identifier_),
            ctx->extends_ ? text(ctx->extends_) : "",
            implements,
            fields,
            types,
            functions,
            classes
        );
        return {};
    }
    antlrcpp::Any visitNamespace_definition(
        ZincParser::Namespace_definitionContext* ctx
    ) noexcept final {
        ComparableSpan<std::unique_ptr<ASTNode>> items =
            ctx->items_ | std::views::transform([this](auto child) { return transform(child); }) |
            GlobalMemory::collect<ComparableSpan<std::unique_ptr<ASTNode>>>();
        last_visited_ = std::make_unique<ASTNamespaceDefinition>(
            loc(ctx), ctx->identifier_ ? text(ctx->identifier_) : "", items
        );
        return {};
    }
    antlrcpp::Any visitAssignExpr(ZincParser::AssignExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_ADD_ASSIGN:
            last_visited_ =
                std::make_unique<ASTAddAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_SUB_ASSIGN:
            last_visited_ =
                std::make_unique<ASTSubtractAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_MUL_ASSIGN:
            last_visited_ =
                std::make_unique<ASTMultiplyAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_DIV_ASSIGN:
            last_visited_ =
                std::make_unique<ASTDivideAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_REM_ASSIGN:
            last_visited_ =
                std::make_unique<ASTRemainderAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_AND_ASSIGN:
            last_visited_ = std::make_unique<ASTLogicalAndAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case ZincParser::OP_OR_ASSIGN:
            last_visited_ =
                std::make_unique<ASTLogicalOrAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_BITAND_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseAndAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case ZincParser::OP_BITOR_ASSIGN:
            last_visited_ =
                std::make_unique<ASTBitwiseOrAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_BITXOR_ASSIGN:
            last_visited_ = std::make_unique<ASTBitwiseXorAssignOp>(
                loc(ctx), std::move(left), std::move(right)
            );
            break;
        case ZincParser::OP_LSHIFT_ASSIGN:
            last_visited_ =
                std::make_unique<ASTLeftShiftAssignOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_RSHIFT_ASSIGN:
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
    antlrcpp::Any visitEqualityExpr(ZincParser::EqualityExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_EQ:
            last_visited_ =
                std::make_unique<ASTEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_NEQ:
            last_visited_ =
                std::make_unique<ASTNotEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitRelationalExpr(ZincParser::RelationalExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:
            last_visited_ =
                std::make_unique<ASTLessThanOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_LTE:
            last_visited_ =
                std::make_unique<ASTLessEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_GT:
            last_visited_ =
                std::make_unique<ASTGreaterThanOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_GTE:
            last_visited_ =
                std::make_unique<ASTGreaterEqualOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitShiftExpr(ZincParser::ShiftExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LSHIFT:
            last_visited_ =
                std::make_unique<ASTLeftShiftOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_RSHIFT:
            last_visited_ =
                std::make_unique<ASTRightShiftOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitAdditiveExpr(ZincParser::AdditiveExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ADD:
            last_visited_ = std::make_unique<ASTAddOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_SUB:
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
        ZincParser::MultiplicativeExprContext* ctx
    ) noexcept final {
        std::unique_ptr<ASTExpression> left =
            static_unique_cast<ASTExpression>(transform(ctx->left_));
        std::unique_ptr<ASTExpression> right =
            static_unique_cast<ASTExpression>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_MUL:
            last_visited_ =
                std::make_unique<ASTMultiplyOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_DIV:
            last_visited_ =
                std::make_unique<ASTDivideOp>(loc(ctx), std::move(left), std::move(right));
            break;
        case ZincParser::OP_REM:
            last_visited_ =
                std::make_unique<ASTRemainderOp>(loc(ctx), std::move(left), std::move(right));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitUnaryExpr(ZincParser::UnaryExprContext* ctx) noexcept final {
        std::unique_ptr<ASTExpression> expr =
            static_unique_cast<ASTExpression>(transform(ctx->expr_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            last_visited_ = std::make_unique<ASTIncrementOp>(loc(ctx), std::move(expr));
            break;
        case ZincParser::OP_DEC:
            last_visited_ = std::make_unique<ASTDecrementOp>(loc(ctx), std::move(expr));
            break;
        case ZincParser::OP_SUB:
            last_visited_ = std::make_unique<ASTNegateOp>(loc(ctx), std::move(expr));
            break;
        case ZincParser::OP_NOT:
            last_visited_ = std::make_unique<ASTLogicalNotOp>(loc(ctx), std::move(expr));
            break;
        case ZincParser::OP_BITNOT:
            last_visited_ = std::make_unique<ASTBitwiseNotOp>(loc(ctx), std::move(expr));
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitConstExpr(ZincParser::ConstExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->constant_);
        return {};
    }
    antlrcpp::Any visitCallExpr(ZincParser::CallExprContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFunctionCall>(
            loc(ctx),
            static_unique_cast<ASTValueExpression>(transform(ctx->func_)),
            transform_list<ASTValueExpression>(ctx->arguments_)
        );
        return {};
    }
    antlrcpp::Any visitParenExpr(ZincParser::ParenExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->inner_expr_);
        return {};
    }
    antlrcpp::Any visitIdentifierExpr(ZincParser::IdentifierExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->identifier_);
        return {};
    }
    antlrcpp::Any visitIdentifier(ZincParser::IdentifierContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTIdentifier>(loc(ctx), text(ctx->name_));
        return {};
    }
    antlrcpp::Any visitConstant(ZincParser::ConstantContext* ctx) noexcept final {
        switch (ctx->value_->getType()) {
        case ZincParser::T_INT:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<IntegerValue>{}
            );
            break;
        case ZincParser::T_FLOAT:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<FloatValue>{}
            );
            break;
        case ZincParser::T_STRING:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<ArrayValue>{}
            );
            break;
        case ZincParser::T_BOOL:
            last_visited_ = std::make_unique<ASTConstant>(
                loc(ctx), text(ctx->value_), std::type_identity<BooleanValue>{}
            );
            break;
        case ZincParser::KW_NULL:
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
    antlrcpp::Any visitPrimitiveType(ZincParser::PrimitiveTypeContext* ctx) noexcept final {
        switch (ctx->primitive_->getType()) {
        case ZincParser::KW_NULL:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &NullType::instance);
            break;
        case ZincParser::KW_INT8:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i8_instance);
            break;
        case ZincParser::KW_INT16:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i16_instance);
            break;
        case ZincParser::KW_INT32:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i32_instance);
            break;
        case ZincParser::KW_INT64:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::i64_instance);
            break;
        case ZincParser::KW_UINT8:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u8_instance);
            break;
        case ZincParser::KW_UINT16:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u16_instance);
            break;
        case ZincParser::KW_UINT32:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u32_instance);
            break;
        case ZincParser::KW_UINT64:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), &IntegerType::u64_instance);
            break;
        case ZincParser::KW_FLOAT32:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &FloatType::f32_instance);
            break;
        case ZincParser::KW_FLOAT64:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &FloatType::f64_instance);
            break;
        case ZincParser::KW_STRING:
            /// TODO:
            last_visited_ =
                std::make_unique<ASTPrimitiveType>(loc(ctx), TypeRegistry::get<StringType>());
            break;
        case ZincParser::KW_BOOL:
            last_visited_ = std::make_unique<ASTPrimitiveType>(loc(ctx), &BooleanType::instance);
            break;
        default:
            assert(false);
            std::unreachable();
        }
        return {};
    }
    antlrcpp::Any visitParenType(ZincParser::ParenTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->inner_type_);
        return {};
    }
    antlrcpp::Any visitIdentifierType(ZincParser::IdentifierTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->identifier_);
        return {};
    }
    antlrcpp::Any visitRecordType(ZincParser::RecordTypeContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTRecordType>(
            loc(ctx), transform_list<ASTFieldDeclaration>(ctx->fields_)
        );
        return {};
    }
    antlrcpp::Any visitFunctionType(ZincParser::FunctionTypeContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFunctionType>(
            loc(ctx),
            transform_list<ASTTypeExpression>(ctx->parameters_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->return_type_))
        );
        return {};
    }
    antlrcpp::Any visitField_declaration(ZincParser::Field_declarationContext* ctx) noexcept final {
        last_visited_ = std::make_unique<ASTFieldDeclaration>(
            loc(ctx),
            text(ctx->identifier_),
            static_unique_cast<ASTTypeExpression>(transform(ctx->type_))
        );
        return {};
    }
};
