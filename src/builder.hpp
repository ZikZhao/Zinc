#pragma once
#include "pch.hpp"

#include "ZincBaseVisitor.h"
#include "ZincLexer.h"
#include "ZincParser.h"
#include "antlr4-runtime.h"
#include "ast.hpp"
#include "builtins.hpp"
#include "object.hpp"
#include "source.hpp"

class ErrorTracker : public antlr4::BaseErrorListener {
public:
    bool has_error = false;

public:
    virtual void syntaxError(
        antlr4::Recognizer* recognizer,
        antlr4::Token* offendingSymbol,
        size_t line,
        size_t charPositionInLine,
        const std::string& msg,
        std::exception_ptr e
    ) override {
        has_error = true;
    }
};

class ASTBuilder final : private ZincBaseVisitor {
private:
    const SourceManager::File& file_;
    ImportManager<ASTRoot>& importer_;
    ASTNode* last_visited_;

private:
    std::shared_future<ASTRoot> import_module(std::string_view path) {
        return importer_.import(path, [this](const SourceManager::File& file) {
            return std::move(*ASTBuilder(file, importer_)());
        });
    }
    ASTNode* transform(antlr4::tree::ParseTree* ctx) noexcept {
        if (ctx) {
            ZincBaseVisitor::visit(ctx);
            return last_visited_;
        } else {
            return nullptr;
        }
    }
    template <std::derived_from<ASTNode> T = ASTNode>
    ComparableSpan<T*> transform_list(const auto& contexts) noexcept {
        return contexts |
               std::views::transform([this](auto ctx) { return static_cast<T*>(transform(ctx)); }) |
               GlobalMemory::collect<ComparableSpan<T*>>();
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
    ASTRoot* operator()() noexcept {
        antlr4::ANTLRInputStream input(file_.content.data(), file_.content.size());
        ZincLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        ZincParser parser(&tokens);
        ErrorTracker tracker;
        lexer.addErrorListener(&tracker);
        parser.addErrorListener(&tracker);
        antlr4::tree::ParseTree* tree = parser.program();
        if (tracker.has_error) {
            return nullptr;
        }
        return static_cast<ASTRoot*>(transform(tree));
    }

private:
    antlrcpp::Any visitProgram(ZincParser::ProgramContext* ctx) noexcept final {
        ComparableSpan statements =
            ctx->statements_ |
            std::views::transform([this](auto child) { return transform(child); }) |
            GlobalMemory::collect<ComparableSpan<ASTNode*>>();
        last_visited_ = new ASTRoot(loc(ctx), std::move(statements));
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
            GlobalMemory::collect<ComparableSpan<ASTNode*>>();
        last_visited_ = new ASTLocalBlock(loc(ctx), statements);
        return {};
    }
    antlrcpp::Any visitExpr_statement(ZincParser::Expr_statementContext* ctx) noexcept final {
        last_visited_ = new ASTExpressionStatement(
            loc(ctx), static_cast<ASTExpression*>(transform(ctx->expr()))
        );
        return {};
    }
    antlrcpp::Any visitLetDecl(ZincParser::LetDeclContext* ctx) noexcept final {
        last_visited_ = new ASTDeclaration(
            loc(ctx),
            text(ctx->identifier_),
            static_cast<ASTExpression*>(transform(ctx->type_)),
            static_cast<ASTExpression*>(transform(ctx->value_)),
            ctx->KW_MUT() != nullptr,
            false
        );
        return {};
    }
    antlrcpp::Any visitConstDecl(ZincParser::ConstDeclContext* ctx) noexcept final {
        last_visited_ = new ASTDeclaration(
            loc(ctx),
            text(ctx->identifier_),
            static_cast<ASTExpression*>(transform(ctx->type_)),
            static_cast<ASTExpression*>(transform(ctx->value_)),
            false,
            true
        );
        return {};
    }
    antlrcpp::Any visitIf_statement(ZincParser::If_statementContext* ctx) noexcept final {
        last_visited_ = new ASTIfStatement(
            loc(ctx),
            static_cast<ASTExpression*>(transform(ctx->condition_)),
            transform_list(ctx->then_),
            transform_list(ctx->else_)
        );
        return {};
    }
    antlrcpp::Any visitFor_statement(ZincParser::For_statementContext* ctx) noexcept final {
        if (ctx->init_decl_) {
            last_visited_ = new ASTForStatement(
                loc(ctx),
                static_cast<ASTDeclaration*>(transform(ctx->init_decl_)),
                static_cast<ASTExpression*>(transform(ctx->condition_)),
                static_cast<ASTExpression*>(transform(ctx->update_)),
                transform_list(ctx->body_)
            );
        } else if (ctx->init_expr_) {
            last_visited_ = new ASTForStatement(
                loc(ctx),
                static_cast<ASTExpression*>(transform(ctx->init_expr_)),
                static_cast<ASTExpression*>(transform(ctx->condition_)),
                static_cast<ASTExpression*>(transform(ctx->update_)),
                transform_list(ctx->body_)
            );
        } else {
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitBreak_statement(ZincParser::Break_statementContext* ctx) noexcept final {
        last_visited_ = new ASTBreakStatement(loc(ctx));
        return {};
    }
    antlrcpp::Any visitContinue_statement(
        ZincParser::Continue_statementContext* ctx
    ) noexcept final {
        last_visited_ = new ASTContinueStatement(loc(ctx));
        return {};
    }
    antlrcpp::Any visitReturn_statement(ZincParser::Return_statementContext* ctx) noexcept final {
        last_visited_ =
            new ASTReturnStatement(loc(ctx), static_cast<ASTExpression*>(transform(ctx->expr_)));
        return {};
    }
    antlrcpp::Any visitType_alias(ZincParser::Type_aliasContext* ctx) noexcept final {
        last_visited_ = new ASTTypeAlias(
            loc(ctx), text(ctx->identifier_), static_cast<ASTExpression*>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitFunction_definition(
        ZincParser::Function_definitionContext* ctx
    ) noexcept final {
        ComparableSpan<ASTFunctionParameter*> parameters =
            transform_list<ASTFunctionParameter>(ctx->parameters_);
        std::string_view identifier = text(ctx->identifier_);
        ASTExpression* return_type = static_cast<ASTExpression*>(transform(ctx->return_type_));
        ComparableSpan<ASTNode*> body = transform_list(ctx->body_);
        last_visited_ = new ASTFunctionDefinition(
            loc(ctx),
            identifier,
            parameters,
            return_type,
            body,
            ctx->KW_CONST() != nullptr,
            ctx->KW_STATIC() != nullptr
        );
        return {};
    }
    antlrcpp::Any visitSelfParam(ZincParser::SelfParamContext* ctx) noexcept final {
        last_visited_ = new ASTFunctionParameter(
            loc(ctx), "self", static_cast<ASTExpression*>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitNormalParam(ZincParser::NormalParamContext* ctx) noexcept final {
        last_visited_ = new ASTFunctionParameter(
            loc(ctx), text(ctx->identifier_), static_cast<ASTExpression*>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitClass_definition(ZincParser::Class_definitionContext* ctx) noexcept final {
        ComparableSpan<std::string_view> implements =
            ctx->implements_ | std::views::transform([this](auto child) { return text(child); }) |
            GlobalMemory::collect<ComparableSpan<std::string_view>>();
        ComparableSpan<ASTConstructorDestructorDefinition*> constructors =
            transform_list<ASTConstructorDestructorDefinition>(ctx->constructor_);
        ComparableSpan<ASTConstructorDestructorDefinition*> destructors =
            transform_list<ASTConstructorDestructorDefinition>(ctx->destructor_);
        ComparableSpan<ASTDeclaration*> fields = transform_list<ASTDeclaration>(ctx->fields_);
        ComparableSpan<ASTTypeAlias*> types = transform_list<ASTTypeAlias>(ctx->types_);
        ComparableSpan<ASTFunctionDefinition*> functions =
            transform_list<ASTFunctionDefinition>(ctx->functions_);
        ComparableSpan<ASTClassDefinition*> classes =
            transform_list<ASTClassDefinition>(ctx->classes_);
        if (destructors.size() > 1) {
            /// TODO: thread safety
            Diagnostic::report(DuplicateDestructorError(destructors[1]->location_));
        }
        last_visited_ = new ASTClassDefinition(
            loc(ctx),
            text(ctx->identifier_),
            ctx->extends_ ? text(ctx->extends_) : "",
            implements,
            constructors,
            destructors.empty() ? nullptr : destructors[0],
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
        ComparableSpan<ASTNode*> items =
            ctx->items_ | std::views::transform([this](auto child) { return transform(child); }) |
            GlobalMemory::collect<ComparableSpan<ASTNode*>>();
        last_visited_ = new ASTNamespaceDefinition(
            loc(ctx), ctx->identifier_ ? text(ctx->identifier_) : "", items
        );
        return {};
    }
    antlrcpp::Any visitSelfExpr(ZincParser::SelfExprContext* ctx) noexcept final {
        last_visited_ = new ASTSelfExpr(loc(ctx), false);
        return {};
    }
    antlrcpp::Any visitAssignExpr(ZincParser::AssignExprContext* ctx) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ASSIGN:
            last_visited_ = new ASTAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_ADD_ASSIGN:
            last_visited_ = new ASTAddAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_SUB_ASSIGN:
            last_visited_ = new ASTSubtractAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_MUL_ASSIGN:
            last_visited_ = new ASTMultiplyAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_DIV_ASSIGN:
            last_visited_ = new ASTDivideAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_REM_ASSIGN:
            last_visited_ = new ASTRemainderAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_AND_ASSIGN:
            last_visited_ = new ASTLogicalAndAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_OR_ASSIGN:
            last_visited_ = new ASTLogicalOrAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_BITAND_ASSIGN:
            last_visited_ = new ASTBitwiseAndAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_BITOR_ASSIGN:
            last_visited_ = new ASTBitwiseOrAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_BITXOR_ASSIGN:
            last_visited_ = new ASTBitwiseXorAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_LSHIFT_ASSIGN:
            last_visited_ = new ASTLeftShiftAssignOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_RSHIFT_ASSIGN:
            last_visited_ = new ASTRightShiftAssignOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitEqualityExpr(ZincParser::EqualityExprContext* ctx) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_EQ:
            last_visited_ = new ASTEqualOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_NEQ:
            last_visited_ = new ASTNotEqualOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitRelationalExpr(ZincParser::RelationalExprContext* ctx) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:
            last_visited_ = new ASTLessThanOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_LTE:
            last_visited_ = new ASTLessEqualOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_GT:
            last_visited_ = new ASTGreaterThanOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_GTE:
            last_visited_ = new ASTGreaterEqualOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitShiftExpr(ZincParser::ShiftExprContext* ctx) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LSHIFT:
            last_visited_ = new ASTLeftShiftOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_RSHIFT:
            last_visited_ = new ASTRightShiftOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitAdditiveExpr(ZincParser::AdditiveExprContext* ctx) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ADD:
            last_visited_ = new ASTAddOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_SUB:
            last_visited_ = new ASTSubtractOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitMultiplicativeExpr(
        ZincParser::MultiplicativeExprContext* ctx
    ) noexcept final {
        ASTExpression* left = static_cast<ASTExpression*>(transform(ctx->left_));
        ASTExpression* right = static_cast<ASTExpression*>(transform(ctx->right_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_MUL:
            last_visited_ = new ASTMultiplyOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_DIV:
            last_visited_ = new ASTDivideOp(loc(ctx), left, right);
            break;
        case ZincParser::OP_REM:
            last_visited_ = new ASTRemainderOp(loc(ctx), left, right);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitUnaryExpr(ZincParser::UnaryExprContext* ctx) noexcept final {
        ASTExpression* expr = static_cast<ASTExpression*>(transform(ctx->expr_));
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            last_visited_ = new ASTIncrementOp(loc(ctx), expr);
            break;
        case ZincParser::OP_DEC:
            last_visited_ = new ASTDecrementOp(loc(ctx), expr);
            break;
        case ZincParser::OP_SUB:
            last_visited_ = new ASTNegateOp(loc(ctx), expr);
            break;
        case ZincParser::OP_NOT:
            last_visited_ = new ASTLogicalNotOp(loc(ctx), expr);
            break;
        case ZincParser::OP_BITNOT:
            last_visited_ = new ASTBitwiseNotOp(loc(ctx), expr);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitConstExpr(ZincParser::ConstExprContext* ctx) noexcept final {
        last_visited_ = transform(ctx->constant_);
        return {};
    }
    antlrcpp::Any visitCallExpr(ZincParser::CallExprContext* ctx) noexcept final {
        last_visited_ = new ASTFunctionCall(
            loc(ctx),
            static_cast<ASTExpression*>(transform(ctx->func_)),
            transform_list<ASTExpression>(ctx->arguments_)
        );
        return {};
    }
    antlrcpp::Any visitAddressOfExpr(ZincParser::AddressOfExprContext* ctx) noexcept final {
        ASTExpression* expr = static_cast<ASTExpression*>(transform(ctx->inner_expr_));
        bool is_mutable = ctx->KW_MUT() != nullptr;
        last_visited_ = new ASTReferenceTypeExpr(loc(ctx), expr, is_mutable);
        return {};
    }
    antlrcpp::Any visitMemberAccessExpr(ZincParser::MemberAccessExprContext* ctx) noexcept final {
        ASTExpression* target = static_cast<ASTExpression*>(transform(ctx->target_));
        last_visited_ = new ASTMemberAccess(loc(ctx), target, text(ctx->member_));
        return {};
    }
    antlrcpp::Any visitStructInitExpr(ZincParser::StructInitExprContext* ctx) noexcept final {
        ASTExpression* struct_type = static_cast<ASTExpression*>(transform(ctx->struct_));
        ComparableSpan<ASTFieldInitialization*> inits =
            transform_list<ASTFieldInitialization>(ctx->inits_);
        last_visited_ = new ASTStructInitialization(loc(ctx), struct_type, inits);
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
        last_visited_ = new ASTIdentifier(loc(ctx), text(ctx->name_));
        return {};
    }
    antlrcpp::Any visitConstant(ZincParser::ConstantContext* ctx) noexcept final {
        switch (ctx->value_->getType()) {
        case ZincParser::T_INT:
            last_visited_ =
                new ASTConstant(loc(ctx), text(ctx->value_), std::type_identity<IntegerValue>{});
            break;
        case ZincParser::T_FLOAT:
            last_visited_ =
                new ASTConstant(loc(ctx), text(ctx->value_), std::type_identity<FloatValue>{});
            break;
        case ZincParser::T_STRING:
            last_visited_ =
                new ASTConstant(loc(ctx), text(ctx->value_), std::type_identity<ArrayValue>{});
            break;
        case ZincParser::T_BOOL:
            last_visited_ =
                new ASTConstant(loc(ctx), text(ctx->value_), std::type_identity<BooleanValue>{});
            break;
        case ZincParser::KW_NULLPTR:
            last_visited_ =
                new ASTConstant(loc(ctx), text(ctx->value_), std::type_identity<NullptrValue>{});
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitSelfType(ZincParser::SelfTypeContext* ctx) noexcept final {
        last_visited_ = new ASTSelfExpr(loc(ctx), true);
        return {};
    }
    antlrcpp::Any visitPrimitiveType(ZincParser::PrimitiveTypeContext* ctx) noexcept final {
        switch (ctx->primitive_->getType()) {
        case ZincParser::KW_INT8:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::i8_instance);
            break;
        case ZincParser::KW_INT16:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::i16_instance);
            break;
        case ZincParser::KW_INT32:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::i32_instance);
            break;
        case ZincParser::KW_INT64:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::i64_instance);
            break;
        case ZincParser::KW_UINT8:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::u8_instance);
            break;
        case ZincParser::KW_UINT16:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::u16_instance);
            break;
        case ZincParser::KW_UINT32:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::u32_instance);
            break;
        case ZincParser::KW_UINT64:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &IntegerType::u64_instance);
            break;
        case ZincParser::KW_FLOAT32:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &FloatType::f32_instance);
            break;
        case ZincParser::KW_FLOAT64:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &FloatType::f64_instance);
            break;
        case ZincParser::KW_STRING:
            /// TODO: write a new class to do
            /// TODO: current implementation leads to memory violation (TypeRegistry::get needs to
            /// be called on main thread)
            // last_visited_ = new ASTPrimitiveType(loc(ctx), TypeRegistry::get<StringType>());
            break;
        case ZincParser::KW_BOOL:
            last_visited_ = new ASTPrimitiveType(loc(ctx), &BooleanType::instance);
            break;
        default:
            UNREACHABLE();
        }
        return {};
    }
    antlrcpp::Any visitIdentifierType(ZincParser::IdentifierTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->identifier_);
        return {};
    }
    antlrcpp::Any visitStructType(ZincParser::StructTypeContext* ctx) noexcept final {
        last_visited_ =
            new ASTStructType(loc(ctx), transform_list<ASTFieldDeclaration>(ctx->fields_));
        return {};
    }
    antlrcpp::Any visitFunctionType(ZincParser::FunctionTypeContext* ctx) noexcept final {
        last_visited_ = new ASTFunctionType(
            loc(ctx),
            transform_list<ASTExpression>(ctx->parameters_),
            static_cast<ASTExpression*>(transform(ctx->return_type_))
        );
        return {};
    }
    antlrcpp::Any visitMutableType(ZincParser::MutableTypeContext* ctx) noexcept final {
        ASTExpression* inner_type = static_cast<ASTExpression*>(transform(ctx->inner_type_));
        last_visited_ = new ASTMutableTypeExpr(loc(ctx), inner_type);
        return {};
    }
    antlrcpp::Any visitReferenceType(ZincParser::ReferenceTypeContext* ctx) noexcept final {
        ASTExpression* inner_type = static_cast<ASTExpression*>(transform(ctx->inner_type_));
        last_visited_ = new ASTReferenceTypeExpr(loc(ctx), inner_type, ctx->KW_MOVE() != nullptr);
        return {};
    }
    antlrcpp::Any visitPointerType(ZincParser::PointerTypeContext* ctx) noexcept final {
        ASTExpression* inner_type = static_cast<ASTExpression*>(transform(ctx->inner_type_));
        last_visited_ = new ASTPointerTypeExpr(loc(ctx), inner_type);
        return {};
    }
    antlrcpp::Any visitParenType(ZincParser::ParenTypeContext* ctx) noexcept final {
        last_visited_ = transform(ctx->inner_type_);
        return {};
    }
    antlrcpp::Any visitField_decl(ZincParser::Field_declContext* ctx) noexcept final {
        last_visited_ = new ASTFieldDeclaration(
            loc(ctx), text(ctx->identifier_), static_cast<ASTExpression*>(transform(ctx->type_))
        );
        return {};
    }
    antlrcpp::Any visitField_init(ZincParser::Field_initContext* ctx) noexcept final {
        last_visited_ = new ASTFieldInitialization(
            loc(ctx),
            ctx->identifier_ ? text(ctx->identifier_) : "",
            static_cast<ASTExpression*>(transform(ctx->value_))
        );
        return {};
    }
    antlrcpp::Any visitConstructor(ZincParser::ConstructorContext* ctx) noexcept final {
        ComparableSpan<ASTFunctionParameter*> parameters =
            transform_list<ASTFunctionParameter>(ctx->parameters_);
        ComparableSpan<ASTNode*> body = transform_list(ctx->body_);
        last_visited_ = new ASTConstructorDestructorDefinition(loc(ctx), parameters, body);
        return {};
    }
    antlrcpp::Any visitDestructor(ZincParser::DestructorContext* ctx) noexcept final {
        ComparableSpan<ASTNode*> body = transform_list(ctx->body_);
        last_visited_ = new ASTConstructorDestructorDefinition(
            loc(ctx), ComparableSpan<ASTFunctionParameter*>{}, body
        );
        return {};
    }
};
