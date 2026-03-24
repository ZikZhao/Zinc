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
private:
    bool& has_error_;
    GlobalMemory::String path_string_;

public:
    explicit ErrorTracker(bool& has_error, std::filesystem::path path) : has_error_(has_error) {
        path_string_ =
            path.string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
    }

    void syntaxError(
        antlr4::Recognizer* recognizer,
        antlr4::Token* offendingSymbol,
        size_t line,
        size_t charPositionInLine,
        const std::string& msg,
        std::exception_ptr e
    ) override {
        has_error_ = true;
        std::cerr << path_string_ << ": line " << line << ":" << charPositionInLine << " " << msg
                  << "\n";
    }
};

class ASTBuilder final : private ZincBaseVisitor {
private:
    /// Indication of return type for visit functions.
    template <typename>
    using Any = antlrcpp::Any;

private:
    SourceManager& sources_;
    std::uint32_t file_id_;
    bool has_error_ = false;

private:
    auto loc(const antlr4::ParserRuleContext* ctx) noexcept -> Location {
        if (ctx == nullptr) {
            return Location{.id = file_id_, .begin = 0, .end = 0};
        }
        const SourceFile& file = sources_[file_id_];
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        Location location{
            .id = file_id_,
            .begin = static_cast<std::uint32_t>(file.to_byte_index(start->getStartIndex())),
            .end = static_cast<std::uint32_t>(file.to_byte_index(stop->getStopIndex() + 1))
        };
        return location;
    }

    auto text(antlr4::ParserRuleContext* ctx) noexcept -> strview {
        if (ctx == nullptr) {
            return strview{};
        }
        const SourceFile& file = sources_[file_id_];
        auto start = ctx->getStart();
        auto stop = ctx->getStop();
        std::size_t begin_offset = file.to_byte_index(start->getStartIndex());
        std::size_t end_offset = file.to_byte_index(stop->getStopIndex() + 1);
        return {file.content_.data() + begin_offset, end_offset - begin_offset};
    }

    auto text(const antlr4::Token* token) noexcept -> strview {
        if (token == nullptr) {
            return strview{};
        }
        const SourceFile& file = sources_[file_id_];
        std::size_t begin_offset = file.to_byte_index(token->getStartIndex());
        std::size_t end_offset = file.to_byte_index(token->getStopIndex() + 1);
        return {file.content_.data() + begin_offset, end_offset - begin_offset};
    }

    template <typename R>
    auto visit(auto* ctx) -> R {
        if constexpr (std::is_default_constructible_v<R>) {
            return ctx ? std::any_cast<R>(ZincBaseVisitor::visit(ctx)) : R{};
        } else {
            assert(ctx != nullptr);
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

    auto import_module(strview path) -> const ASTRoot* {
        std::uint32_t module_file_id = sources_.load(path, file_id_);
        if (module_file_id == std::numeric_limits<std::uint32_t>::max()) {
            Diagnostic::error_module_not_found(path, sources_[file_id_].relative_path_.string());
            has_error_ = true;
            return nullptr;
        }
        if (const void* cached = sources_.get_cache(module_file_id)) {
            return static_cast<const ASTRoot*>(cached);
        } else {
            const ASTRoot* module_root = ASTBuilder{sources_, module_file_id}();
            sources_.set_cache(module_file_id, module_root);
            if (module_root == nullptr) {
                has_error_ = true;
            }
            return module_root;
        }
    }

public:
    ASTBuilder(SourceManager& sources, std::uint32_t file_id) noexcept
        : sources_(sources), file_id_(file_id) {}

    auto operator()() noexcept -> const ASTRoot* {
        if (file_id_ == std::numeric_limits<std::uint32_t>::max()) {
            return nullptr;
        }
        antlr4::ANTLRInputStream input(
            sources_[file_id_].content_.data(), sources_[file_id_].content_.size()
        );
        ZincLexer lexer(&input);
        antlr4::CommonTokenStream tokens(&lexer);
        ZincParser parser(&tokens);
        ErrorTracker tracker{has_error_, sources_[file_id_].relative_path_};
        lexer.removeErrorListeners();
        parser.removeErrorListeners();
        lexer.addErrorListener(&tracker);
        parser.addErrorListener(&tracker);
        antlr4::tree::ParseTree* tree = parser.program();
        const ASTRoot* root = std::get<const ASTRoot*>(visit(tree));
        return has_error_ ? nullptr : root;
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
        if (ctx->else_) {
            return as_variant(new ASTIfStatement{
                loc(ctx),
                visit_expr(ctx->condition_),
                std::get<const ASTLocalBlock*>(visit(ctx->if_)),
                visit(ctx->else_),
            });
        } else {
            return as_variant(new ASTIfStatement{
                loc(ctx),
                visit_expr(ctx->condition_),
                std::get<const ASTLocalBlock*>(visit(ctx->if_)),
                visit(ctx->elseif_),
            });
        }
    }

    auto visitSwitch_statement(ZincParser::Switch_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTSwitchStatement{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit_list<ASTSwitchCase>(ctx->cases_),
        });
    }

    auto visitSwitch_case(ZincParser::Switch_caseContext* ctx) noexcept
        -> Any<ASTSwitchCase> final {
        if (ctx->KW_DEFAULT()) {
            return ASTSwitchCase{loc(ctx), std::monostate{}, visit(ctx->body_)};
        } else {
            return ASTSwitchCase{loc(ctx), visit_expr(ctx->value_), visit(ctx->body_)};
        }
    }

    auto visitCStyleFor(ZincParser::CStyleForContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        if (ctx->init_decl_) {
            return as_variant(new ASTForStatement{
                loc(ctx),
                std::get<const ASTDeclaration*>(visit(ctx->init_decl_)),
                visit_expr(ctx->condition_),
                visit_expr(ctx->update_),
                std::get<const ASTLocalBlock*>(visit(ctx->body_)),
            });
        } else {
            return as_variant(new ASTForStatement{
                loc(ctx),
                visit_expr(ctx->init_expr_),
                visit_expr(ctx->condition_),
                visit_expr(ctx->update_),
                std::get<const ASTLocalBlock*>(visit(ctx->body_)),
            });
        }
    }

    auto visitWhileStyleFor(ZincParser::WhileStyleForContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTForStatement{
            loc(ctx),
            std::monostate{},
            visit_expr(ctx->condition_),
            std::monostate{},
            std::get<const ASTLocalBlock*>(visit(ctx->body_)),
        });
    }

    auto visitRangeBasedFor(ZincParser::RangeBasedForContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTRangeBasedForStatement{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->iterable_),
            std::get<const ASTLocalBlock*>(visit(ctx->body_)),
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
        auto* func_def = new ASTFunctionDefinition{
            loc(ctx),
            text(ctx->identifier_),
            visit_list<ASTFunctionParameter>(ctx->parameters_),
            visit_expr(ctx->return_type_),
            visit_list(ctx->body_),
            ctx->KW_CONST() != nullptr,
            ctx->KW_STATIC() != nullptr
        };
        if (ctx->template_list_) {
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                func_def->identifier,
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                func_def
            });
        }
        return as_variant(func_def);
    }

    auto visitOperator_overload_definition(
        ZincParser::Operator_overload_definitionContext* ctx
    ) noexcept -> Any<ASTNodeVariant> final {
        /// TODO: throw error if operator is invalid or if number of parameters doesn't match
        /// operator
        OperatorCode opcode;
        switch (ctx->operator_->getType()) {
        case ZincParser::OP_ADD:
            opcode = OperatorCode::Add;
            break;
        case ZincParser::OP_SUB:
            opcode = ctx->parameters_.size() == 1 ? OperatorCode::Negate : OperatorCode::Subtract;
            break;
        case ZincParser::OP_MUL:
            opcode = OperatorCode::Multiply;
            break;
        case ZincParser::OP_DIV:
            opcode = OperatorCode::Divide;
            break;
        case ZincParser::OP_REM:
            opcode = OperatorCode::Remainder;
            break;
        case ZincParser::OP_INC:
            opcode = OperatorCode::Increment;
            break;
        case ZincParser::OP_DEC:
            opcode = OperatorCode::Decrement;
            break;
        case ZincParser::OP_EQ:
            opcode = OperatorCode::Equal;
            break;
        case ZincParser::OP_NEQ:
            opcode = OperatorCode::NotEqual;
            break;
        case ZincParser::OP_LT:
            opcode = ctx->op2_
                         ? (ctx->op3_ ? OperatorCode::LeftShiftAssign : OperatorCode::LeftShift)
                         : OperatorCode::LessThan;
            break;
        case ZincParser::OP_LTE:
            opcode = OperatorCode::LessEqual;
            break;
        case ZincParser::OP_GT:
            opcode = ctx->op2_
                         ? (ctx->op3_ ? OperatorCode::RightShiftAssign : OperatorCode::RightShift)
                         : OperatorCode::GreaterThan;
            break;
        case ZincParser::OP_GTE:
            opcode = OperatorCode::GreaterEqual;
            break;
        case ZincParser::OP_AND:
            opcode = OperatorCode::LogicalAnd;
            break;
        case ZincParser::OP_OR:
            opcode = OperatorCode::LogicalOr;
            break;
        case ZincParser::OP_NOT:
            opcode = OperatorCode::LogicalNot;
            break;
        case ZincParser::OP_BITAND:
            opcode = OperatorCode::BitwiseAnd;
            break;
        case ZincParser::OP_BITOR:
            opcode = OperatorCode::BitwiseOr;
            break;
        case ZincParser::OP_BITXOR:
            opcode = OperatorCode::BitwiseXor;
            break;
        case ZincParser::OP_BITNOT:
            opcode = OperatorCode::BitwiseNot;
            break;
        case ZincParser::OP_ASSIGN:
            opcode = OperatorCode::Assign;
            break;
        case ZincParser::OP_ADD_ASSIGN:
            opcode = OperatorCode::AddAssign;
            break;
        case ZincParser::OP_SUB_ASSIGN:
            opcode = OperatorCode::SubtractAssign;
            break;
        case ZincParser::OP_MUL_ASSIGN:
            opcode = OperatorCode::MultiplyAssign;
            break;
        case ZincParser::OP_DIV_ASSIGN:
            opcode = OperatorCode::DivideAssign;
            break;
        case ZincParser::OP_REM_ASSIGN:
            opcode = OperatorCode::RemainderAssign;
            break;
        case ZincParser::OP_AND_ASSIGN:
            opcode = OperatorCode::LogicalAndAssign;
            break;
        case ZincParser::OP_OR_ASSIGN:
            opcode = OperatorCode::LogicalOrAssign;
            break;
        case ZincParser::OP_BITAND_ASSIGN:
            opcode = OperatorCode::BitwiseAndAssign;
            break;
        case ZincParser::OP_BITOR_ASSIGN:
            opcode = OperatorCode::BitwiseOrAssign;
            break;
        case ZincParser::OP_BITXOR_ASSIGN:
            opcode = OperatorCode::BitwiseXorAssign;
            break;
        case ZincParser::OP_LPAREN:
            opcode = OperatorCode::Call;
            break;
        case ZincParser::OP_LBRACKET:
            opcode = OperatorCode::Index;
            break;
        case ZincParser::OP_ARROW:
            opcode = OperatorCode::Pointer;
            break;
        default:
            UNREACHABLE();
        }
        auto* op_def = new ASTOperatorDefinition{
            loc(ctx),
            opcode,
            visit<ASTFunctionParameter>(ctx->parameters_[0]),
            ctx->parameters_.size() > 1
                ? new ASTFunctionParameter(visit<ASTFunctionParameter>(ctx->parameters_[1]))
                : nullptr,
            visit_expr(ctx->return_type_),
            visit_list(ctx->body_),
            ctx->KW_CONST() != nullptr
        };
        if (ctx->template_list_) {
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                GetOperatorString(opcode),
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                op_def
            });
        }
        return as_variant(op_def);
    }

    auto visitSelfParam(ZincParser::SelfParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{loc(ctx), "self", visit_expr(ctx->type_)};
    }

    auto visitNormalParam(ZincParser::NormalParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            std::monostate{},
            ctx->KW_MUT() != nullptr,
            false
        };
    }

    auto visitDefaultParam(ZincParser::DefaultParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            visit_expr(ctx->default_),
            ctx->KW_MUT() != nullptr,
            false
        };
    }

    auto visitVariadicParam(ZincParser::VariadicParamContext* ctx) noexcept
        -> Any<ASTFunctionParameter> final {
        return ASTFunctionParameter{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->type_),
            ASTExprVariant(new ASTSelfExpr{loc(ctx), true}),
            ctx->KW_MUT() != nullptr,
            true
        };
    }

    auto visitClass_definition(ZincParser::Class_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        std::span fields = visit_list(ctx->fields_);
        std::span scope_items = GlobalMemory::alloc_array<ASTNodeVariant>(
            ctx->aliases_.size() + ctx->classes_.size() + ctx->constructors_.size() +
            ctx->destructors_.size() + ctx->functions_.size() + ctx->operators_.size()
        );
        std::size_t item_index = 0;
        for (const auto& alias : ctx->aliases_) {
            scope_items[item_index++] = visit(alias);
        }
        for (const auto& class_def : ctx->classes_) {
            scope_items[item_index++] = visit(class_def);
        }
        for (const auto& constructor : ctx->constructors_) {
            scope_items[item_index++] = visit(constructor);
        }
        for (const auto& destructor : ctx->destructors_) {
            scope_items[item_index++] = visit(destructor);
        }
        for (const auto& function : ctx->functions_) {
            scope_items[item_index++] = visit(function);
        }
        for (const auto& operator_def : ctx->operators_) {
            scope_items[item_index++] = visit(operator_def);
        }
        auto* class_def = new ASTClassDefinition{
            loc(ctx),
            text(ctx->identifier_),
            visit_expr(ctx->extends_),
            visit_list<ASTExprVariant>(ctx->implements_),
            fields,
            scope_items,
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

    auto visitEnum_definition(ZincParser::Enum_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTEnumDefinition{
            loc(ctx),
            text(ctx->identifier_),
            ctx->enumerators_ | std::views::transform([this](auto& enumerator) {
                return text(enumerator);
            }) | GlobalMemory::collect<std::span>()
        });
    }

    auto visitNamespace_definition(ZincParser::Namespace_definitionContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTNamespaceDefinition{
            loc(ctx), ctx->identifier_ ? text(ctx->identifier_) : "", visit_list(ctx->items_)
        });
    }

    auto visitThrow_statement(ZincParser::Throw_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTThrowStatement{loc(ctx), visit_expr(ctx->expr_)});
    }

    auto visitStatic_assert_statement(ZincParser::Static_assert_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        return as_variant(new ASTStaticAssertStatement{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit_expr(ctx->message_),
        });
    }

    auto visitImport_statement(ZincParser::Import_statementContext* ctx) noexcept
        -> Any<ASTNodeVariant> final {
        strview path = text(ctx->path_);
        path = path.substr(1, path.size() - 2);  // remove quotes
        return as_variant(
            new ASTImportStatement{loc(ctx), path, text(ctx->identifier_), import_module(path)}
        );
    }

    auto visitSelfExpr(ZincParser::SelfExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTSelfExpr{loc(ctx), false});
    }

    auto visitConstExpr(ZincParser::ConstExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return visit_expr(ctx->constant_);
    }

    auto visitIdentifierExpr(ZincParser::IdentifierExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTIdentifier{loc(ctx), text(ctx->identifier_)});
    }

    auto visitMemberAccessExpr(ZincParser::MemberAccessExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTMemberAccess{loc(ctx), visit_expr(ctx->base_), text(ctx->member_)}
        );
    }

    auto visitPointerAccessExpr(ZincParser::PointerAccessExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTPointerAccess{loc(ctx), visit_expr(ctx->base_), text(ctx->member_)}
        );
    }

    auto visitIndexAccessExpr(ZincParser::IndexAccessExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTIndexAccess{loc(ctx), visit_expr(ctx->base_), visit_expr(ctx->length_)}
        );
    }

    auto visitCallExpr(ZincParser::CallExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTFunctionCall{
            loc(ctx), visit_expr(ctx->func_), visit_list<ASTExprVariant>(ctx->arguments_)
        });
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

    auto visitParenExpr(ZincParser::ParenExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTParenExpr{loc(ctx), visit_expr(ctx->inner_expr_)});
    }

    auto visitInstantiationExpr(ZincParser::InstantiationExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTTemplateInstantiation{
            loc(ctx),
            visit_expr(ctx->template_),
            visit<std::span<ASTExprVariant>>(ctx->instantiation_list_)
        });
    }

    auto visitTernaryExpr(ZincParser::TernaryExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTTernaryOp{
            loc(ctx),
            visit_expr(ctx->condition_),
            visit_expr(ctx->true_expr_),
            visit_expr(ctx->false_expr_)
        });
    }

    auto visitAsExpr(ZincParser::AsExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(new ASTAs{loc(ctx), visit_expr(ctx->expr_), visit_expr(ctx->type_)});
    }

    auto visitLambdaExpr(ZincParser::LambdaExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        if (ctx->expr_) {
            return as_variant(new ASTLambda{
                loc(ctx),
                visit_list<ASTFunctionParameter>(ctx->parameters_),
                visit_expr(ctx->return_type_),
                visit_expr(ctx->expr_)
            });
        } else {
            return as_variant(new ASTLambda{
                loc(ctx),
                visit_list<ASTFunctionParameter>(ctx->parameters_),
                visit_expr(ctx->return_type_),
                visit(ctx->body_)
            });
        }
    }

    auto visitUnaryExpr(ZincParser::UnaryExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant expr = visit_expr(ctx->expr_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::Increment, expr});
        case ZincParser::OP_DEC:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::Decrement, expr});
        case ZincParser::OP_SUB:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::Negate, expr});
        case ZincParser::OP_NOT:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::LogicalNot, expr});
        case ZincParser::OP_BITNOT:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::BitwiseNot, expr});
        case ZincParser::OP_BITAND:
            return as_variant(
                new ASTAddressOfExpr{loc(ctx), visit_expr(ctx->expr_), ctx->KW_MUT() != nullptr}
            );
        case ZincParser::OP_MUL:
            return as_variant(new ASTDereference{loc(ctx), visit_expr(ctx->expr_)});
        default:
            UNREACHABLE();
        }
    }

    auto visitPostfixUnaryExpr(ZincParser::PostfixUnaryExprContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        ASTExprVariant expr = visit_expr(ctx->expr_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_INC:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::PostIncrement, expr});
        case ZincParser::OP_DEC:
            return as_variant(new ASTUnaryOp{loc(ctx), OperatorCode::PostDecrement, expr});
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
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Multiply, left, right});
        case ZincParser::OP_DIV:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Divide, left, right});
        case ZincParser::OP_REM:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Remainder, left, right});
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
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Add, left, right});
        case ZincParser::OP_SUB:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Subtract, left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitShiftExpr(ZincParser::ShiftExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_LT:  // OP_LSHIFT
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::LeftShift, left, right});
        case ZincParser::OP_GT:  // OP_RSHIFT
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::RightShift, left, right});
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
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::LessThan, left, right});
        case ZincParser::OP_LTE:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::LessEqual, left, right});
        case ZincParser::OP_GT:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::GreaterThan, left, right});
        case ZincParser::OP_GTE:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::GreaterEqual, left, right});
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
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Equal, left, right});
        case ZincParser::OP_NEQ:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::NotEqual, left, right});
        default:
            UNREACHABLE();
        }
    }

    auto visitBitAndExpr(ZincParser::BitAndExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseAnd, left, right});
    }

    auto visitBitXorExpr(ZincParser::BitXorExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseXor, left, right});
    }

    auto visitBitOrExpr(ZincParser::BitOrExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseOr, left, right});
    }

    auto visitAndExpr(ZincParser::AndExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::LogicalAnd, left, right});
    }

    auto visitOrExpr(ZincParser::OrExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::LogicalOr, left, right});
    }

    auto visitAssignExpr(ZincParser::AssignExprContext* ctx) noexcept -> Any<ASTExprVariant> final {
        ASTExprVariant left = visit_expr(ctx->left_);
        ASTExprVariant right = visit_expr(ctx->right_);
        switch (ctx->op_->getType()) {
        case ZincParser::OP_ASSIGN:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::Assign, left, right});
        case ZincParser::OP_ADD_ASSIGN:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::AddAssign, left, right});
        case ZincParser::OP_SUB_ASSIGN:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::SubtractAssign, left, right});
        case ZincParser::OP_MUL_ASSIGN:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::MultiplyAssign, left, right});
        case ZincParser::OP_DIV_ASSIGN:
            return as_variant(new ASTBinaryOp{loc(ctx), OperatorCode::DivideAssign, left, right});
        case ZincParser::OP_REM_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::RemainderAssign, left, right}
            );
        case ZincParser::OP_AND_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::LogicalAndAssign, left, right}
            );
        case ZincParser::OP_OR_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::LogicalOrAssign, left, right}
            );
        case ZincParser::OP_BITAND_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseAndAssign, left, right}
            );
        case ZincParser::OP_BITOR_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseOrAssign, left, right}
            );
        case ZincParser::OP_BITXOR_ASSIGN:
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::BitwiseXorAssign, left, right}
            );
        case ZincParser::OP_LT:  // OP_LSHIFT_ASSIGN
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::LeftShiftAssign, left, right}
            );
        case ZincParser::OP_GT:  // OP_RSHIFT_ASSIGN
            return as_variant(
                new ASTBinaryOp{loc(ctx), OperatorCode::RightShiftAssign, left, right}
            );
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
        case ZincParser::KW_VOID:
            return as_variant(new ASTPrimitiveType{loc(ctx), &VoidType::instance});
        case ZincParser::KW_INT8:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i8_instance});
        case ZincParser::KW_INT16:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i16_instance});
        case ZincParser::KW_INT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i32_instance});
        case ZincParser::KW_INT64:
        case ZincParser::KW_ISIZE:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::i64_instance});
        case ZincParser::KW_UINT8:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u8_instance});
        case ZincParser::KW_UINT16:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u16_instance});
        case ZincParser::KW_UINT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u32_instance});
        case ZincParser::KW_UINT64:
        case ZincParser::KW_USIZE:
            return as_variant(new ASTPrimitiveType{loc(ctx), &IntegerType::u64_instance});
        case ZincParser::KW_FLOAT32:
            return as_variant(new ASTPrimitiveType{loc(ctx), &FloatType::f32_instance});
        case ZincParser::KW_FLOAT64:
            return as_variant(new ASTPrimitiveType{loc(ctx), &FloatType::f64_instance});
        case ZincParser::KW_BOOL:
            return as_variant(new ASTPrimitiveType{loc(ctx), &BooleanType::instance});
        case ZincParser::KW_STRVIEW:
            return as_variant(new ASTStringViewType{loc(ctx)});
        default:
            UNREACHABLE();
        }
    }

    auto visitIdentifierType(ZincParser::IdentifierTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTIdentifier{loc(ctx), text(ctx->identifier_)});
    }

    auto visitMemberAccessType(ZincParser::MemberAccessTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTMemberAccess{loc(ctx), visit_expr(ctx->base_), text(ctx->member_)}
        );
    }

    auto visitStructType(ZincParser::StructTypeContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTStructType{loc(ctx), visit_list<ASTFieldDeclaration>(ctx->fields_)}
        );
    }

    auto visitArrayType(ZincParser::ArrayTypeContext* ctx) noexcept -> Any<ASTExprVariant> final {
        return as_variant(
            new ASTArrayType{loc(ctx), visit_expr(ctx->element_type_), visit_expr(ctx->length_)}
        );
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

    auto visitInstantiatedType(ZincParser::InstantiatedTypeContext* ctx) noexcept
        -> Any<ASTExprVariant> final {
        return as_variant(new ASTTemplateInstantiation{
            loc(ctx),
            visit_expr(ctx->template_),
            visit<std::span<ASTExprVariant>>(ctx->instantiation_list_),
        });
    }

    auto visitAny_identifier(ZincParser::Any_identifierContext* ctx) noexcept -> Any<void> final {
        UNREACHABLE();
    }

    auto visitConstant(ZincParser::ConstantContext* ctx) noexcept -> Any<ASTExprVariant> final {
        switch (ctx->value_->getType()) {
        case ZincParser::T_INT:
            return as_variant(new ASTConstant{loc(ctx), new IntegerValue(text(ctx->value_))});
        case ZincParser::T_FLOAT: {
            std::string str = ctx->value_->getText();
            double value = 0.0;
            auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
            if (ec == std::errc::result_out_of_range) {
                throw;
            } else if (ec != std::errc{}) {
                throw;
            }
            auto* float_value = new FloatValue(value);
            if (!str.empty() && (str.back() == 'f' || str.back() == 'F')) {
                float_value = float_value->resolve_to(&FloatType::f32_instance);
            }
            return as_variant(new ASTConstant{loc(ctx), float_value});
        }
        case ZincParser::T_STRING: {
            std::string str = ctx->value_->getText();
            unescape_string(str);
            strview persisted = GlobalMemory::persist_string({str.data() + 1, str.size() - 2});
            return as_variant(new ASTStringConstant{loc(ctx), persisted});
        }
        case ZincParser::T_CHAR: {
            std::string str = ctx->value_->getText();
            unescape_string(str);
            return as_variant(new ASTConstant{
                loc(ctx),
                new IntegerValue(&IntegerType::i8_instance, static_cast<std::size_t>(str[1]))
            });
        }
        case ZincParser::KW_TRUE:
            return as_variant(new ASTConstant{loc(ctx), new BooleanValue(true)});
        case ZincParser::KW_FALSE:
            return as_variant(new ASTConstant{loc(ctx), new BooleanValue(false)});
        case ZincParser::KW_NULLPTR:
            return as_variant(new ASTConstant{loc(ctx), new NullptrValue()});
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
        -> Any<ASTNodeVariant> final {
        auto* constructor_def = new ASTCtorDtorDefinition{
            loc(ctx),
            visit_list<ASTFunctionParameter>(ctx->parameters_),
            visit_list<ASTNodeVariant>(ctx->body_),
            true,
            ctx->KW_CONST() != nullptr
        };
        if (ctx->template_list_) {
            return as_variant(new ASTTemplateDefinition{
                loc(ctx),
                "!",
                visit<std::span<ASTTemplateParameter>>(ctx->template_list_),
                constructor_def
            });
        }
        return as_variant(constructor_def);
    }

    auto visitDestructor(ZincParser::DestructorContext* ctx) noexcept -> Any<ASTNodeVariant> final {
        return as_variant(new ASTCtorDtorDefinition{
            loc(ctx),
            std::span<ASTFunctionParameter>{},
            visit_list(ctx->body_),
            false,
            ctx->KW_CONST() != nullptr
        });
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
            ctx->OP_ELLIPSIS() != nullptr,
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
            ctx->OP_ELLIPSIS() != nullptr,
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
