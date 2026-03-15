#pragma once
#include "pch.hpp"

#include "ZincBaseVisitor.h"
#include "ZincLexer.h"
#include "ZincParser.h"
#include "ast.hpp"
#include "builtins.hpp"
#include "object.hpp"
#include "source.hpp"

class ErrorTracker : public antlr4::BaseErrorListener {
public:
    bool has_error = false;

public:
    void syntaxError(
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
    /// Indication of return type for visit functions.
    template <typename>
    using Any = antlrcpp::Any;

    template <typename T>
    struct Cast {
        template <typename U>
        friend auto operator|(U&& value, Cast) -> const T* {
            if constexpr (requires { std::declval<U>().operator[]; }) {
                return std::forward<U>(value) | std::views::transform([](auto& elem) -> const T* {
                           return std::get<const T*>(elem);
                       }) |
                       GlobalMemory::collect<std::span>();
            } else {
                return std::get<const T*>(std::forward<U>(value));
            }
        }
    };

private:
    const SourceManager::File& file_;
    ImportManager<ASTRoot>& importer_;

private:
    auto loc(const antlr4::ParserRuleContext* ctx) noexcept -> Location {
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

    auto text(const antlr4::ParserRuleContext* ctx) noexcept -> std::string_view {
        assert(ctx != nullptr);
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        std::size_t begin_offset = start->getStartIndex();
        std::size_t end_offset = stop->getStopIndex() + 1;
        return {file_.content.data() + begin_offset, end_offset - begin_offset};
    }

    auto text(const antlr4::Token* token) noexcept -> std::string_view {
        assert(token != nullptr);
        std::size_t begin_offset = token->getStartIndex();
        std::size_t end_offset = token->getStopIndex() + 1;
        return {file_.content.data() + begin_offset, end_offset - begin_offset};
    }

    template <typename R>
    auto visit(auto* ctx) -> R {
        if constexpr (std::is_default_constructible_v<R>) {
            return ctx ? std::any_cast<R>(ZincBaseVisitor::visit(ctx)) : R{};
        } else {
            assert(ctx);
            return std::any_cast<R>(ZincBaseVisitor::visit(ctx));
        }
    }

    auto visit(auto* ctx) -> ASTNodeVariant { return visit<ASTNodeVariant>(ctx); }

    auto visit_expr(auto* ctx) -> ASTExprVariant { return visit<ASTExprVariant>(ctx); }

    template <typename R>
    auto visit_list(const auto& contexts) -> std::span<R> {
        return contexts | std::views::transform([&](auto* ctx) { return visit<R>(ctx); }) |
               GlobalMemory::collect<std::span<R>>();
    }

    auto visit_list(const auto& contexts) -> std::span<ASTNodeVariant> {
        return visit_list<ASTNodeVariant>(contexts);
    }

    auto as_variant(auto* ptr) {
        if constexpr (std::derived_from<std::decay_t<decltype(*ptr)>, ASTExpression>) {
            return ASTExprVariant(ptr);
        } else {
            return ASTNodeVariant(ptr);
        }
    }

    auto import_module(std::string_view path) -> std::shared_future<ASTRoot> {
        return importer_.import(path, [this](const SourceManager::File& file) {
            return *ASTBuilder(file, importer_)();
        });
    }

public:
    ASTBuilder(const SourceManager::File& file, ImportManager<ASTRoot>& importer) noexcept
        : file_(file), importer_(importer) {}

    auto operator()() noexcept -> const ASTRoot* {
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
        return std::get<const ASTRoot*>(visit(tree));
    }

private:
    auto visitProgram(ZincParser::ProgramContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        return as_variant(new ASTRoot{loc(ctx), visit_list(ctx->statements_)});
    }

    auto visitTop_level_statement(ZincParser::Top_level_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return visit(ctx->children[0]);
    }

    auto visitStatement(ZincParser::StatementContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        return visit(ctx->children[0]);
    }

    auto visitLocal_block(ZincParser::Local_blockContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTLocalBlock{loc(ctx), visit_list(ctx->statements_)});
    }

    auto visitExpr_statement(ZincParser::Expr_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTExpressionStatement{loc(ctx), visit_expr(ctx->expr())});
    }

    auto visitLetDecl(ZincParser::LetDeclContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        return as_variant(new ASTDeclaration{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            visit_expr(ctx->value_),
            ctx->KW_MUT() != nullptr,
            false
        });
    }

    auto visitConstDecl(ZincParser::ConstDeclContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        return as_variant(new ASTDeclaration{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            visit_expr(ctx->value_),
            false,
            true
        });
    }

    auto visitIf_statement(ZincParser::If_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTIfStatement{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit(ctx->if_) | Cast<ASTLocalBlock>{},
            visit(ctx->else_) | Cast<ASTLocalBlock>{},
        });
    }

    auto visitFor_statement(ZincParser::For_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTForStatement{
            loc(ctx),
            visit(ctx->init_decl_) | Cast<ASTDeclaration>(),
            visit_expr(ctx->init_expr_),
            visit_expr(ctx->condition_),
            visit_expr(ctx->update_),
            visit(ctx->body_) | Cast<ASTLocalBlock>{},
        });
    }

    auto visitBreak_statement(ZincParser::Break_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTBreakStatement{loc(ctx)});
    }

    auto visitContinue_statement(ZincParser::Continue_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTContinueStatement{loc(ctx)});
    }

    auto visitReturn_statement(ZincParser::Return_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTReturnStatement{loc(ctx), visit_expr(ctx->expr_)});
    }

    auto visitType_alias(ZincParser::Type_aliasContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        auto* type_alias =
            new ASTTypeAlias{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
        if (ctx->template_list_) {
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                type_alias->identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                type_alias
            });
        }
        return as_variant(type_alias);
    }

    auto visitFunction_definition(ZincParser::Function_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
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
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                function_def
            });
        }
        return as_variant(function_def);
    }

    auto visitSelfParam(ZincParser::SelfParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{loc(ctx), "self", visit_expr(ctx->type_)};
    }

    auto visitNormalParam(ZincParser::NormalParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
    }

    auto visitClass_definition(ZincParser::Class_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        std::span implements = ctx->implements_ |
                               std::views::transform([this](auto child) { return text(child); }) |
                               GlobalMemory::collect<std::span<std::string_view>>();
        std::span constructors =
            visit_list<const ASTConstructorDestructorDefinition*>(ctx->constructor_);
        std::span destructors =
            visit_list<const ASTConstructorDestructorDefinition*>(ctx->destructor_);
        std::span fields = visit_list(ctx->fields_) |
                           std::views::transform([](ASTNodeVariant node) -> const ASTDeclaration* {
                               return std::get<const ASTDeclaration*>(node);
                           }) |
                           GlobalMemory::collect<std::span>();
        std::span types = visit_list(ctx->types_) |
                          std::views::transform([](ASTNodeVariant node) -> const ASTTypeAlias* {
                              return std::get<const ASTTypeAlias*>(node);
                          }) |
                          GlobalMemory::collect<std::span>();
        std::span functions =
            visit_list(ctx->functions_) |
            std::views::transform([](ASTNodeVariant node) -> const ASTFunctionDefinition* {
                return std::get<const ASTFunctionDefinition*>(node);
            }) |
            GlobalMemory::collect<std::span>();
        std::span classes =
            visit_list(ctx->classes_) |
            std::views::transform([](ASTNodeVariant node) -> const ASTClassDefinition* {
                return std::get<const ASTClassDefinition*>(node);
            }) |
            GlobalMemory::collect<std::span>();
        if (destructors.size() > 1) {
            /// TODO: thread safety
            Diagnostic::report(DuplicateDestructorError(destructors[1]->location));
        }
        auto* class_def = new ASTClassDefinition{
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
            if (ctx->specialize_list_) throw;
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                class_def->identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                class_def
            });
        } else if (ctx->specialize_list_) {
            if (!ctx->instantiation_list_) throw;
            return as_variant(new ASTTemplateSpecialization{
                loc(ctx),
                class_def->identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->specialize_list_),
                visit<std::span<ASTExprVariant>>(ctx->instantiation_list_),
                class_def
            });
        } else {
            return as_variant(class_def);
        }
    }

    auto visitNamespace_definition(ZincParser::Namespace_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTNamespaceDefinition{
            loc(ctx), ctx->identifier_ ? text(ctx->identifier_) : "", visit_list(ctx->items_)
        });
    }

    auto visitStatic_assert_statement(ZincParser::Static_assert_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTStaticAssertStatement{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit_expr(ctx->message_),
        });
    }

    auto visitSelfExpr(ZincParser::SelfExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTSelfExpr{loc(ctx), false});
    }

    auto visitConstExpr(ZincParser::ConstExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return visit_expr(ctx->constant_);
    }

    auto visitIdentifierExpr(ZincParser::IdentifierExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return visit_expr(ctx->identifier_);
    }

    auto visitAccessChainExpr(ZincParser::AccessChainExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant base = visit_expr(ctx->base_);
        std::span members = ctx->members_ |
                            std::views::transform([this](auto* member) { return text(member); }) |
                            GlobalMemory::collect<std::span>();
        std::span instantiation_list = visit<std::span<ASTExprVariant>>(ctx->instantiation_list_);
        return as_variant(new ASTAccessChain{loc(ctx), base, members, instantiation_list});
    }

    auto visitAccessChainExprAlternate(ZincParser::AccessChainExprAlternateContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant base = visit_expr(ctx->base_);
        std::span members = ctx->members_ |
                            std::views::transform([this](auto* member) { return text(member); }) |
                            GlobalMemory::collect<std::span>();
        std::span instantiation_list = visit<std::span<ASTExprVariant>>(ctx->instantiation_list_);
        return as_variant(new ASTAccessChain{loc(ctx), base, members, instantiation_list});
    }

    auto visitCallExpr(ZincParser::CallExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTFunctionCall{
            loc(ctx), visit_expr(ctx->func_), visit_list<ASTExprVariant>(ctx->arguments_)
        });
    }

    auto visitAddressOfExpr(ZincParser::AddressOfExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTReferenceType{loc(ctx), visit_expr(ctx->inner_expr_), ctx->KW_MUT() != nullptr}
        );
    }

    auto visitStructInitExpr(ZincParser::StructInitExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTStructInitialization{
            loc(ctx), visit_expr(ctx->struct_), visit_list<ASTFieldInitialization>(ctx->inits_)
        });
    }

    auto visitArrayInitExpr(ZincParser::ArrayInitExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTArrayInitialization{loc(ctx), visit_list<ASTExprVariant>(ctx->elements_)}
        );
    }

    auto visitArrayAccessExpr(ZincParser::ArrayAccessExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTArrayAccess{loc(ctx), visit_expr(ctx->base_), visit_expr(ctx->length_)}
        );
    }

    auto visitParenExpr(ZincParser::ParenExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTParenExpr{loc(ctx), visit_expr(ctx->inner_expr_)});
    }

    auto visitUnaryExpr(ZincParser::UnaryExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant expr = visit_expr(ctx->expr_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            return as_variant(new ASTIncrementOp{loc(ctx), expr});
        case ZincParser::OP_DEC:
            return as_variant(new ASTDecrementOp{loc(ctx), expr});
        case ZincParser::OP_SUB:
            return as_variant(new ASTNegateOp{loc(ctx), expr});
        case ZincParser::OP_NOT:
            return as_variant(new ASTLogicalNotOp{loc(ctx), expr});
        case ZincParser::OP_BITNOT:
            return as_variant(new ASTBitwiseNotOp{loc(ctx), expr});
        default:
            UNREACHABLE();
        }
    }

    auto visitMultiplicativeExpr(ZincParser::MultiplicativeExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_MUL:
            return as_variant(new ASTMultiplyOp{loc(ctx), left, right});
        case ZincParser::OP_DIV:
            return as_variant(new ASTDivideOp{loc(ctx), left, right});
        case ZincParser::OP_REM:
            return as_variant(new ASTRemainderOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitAdditiveExpr(ZincParser::AdditiveExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ADD:
            return as_variant(new ASTAddOp{loc(ctx), left, right});
        case ZincParser::OP_SUB:
            return as_variant(new ASTSubtractOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitShiftExpr(ZincParser::ShiftExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:  // OP_LSHIFT
            return as_variant(new ASTLeftShiftOp{loc(ctx), left, right});
        case ZincParser::OP_GT:  // OP_RSHIFT
            return as_variant(new ASTRightShiftOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitRelationalExpr(ZincParser::RelationalExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:
            return as_variant(new ASTLessThanOp{loc(ctx), left, right});
        case ZincParser::OP_LTE:
            return as_variant(new ASTLessEqualOp{loc(ctx), left, right});
        case ZincParser::OP_GT:
            return as_variant(new ASTGreaterThanOp{loc(ctx), left, right});
        case ZincParser::OP_GTE:
            return as_variant(new ASTGreaterEqualOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitEqualityExpr(ZincParser::EqualityExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_EQ:
            return as_variant(new ASTEqualOp{loc(ctx), left, right});
        case ZincParser::OP_NEQ:
            return as_variant(new ASTNotEqualOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitBitAndExpr(ZincParser::BitAndExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBitwiseAndOp{loc(ctx), left, right});
    }

    auto visitBitXorExpr(ZincParser::BitXorExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBitwiseXorOp{loc(ctx), left, right});
    }

    auto visitBitOrExpr(ZincParser::BitOrExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBitwiseOrOp{loc(ctx), left, right});
    }

    auto visitAndExpr(ZincParser::AndExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTLogicalAndOp{loc(ctx), left, right});
    }

    auto visitOrExpr(ZincParser::OrExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTLogicalOrOp{loc(ctx), left, right});
    }

    auto visitAssignExpr(ZincParser::AssignExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ASSIGN:
            return as_variant(new ASTAssignOp{loc(ctx), left, right});
        case ZincParser::OP_ADD_ASSIGN:
            return as_variant(new ASTAddAssignOp{loc(ctx), left, right});
        case ZincParser::OP_SUB_ASSIGN:
            return as_variant(new ASTSubtractAssignOp{loc(ctx), left, right});
        case ZincParser::OP_MUL_ASSIGN:
            return as_variant(new ASTMultiplyAssignOp{loc(ctx), left, right});
        case ZincParser::OP_DIV_ASSIGN:
            return as_variant(new ASTDivideAssignOp{loc(ctx), left, right});
        case ZincParser::OP_REM_ASSIGN:
            return as_variant(new ASTRemainderAssignOp{loc(ctx), left, right});
        case ZincParser::OP_AND_ASSIGN:
            return as_variant(new ASTLogicalAndAssignOp{loc(ctx), left, right});
        case ZincParser::OP_OR_ASSIGN:
            return as_variant(new ASTLogicalOrAssignOp{loc(ctx), left, right});
        case ZincParser::OP_BITAND_ASSIGN:
            return as_variant(new ASTBitwiseAndAssignOp{loc(ctx), left, right});
        case ZincParser::OP_BITOR_ASSIGN:
            return as_variant(new ASTBitwiseOrAssignOp{loc(ctx), left, right});
        case ZincParser::OP_BITXOR_ASSIGN:
            return as_variant(new ASTBitwiseXorAssignOp{loc(ctx), left, right});
        case ZincParser::OP_LT:  // OP_LSHIFT_ASSIGN
            return as_variant(new ASTLeftShiftAssignOp{loc(ctx), left, right});
        case ZincParser::OP_GT:  // OP_RSHIFT_ASSIGN
            return as_variant(new ASTRightShiftAssignOp{loc(ctx), left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitSelfType(ZincParser::SelfTypeContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTSelfExpr{loc(ctx), true});
    }

    auto visitPrimitiveType(ZincParser::PrimitiveTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        switch (ctx->primitive_->getType()) {
        case ZincParser::KW_INT8:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i8_instance});
        case ZincParser::KW_INT16:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i16_instance});
        case ZincParser::KW_INT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i32_instance});
        case ZincParser::KW_INT64:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i64_instance});
        case ZincParser::KW_UINT8:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u8_instance});
        case ZincParser::KW_UINT16:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u16_instance});
        case ZincParser::KW_UINT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u32_instance});
        case ZincParser::KW_UINT64:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u64_instance});
        case ZincParser::KW_FLOAT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &FloatType::f32_instance});
        case ZincParser::KW_FLOAT64:
            return as_variant(new ASTPrimitiveType{loc(ctx), &FloatType::f64_instance});
        case ZincParser::KW_STRING:
            /// TODO: write a new class to do
            /// TODO: current implementation leads to memory violation
            /// (TypeRegistry::get needs to be called on main thread)
            // last_visited_ = new ASTPrimitiveType(loc(ctx),
            // TypeRegistry::get<StringType>());
            return nullptr;
        case ZincParser::KW_BOOL:
            return as_variant(new ASTPrimitiveType{loc(ctx), &BooleanType::instance});
        default:
            UNREACHABLE();
        }
    }

    auto visitIdentifierType(ZincParser::IdentifierTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTIdentifier{loc(ctx), text(ctx->identifier_)});
    }

    auto visitAccessChainType(ZincParser::AccessChainTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        std::span members = ctx->members_ |
                            std::views::transform([this](auto* member) { return text(member); }) |
                            GlobalMemory::collect<std::span>();
        std::span instantiation_list = visit<std::span<ASTExprVariant>>(ctx->instantiation_list_);
        return as_variant(
            new ASTAccessChain{loc(ctx), visit_expr(ctx->base_), members, instantiation_list}
        );
    }

    auto visitAccessChainTypeAlternate(ZincParser::AccessChainTypeAlternateContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        std::span members = ctx->members_ |
                            std::views::transform([this](auto* member) { return text(member); }) |
                            GlobalMemory::collect<std::span>();
        std::span instantiation_list = visit<std::span<ASTExprVariant>>(ctx->instantiation_list_);
        return as_variant(
            new ASTAccessChain{loc(ctx), visit_expr(ctx->base_), members, instantiation_list}
        );
    }

    auto visitStructType(ZincParser::StructTypeContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTStructType{loc(ctx), visit_list<ASTFieldDeclaration>(ctx->fields_)}
        );
    }

    auto visitArrayType(ZincParser::ArrayTypeContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTArrayType{loc(ctx), visit_expr(ctx->element_type_)});
    }

    auto visitFunctionType(ZincParser::FunctionTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTFunctionType{
            loc(ctx),
            visit_list<ASTExprVariant>(ctx->parameters_),
            visit_expr(ctx->return_type_),
        });
    }

    auto visitMutableType(ZincParser::MutableTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTMutableType{loc(ctx), visit_expr(ctx->inner_type_)});
    }

    auto visitReferenceType(ZincParser::ReferenceTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTReferenceType{loc(ctx), visit_expr(ctx->inner_type_), ctx->KW_MOVE() != nullptr}
        );
    }

    auto visitPointerType(ZincParser::PointerTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTPointerType{loc(ctx), visit_expr(ctx->inner_type_)});
    }

    auto visitIdentifier(ZincParser::IdentifierContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTIdentifier{loc(ctx), text(ctx)});
    }

    auto visitConstant(ZincParser::ConstantContext* ctx) noexcept -> Any<ASTExprVariant> final {
        switch (ctx->value_->getType()) {
        case ZincParser::T_INT:
            return as_variant(
                new ASTConstant{loc(ctx), Value::from_literal<IntegerValue>(text(ctx->value_))}
            );
        case ZincParser::T_FLOAT:
            return as_variant(
                new ASTConstant{loc(ctx), Value::from_literal<FloatValue>(text(ctx->value_))}
            );
        case ZincParser::T_STRING:
            return as_variant(
                new ASTConstant{loc(ctx), Value::from_literal<ArrayValue>(text(ctx->value_))}
            );
        case ZincParser::T_BOOL:
            return as_variant(
                new ASTConstant{loc(ctx), Value::from_literal<BooleanValue>(text(ctx->value_))}
            );
        case ZincParser::KW_NULLPTR:
            return as_variant(
                new ASTConstant{loc(ctx), Value::from_literal<NullptrValue>(text(ctx->value_))}
            );
        default:
            UNREACHABLE();
        }
    }

    auto visitField_decl(ZincParser::Field_declContext* ctx) noexcept
        -> Any<ASTFieldDeclaration> final {
        return ASTFieldDeclaration{loc(ctx), text(ctx->identifier_), visit_expr(ctx->type_)};
    }

    auto visitField_init(ZincParser::Field_initContext* ctx) noexcept
        -> Any<ASTFieldInitialization> final {
        return ASTFieldInitialization{
            loc(ctx),
            ctx->identifier_ ? text(ctx->identifier_) : "",
            visit_expr(ctx->value_),
        };
    }

    auto visitConstructor(ZincParser::ConstructorContext* ctx) noexcept
        -> Any<const ASTConstructorDestructorDefinition*> final {
        return const_cast<const ASTConstructorDestructorDefinition*>(
            new ASTConstructorDestructorDefinition{
                loc(ctx),
                true,
                visit_list<ASTFunctionParameter>(ctx->parameters_),
                visit_list(ctx->body_)
            }
        );
    }

    auto visitDestructor(ZincParser::DestructorContext* ctx) noexcept
        -> Any<const ASTConstructorDestructorDefinition*> final {
        return const_cast<const ASTConstructorDestructorDefinition*>(
            new ASTConstructorDestructorDefinition{
                loc(ctx), false, std::span<ASTFunctionParameter>{}, visit_list(ctx->body_)
            }
        );
    }

    auto visitTemplate_parameter_list(ZincParser::Template_parameter_listContext* ctx) noexcept
        -> Any<std::span<ASTTemplateParameter>> final {
        return visit_list<ASTTemplateParameter>(ctx->parameters_);
    }

    auto visitSpecialize_parameter_list(ZincParser::Specialize_parameter_listContext* ctx) noexcept
        -> Any<std::span<ASTTemplateParameter>> final {
        return visit_list<ASTTemplateParameter>(ctx->parameters_);
    }

    auto visitTypeTemplateParam(ZincParser::TypeTemplateParamContext* ctx) noexcept
        -> Any<ASTTemplateParameter> final {
        return ASTTemplateParameter{
            loc(ctx),
            false,
            text(ctx->identifier_),
            std::monostate{},
            visit_expr(ctx->default_),
        };
    }

    auto visitComptimeTemplateParam(ZincParser::ComptimeTemplateParamContext* ctx) noexcept
        -> Any<ASTTemplateParameter> final {
        return ASTTemplateParameter{
            loc(ctx),
            true,
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            std::monostate{},
        };
    }

    auto visitInstantiation_list(ZincParser::Instantiation_listContext* ctx) noexcept
        -> Any<std::span<ASTExprVariant>> final {
        return ctx->arguments_ |
               std::views::transform([this](auto* arg) { return visit_expr(arg); }) |
               GlobalMemory::collect<std::span>();
    }

    auto visitInstantiation_argument(ZincParser::Instantiation_argumentContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return ctx->type_ ? visit_expr(ctx->type_) : visit_expr(ctx->value_);
    }
};
