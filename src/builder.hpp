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

private:
    std::shared_future<ASTRoot> import_module(std::string_view path) {
        return importer_.import(path, [this](const SourceManager::File& file) {
            return std::move(*ASTBuilder(file, importer_)());
        });
    }
    template <typename R>
    R visit(auto* ctx) {
        return std::any_cast<R>(ZincBaseVisitor::visit(ctx));
    }
    ASTNodeVariant visit(auto* ctx) { return visit<ASTNodeVariant>(ctx); }
    ASTExprVariant visit_expr(auto* ctx) { return visit<ASTExprVariant>(ctx); }
    template <typename R>
    std::span<R> visit_list(const auto& contexts) {
        return contexts | std::views::transform([this](auto* ctx) { return visit<R>(ctx); }) |
               GlobalMemory::collect<std::span<R>>();
    }
    std::span<ASTNodeVariant> visit_list(const auto& contexts) {
        return visit_list<ASTNodeVariant>(contexts);
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
        return std::get<ASTRoot*>(visit(tree));
    }

private:
    antlrcpp::Any visitProgram(ZincParser::ProgramContext* ctx) noexcept final {
        return ASTRoot{loc(ctx), visit_list(ctx->statements_)};
    }
    antlrcpp::Any visitTop_level_statement(
        ZincParser::Top_level_statementContext* ctx
    ) noexcept final {
        return visit(ctx->children[0]);
    }
    antlrcpp::Any visitStatement(ZincParser::StatementContext* ctx) noexcept final {
        return visit(ctx->children[0]);
    }
    antlrcpp::Any visitLocal_block(ZincParser::Local_blockContext* ctx) noexcept final {
        return new ASTLocalBlock{loc(ctx), visit_list(ctx->statements_)};
    }
    antlrcpp::Any visitExpr_statement(ZincParser::Expr_statementContext* ctx) noexcept final {
        return new ASTExpressionStatement{loc(ctx), visit_expr(ctx->expr())};
    }
    antlrcpp::Any visitLetDecl(ZincParser::LetDeclContext* ctx) noexcept final {
        return ASTDeclaration{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            visit_expr(ctx->value_),
            ctx->KW_MUT() != nullptr,
            false
        };
    }
    antlrcpp::Any visitConstDecl(ZincParser::ConstDeclContext* ctx) noexcept final {
        return new ASTDeclaration{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            visit_expr(ctx->value_),
            false,
            true
        };
    }
    antlrcpp::Any visitIf_statement(ZincParser::If_statementContext* ctx) noexcept final {
        return new ASTIfStatement{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit<ASTLocalBlock*>(ctx->if_),
            visit<ASTLocalBlock*>(ctx->else_),
        };
    }
    antlrcpp::Any visitFor_statement(ZincParser::For_statementContext* ctx) noexcept final {
        if (ctx->init_decl_) {
            return new ASTForStatement{
                loc(ctx),
                visit<ASTDeclaration*>(ctx->init_decl_),
                visit_expr(ctx->condition_),
                visit_expr(ctx->update_),
                visit<ASTLocalBlock*>(ctx->body_),
            };
        } else if (ctx->init_expr_) {
            return new ASTForStatement{
                loc(ctx),
                visit(ctx->init_expr_),
                visit_expr(ctx->condition_),
                visit_expr(ctx->update_),
                visit<ASTLocalBlock*>(ctx->body_),
            };
        } else {
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitBreak_statement(ZincParser::Break_statementContext* ctx) noexcept final {
        return new ASTBreakStatement{loc(ctx)};
    }
    antlrcpp::Any visitContinue_statement(
        ZincParser::Continue_statementContext* ctx
    ) noexcept final {
        return new ASTContinueStatement{loc(ctx)};
    }
    antlrcpp::Any visitReturn_statement(ZincParser::Return_statementContext* ctx) noexcept final {
        return new ASTReturnStatement{loc(ctx), visit_expr(ctx->expr_)};
    }
    antlrcpp::Any visitType_alias(ZincParser::Type_aliasContext* ctx) noexcept final {
        return new ASTTypeAlias{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
    }
    antlrcpp::Any visitFunction_definition(
        ZincParser::Function_definitionContext* ctx
    ) noexcept final {
        std::span parameters = visit_list<ASTFunctionParameter>(ctx->parameters_);
        std::string_view identifier = text(ctx->identifier_);
        ASTExprVariant return_type = visit_expr(ctx->return_type_);
        std::span body = visit_list(ctx->body_);
        auto function_def = new ASTFunctionDefinition{
            loc(ctx),
            identifier,
            parameters,
            return_type,
            body,
            ctx->KW_CONST() != nullptr,
            ctx->KW_STATIC() != nullptr,
            ctx->semi_ != nullptr
        };
        if (ctx->template_list_) {
            return new ASTTemplateDefinition{
                loc(ctx),
                identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                function_def
            };
        }
        return function_def;
    }
    antlrcpp::Any visitSelfParam(ZincParser::SelfParamContext* ctx) noexcept final {
        return ASTFunctionParameter{loc(ctx), "self", visit_expr(ctx->type_)};
    }
    antlrcpp::Any visitNormalParam(ZincParser::NormalParamContext* ctx) noexcept final {
        return ASTFunctionParameter{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
    }
    antlrcpp::Any visitClass_definition(ZincParser::Class_definitionContext* ctx) noexcept final {
        std::span implements = ctx->implements_ |
                               std::views::transform([this](auto child) { return text(child); }) |
                               GlobalMemory::collect<std::span<std::string_view>>();
        std::span<ASTConstructorDestructorDefinition*> constructors =
            visit_list<ASTConstructorDestructorDefinition*>(ctx->constructor_);
        std::span<ASTConstructorDestructorDefinition*> destructors =
            visit_list<ASTConstructorDestructorDefinition*>(ctx->destructor_);
        std::span<ASTDeclaration*> fields = visit_list<ASTDeclaration*>(ctx->fields_);
        std::span<ASTTypeAlias*> types = visit_list<ASTTypeAlias*>(ctx->types_);
        std::span<ASTFunctionDefinition*> functions =
            visit_list<ASTFunctionDefinition*>(ctx->functions_);
        std::span<ASTClassDefinition*> classes = visit_list<ASTClassDefinition*>(ctx->classes_);
        if (destructors.size() > 1) {
            /// TODO: thread safety
            Diagnostic::report(DuplicateDestructorError(destructors[1]->location_));
        }
        auto class_def = new ASTClassDefinition{
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
        };
        if (ctx->template_list_) {
            return new ASTTemplateDefinition{
                loc(ctx),
                text(ctx->identifier_),
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                class_def
            };
        }
        return class_def;
    }
    antlrcpp::Any visitNamespace_definition(
        ZincParser::Namespace_definitionContext* ctx
    ) noexcept final {
        return new ASTNamespaceDefinition{
            loc(ctx), ctx->identifier_ ? text(ctx->identifier_) : "", visit_list(ctx->items_)
        };
    }
    antlrcpp::Any visitSelfExpr(ZincParser::SelfExprContext* ctx) noexcept final {
        return new ASTSelfExpr{loc(ctx), false};
    }
    antlrcpp::Any visitAssignExpr(ZincParser::AssignExprContext* ctx) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ASSIGN:
            return new ASTAssignOp{loc(ctx), left, right};
        case ZincParser::OP_ADD_ASSIGN:
            return new ASTAddAssignOp{loc(ctx), left, right};
        case ZincParser::OP_SUB_ASSIGN:
            return new ASTSubtractAssignOp{loc(ctx), left, right};
        case ZincParser::OP_MUL_ASSIGN:
            return new ASTMultiplyAssignOp{loc(ctx), left, right};
        case ZincParser::OP_DIV_ASSIGN:
            return new ASTDivideAssignOp{loc(ctx), left, right};
        case ZincParser::OP_REM_ASSIGN:
            return new ASTRemainderAssignOp{loc(ctx), left, right};
        case ZincParser::OP_AND_ASSIGN:
            return new ASTLogicalAndAssignOp{loc(ctx), left, right};
        case ZincParser::OP_OR_ASSIGN:
            return new ASTLogicalOrAssignOp{loc(ctx), left, right};
        case ZincParser::OP_BITAND_ASSIGN:
            return new ASTBitwiseAndAssignOp{loc(ctx), left, right};
        case ZincParser::OP_BITOR_ASSIGN:
            return new ASTBitwiseOrAssignOp{loc(ctx), left, right};
        case ZincParser::OP_BITXOR_ASSIGN:
            return new ASTBitwiseXorAssignOp{loc(ctx), left, right};
        case ZincParser::OP_LSHIFT_ASSIGN:
            return new ASTLeftShiftAssignOp{loc(ctx), left, right};
        case ZincParser::OP_RSHIFT_ASSIGN:
            return new ASTRightShiftAssignOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitEqualityExpr(ZincParser::EqualityExprContext* ctx) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_EQ:
            return new ASTEqualOp{loc(ctx), left, right};
        case ZincParser::OP_NEQ:
            return new ASTNotEqualOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitRelationalExpr(ZincParser::RelationalExprContext* ctx) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:
            return new ASTLessThanOp{loc(ctx), left, right};
        case ZincParser::OP_LTE:
            return new ASTLessEqualOp{loc(ctx), left, right};
        case ZincParser::OP_GT:
            return new ASTGreaterThanOp{loc(ctx), left, right};
        case ZincParser::OP_GTE:
            return new ASTGreaterEqualOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitShiftExpr(ZincParser::ShiftExprContext* ctx) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LSHIFT:
            return new ASTLeftShiftOp{loc(ctx), left, right};
        case ZincParser::OP_RSHIFT:
            return new ASTRightShiftOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitAdditiveExpr(ZincParser::AdditiveExprContext* ctx) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ADD:
            return new ASTAddOp{loc(ctx), left, right};
        case ZincParser::OP_SUB:
            return new ASTSubtractOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitMultiplicativeExpr(
        ZincParser::MultiplicativeExprContext* ctx
    ) noexcept final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_MUL:
            return new ASTMultiplyOp{loc(ctx), left, right};
        case ZincParser::OP_DIV:
            return new ASTDivideOp{loc(ctx), left, right};
        case ZincParser::OP_REM:
            return new ASTRemainderOp{loc(ctx), left, right};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitUnaryExpr(ZincParser::UnaryExprContext* ctx) noexcept final {
        ASTExprVariant expr = visit_expr(ctx->expr_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            return new ASTIncrementOp{loc(ctx), expr};
        case ZincParser::OP_DEC:
            return new ASTDecrementOp{loc(ctx), expr};
        case ZincParser::OP_SUB:
            return new ASTNegateOp{loc(ctx), expr};
        case ZincParser::OP_NOT:
            return new ASTLogicalNotOp{loc(ctx), expr};
        case ZincParser::OP_BITNOT:
            return new ASTBitwiseNotOp{loc(ctx), expr};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitConstExpr(ZincParser::ConstExprContext* ctx) noexcept final {
        return visit(ctx->constant_);
    }
    antlrcpp::Any visitIdentifierExpr(ZincParser::IdentifierExprContext* ctx) noexcept final {
        return visit(ctx->identifier_);
    }
    antlrcpp::Any visitTemplateIdentifierExpr(
        ZincParser::TemplateIdentifierExprContext* ctx
    ) noexcept final {
        return new ASTTemplateInstantiation{
            loc(ctx), text(ctx->identifier_), visit<std::span<ASTExprVariant>>(ctx->template_args_)
        };
    }
    antlrcpp::Any visitCallExpr(ZincParser::CallExprContext* ctx) noexcept final {
        return new ASTFunctionCall{
            loc(ctx), visit_expr(ctx->func_), visit_list<ASTExprVariant>(ctx->arguments_)
        };
    }
    antlrcpp::Any visitAddressOfExpr(ZincParser::AddressOfExprContext* ctx) noexcept final {
        return new ASTReferenceType{
            loc(ctx), visit_expr(ctx->inner_expr_), ctx->KW_MUT() != nullptr
        };
    }
    antlrcpp::Any visitMemberAccessExpr(ZincParser::MemberAccessExprContext* ctx) noexcept final {
        std::span members = ctx->members_ |
                            std::views::transform([this](auto child) { return text(child); }) |
                            GlobalMemory::collect<std::span<std::string_view>>();
        return new ASTMemberAccess{loc(ctx), visit_expr(ctx->target_), members};
    }
    antlrcpp::Any visitStructInitExpr(ZincParser::StructInitExprContext* ctx) noexcept final {
        return new ASTStructInitialization{
            loc(ctx), visit_expr(ctx->struct_), visit_list<ASTFieldInitialization>(ctx->inits_)
        };
    }
    antlrcpp::Any visitParenExpr(ZincParser::ParenExprContext* ctx) noexcept final {
        return new ASTParenExpr{loc(ctx), visit_expr(ctx->inner_expr_)};
    }
    antlrcpp::Any visitIdentifier(ZincParser::IdentifierContext* ctx) noexcept final {
        return new ASTIdentifier{loc(ctx), text(ctx)};
    }
    antlrcpp::Any visitConstant(ZincParser::ConstantContext* ctx) noexcept final {
        switch (ctx->value_->getType()) {
        case ZincParser::T_INT:
            return new ASTConstant{loc(ctx), Value::from_literal<IntegerValue>(text(ctx->value_))};
        case ZincParser::T_FLOAT:
            return new ASTConstant{loc(ctx), Value::from_literal<FloatValue>(text(ctx->value_))};
        case ZincParser::T_STRING:
            return new ASTConstant{loc(ctx), Value::from_literal<ArrayValue>(text(ctx->value_))};
        case ZincParser::T_BOOL:
            return new ASTConstant{loc(ctx), Value::from_literal<BooleanValue>(text(ctx->value_))};
        case ZincParser::KW_NULLPTR:
            return new ASTConstant{loc(ctx), Value::from_literal<NullptrValue>(text(ctx->value_))};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitSelfType(ZincParser::SelfTypeContext* ctx) noexcept final {
        return new ASTSelfExpr{loc(ctx), true};
    }
    antlrcpp::Any visitPrimitiveType(ZincParser::PrimitiveTypeContext* ctx) noexcept final {
        switch (ctx->primitive_->getType()) {
        case ZincParser::KW_INT8:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::i8_instance};
        case ZincParser::KW_INT16:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::i16_instance};
        case ZincParser::KW_INT32:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::i32_instance};
        case ZincParser::KW_INT64:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::i64_instance};
        case ZincParser::KW_UINT8:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::u8_instance};
        case ZincParser::KW_UINT16:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::u16_instance};
        case ZincParser::KW_UINT32:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::u32_instance};
        case ZincParser::KW_UINT64:
            return new ASTPrimitiveType{loc(ctx), &IntegerType::u64_instance};
        case ZincParser::KW_FLOAT32:
            return new ASTPrimitiveType{loc(ctx), &FloatType::f32_instance};
        case ZincParser::KW_FLOAT64:
            return new ASTPrimitiveType{loc(ctx), &FloatType::f64_instance};
        case ZincParser::KW_STRING:
            /// TODO: write a new class to do
            /// TODO: current implementation leads to memory violation (TypeRegistry::get needs
            /// to be called on main thread)
            // last_visited_ = new ASTPrimitiveType(loc(ctx), TypeRegistry::get<StringType>());
            return nullptr;
        case ZincParser::KW_BOOL:
            return new ASTPrimitiveType{loc(ctx), &BooleanType::instance};
        default:
            UNREACHABLE();
        }
    }
    antlrcpp::Any visitIdentifierType(ZincParser::IdentifierTypeContext* ctx) noexcept final {
        return visit(ctx->identifier_);
    }
    antlrcpp::Any visitStructType(ZincParser::StructTypeContext* ctx) noexcept final {
        return new ASTStructType{loc(ctx), visit_list<ASTFieldDeclaration>(ctx->fields_)};
    }
    antlrcpp::Any visitFunctionType(ZincParser::FunctionTypeContext* ctx) noexcept final {
        return new ASTFunctionType{
            loc(ctx),
            visit_list<ASTExprVariant>(ctx->parameters_),
            visit_expr(ctx->return_type_),
        };
        return {};
    }
    antlrcpp::Any visitMutableType(ZincParser::MutableTypeContext* ctx) noexcept final {
        return new ASTMutableType{loc(ctx), visit_expr(ctx->inner_type_)};
    }
    antlrcpp::Any visitReferenceType(ZincParser::ReferenceTypeContext* ctx) noexcept final {
        return new ASTReferenceType{
            loc(ctx), visit_expr(ctx->inner_type_), ctx->KW_MOVE() != nullptr
        };
    }
    antlrcpp::Any visitPointerType(ZincParser::PointerTypeContext* ctx) noexcept final {
        return new ASTPointerType{loc(ctx), visit_expr(ctx->inner_type_)};
    }
    antlrcpp::Any visitParenType(ZincParser::ParenTypeContext* ctx) noexcept final {
        return visit_expr(ctx->inner_type_);
    }
    antlrcpp::Any visitField_decl(ZincParser::Field_declContext* ctx) noexcept final {
        return ASTFieldDeclaration{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
    }
    antlrcpp::Any visitField_init(ZincParser::Field_initContext* ctx) noexcept final {
        return ASTFieldInitialization{
            loc(ctx),
            ctx->identifier_ ? text(ctx->identifier_) : "",
            visit_expr(ctx->value_),
        };
    }
    antlrcpp::Any visitConstructor(ZincParser::ConstructorContext* ctx) noexcept final {
        return new ASTConstructorDestructorDefinition{
            loc(ctx),
            true,
            visit_list<ASTFunctionParameter>(ctx->parameters_),
            visit_list(ctx->body_)
        };
    }
    antlrcpp::Any visitDestructor(ZincParser::DestructorContext* ctx) noexcept final {
        return new ASTConstructorDestructorDefinition{
            loc(ctx), false, std::span<ASTFunctionParameter>{}, visit_list(ctx->body_)
        };
    }
    antlrcpp::Any visitTemplate_parameter_list(
        ZincParser::Template_parameter_listContext* ctx
    ) noexcept final {
        return visit_list<ASTTemplateParameter>(ctx->parameters_);
    }
    antlrcpp::Any visitTypeTemplateParam(ZincParser::TypeTemplateParamContext* ctx) noexcept final {
        return ASTTemplateParameter{
            loc(ctx),
            false,
            text(ctx->identifier_),
            std::monostate{},
            visit_expr(ctx->default_),
        };
    }
    antlrcpp::Any visitComptimeTemplateParam(
        ZincParser::ComptimeTemplateParamContext* ctx
    ) noexcept final {
        return ASTTemplateParameter{
            loc(ctx),
            true,
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            std::monostate{},
        };
    }
    antlrcpp::Any visitInstantiation_list(
        ZincParser::Instantiation_listContext* ctx
    ) noexcept final {
        return visit_list(ctx->arguments_);
    }
    antlrcpp::Any visitInstantiation_argument(
        ZincParser::Instantiation_argumentContext* ctx
    ) noexcept final {
        return ctx->type_ ? visit_expr(ctx->type_) : visit_expr(ctx->value_);
    }
};
