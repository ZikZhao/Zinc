#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

struct ASTNode;
struct ASTCompileTimeConstruct;
struct ASTRoot;
struct ASTLocalBlock;

struct ASTExpression;
struct ASTExplicitTypeExpr;
struct ASTHiddenTypeExpr;
struct ASTParenExpr;
struct ASTConstant;
struct ASTSelfExpr;
struct ASTIdentifier;
template <typename Op>
struct ASTUnaryOp;
template <typename Op>
struct ASTBinaryOp;

using ASTAddOp = ASTBinaryOp<OperatorFunctors::Add>;
using ASTSubtractOp = ASTBinaryOp<OperatorFunctors::Subtract>;
using ASTNegateOp = ASTUnaryOp<OperatorFunctors::Negate>;
using ASTMultiplyOp = ASTBinaryOp<OperatorFunctors::Multiply>;
using ASTDivideOp = ASTBinaryOp<OperatorFunctors::Divide>;
using ASTRemainderOp = ASTBinaryOp<OperatorFunctors::Remainder>;
using ASTIncrementOp = ASTUnaryOp<OperatorFunctors::Increment>;
using ASTDecrementOp = ASTUnaryOp<OperatorFunctors::Decrement>;

using ASTEqualOp = ASTBinaryOp<OperatorFunctors::Equal>;
using ASTNotEqualOp = ASTBinaryOp<OperatorFunctors::NotEqual>;
using ASTLessThanOp = ASTBinaryOp<OperatorFunctors::LessThan>;
using ASTLessEqualOp = ASTBinaryOp<OperatorFunctors::LessEqual>;
using ASTGreaterThanOp = ASTBinaryOp<OperatorFunctors::GreaterThan>;
using ASTGreaterEqualOp = ASTBinaryOp<OperatorFunctors::GreaterEqual>;

using ASTLogicalAndOp = ASTBinaryOp<OperatorFunctors::LogicalAnd>;
using ASTLogicalOrOp = ASTBinaryOp<OperatorFunctors::LogicalOr>;
using ASTLogicalNotOp = ASTUnaryOp<OperatorFunctors::LogicalNot>;

using ASTBitwiseAndOp = ASTBinaryOp<OperatorFunctors::BitwiseAnd>;
using ASTBitwiseOrOp = ASTBinaryOp<OperatorFunctors::BitwiseOr>;
using ASTBitwiseXorOp = ASTBinaryOp<OperatorFunctors::BitwiseXor>;
using ASTBitwiseNotOp = ASTUnaryOp<OperatorFunctors::BitwiseNot>;
using ASTLeftShiftOp = ASTBinaryOp<OperatorFunctors::LeftShift>;
using ASTRightShiftOp = ASTBinaryOp<OperatorFunctors::RightShift>;

using ASTAssignOp = ASTBinaryOp<OperatorFunctors::Assign>;
using ASTAddAssignOp = ASTBinaryOp<OperatorFunctors::AddAssign>;
using ASTSubtractAssignOp = ASTBinaryOp<OperatorFunctors::SubtractAssign>;
using ASTMultiplyAssignOp = ASTBinaryOp<OperatorFunctors::MultiplyAssign>;
using ASTDivideAssignOp = ASTBinaryOp<OperatorFunctors::DivideAssign>;
using ASTRemainderAssignOp = ASTBinaryOp<OperatorFunctors::RemainderAssign>;
using ASTLogicalAndAssignOp = ASTBinaryOp<OperatorFunctors::LogicalAndAssign>;
using ASTLogicalOrAssignOp = ASTBinaryOp<OperatorFunctors::LogicalOrAssign>;
using ASTBitwiseAndAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseAndAssign>;
using ASTBitwiseOrAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseOrAssign>;
using ASTBitwiseXorAssignOp = ASTBinaryOp<OperatorFunctors::BitwiseXorAssign>;
using ASTLeftShiftAssignOp = ASTBinaryOp<OperatorFunctors::LeftShiftAssign>;
using ASTRightShiftAssignOp = ASTBinaryOp<OperatorFunctors::RightShiftAssign>;

struct ASTMemberAccess;
struct ASTFieldInitialization;
struct ASTStructInitialization;
struct ASTFunctionCall;
struct ASTPrimitiveType;
struct ASTFunctionType;
struct ASTStructType;
struct ASTMutableType;
struct ASTReferenceType;
struct ASTPointerType;
struct ASTTemplateInstantiation;
struct ASTTemplateMemberAccessInstantiation;
struct ASTExpressionStatement;
struct ASTRuntimeSymbolDeclaration;
struct ASTDeclaration;
struct ASTFieldDeclaration;
struct ASTTypeAlias;
struct ASTIfStatement;
struct ASTForStatement;
struct ASTContinueStatement;
struct ASTBreakStatement;
struct ASTReturnStatement;
struct ASTFunctionParameter;
struct ASTFunctionDefinition;
struct ASTConstructorDestructorDefinition;
struct ASTClassSignature;
struct ASTClassDefinition;
struct ASTNamespaceDefinition;
struct ASTTemplateParameter;
struct ASTTemplateDefinition;
struct ASTTemplateSpecialization;

// All AST node types (base classes and final classes)
using ASTNodeVariant = std::variant<
    std::monostate,
    // Base classes
    ASTNode*,
    ASTCompileTimeConstruct*,
    ASTRuntimeSymbolDeclaration*,
    ASTExpression*,
    ASTExplicitTypeExpr*,
    ASTHiddenTypeExpr*,
    // Root and blocks
    ASTRoot*,
    ASTLocalBlock*,
    // Expressions
    ASTParenExpr*,
    ASTConstant*,
    ASTSelfExpr*,
    ASTIdentifier*,
    // Unary operators
    ASTNegateOp*,
    ASTIncrementOp*,
    ASTDecrementOp*,
    ASTLogicalNotOp*,
    ASTBitwiseNotOp*,
    // Binary operators - arithmetic
    ASTAddOp*,
    ASTSubtractOp*,
    ASTMultiplyOp*,
    ASTDivideOp*,
    ASTRemainderOp*,
    // Binary operators - comparison
    ASTEqualOp*,
    ASTNotEqualOp*,
    ASTLessThanOp*,
    ASTLessEqualOp*,
    ASTGreaterThanOp*,
    ASTGreaterEqualOp*,
    // Binary operators - logical
    ASTLogicalAndOp*,
    ASTLogicalOrOp*,
    // Binary operators - bitwise
    ASTBitwiseAndOp*,
    ASTBitwiseOrOp*,
    ASTBitwiseXorOp*,
    ASTLeftShiftOp*,
    ASTRightShiftOp*,
    // Binary operators - assignment
    ASTAssignOp*,
    ASTAddAssignOp*,
    ASTSubtractAssignOp*,
    ASTMultiplyAssignOp*,
    ASTDivideAssignOp*,
    ASTRemainderAssignOp*,
    ASTLogicalAndAssignOp*,
    ASTLogicalOrAssignOp*,
    ASTBitwiseAndAssignOp*,
    ASTBitwiseOrAssignOp*,
    ASTBitwiseXorAssignOp*,
    ASTLeftShiftAssignOp*,
    ASTRightShiftAssignOp*,
    // Member access and calls
    ASTMemberAccess*,
    ASTStructInitialization*,
    ASTFunctionCall*,
    // Type expressions
    ASTPrimitiveType*,
    ASTFunctionType*,
    ASTFieldDeclaration*,
    ASTStructType*,
    ASTMutableType*,
    ASTReferenceType*,
    ASTPointerType*,
    ASTTemplateInstantiation*,
    ASTTemplateMemberAccessInstantiation*,
    // Statements
    ASTExpressionStatement*,
    ASTDeclaration*,
    ASTTypeAlias*,
    ASTIfStatement*,
    ASTForStatement*,
    ASTContinueStatement*,
    ASTBreakStatement*,
    ASTReturnStatement*,
    // Functions and classes
    ASTFunctionDefinition*,
    ASTConstructorDestructorDefinition*,
    ASTClassDefinition*,
    ASTNamespaceDefinition*,
    // Templates
    ASTTemplateDefinition*,
    ASTTemplateSpecialization*>;

// All expression node types (all ASTExpression derivatives)
using ASTExprVariant = std::variant<
    std::monostate,
    // Base classes
    ASTExpression*,
    ASTExplicitTypeExpr*,
    ASTHiddenTypeExpr*,
    // Basic expressions
    ASTParenExpr*,
    ASTConstant*,
    ASTSelfExpr*,
    ASTIdentifier*,
    // Unary operators
    ASTNegateOp*,
    ASTIncrementOp*,
    ASTDecrementOp*,
    ASTLogicalNotOp*,
    ASTBitwiseNotOp*,
    // Binary operators - arithmetic
    ASTAddOp*,
    ASTSubtractOp*,
    ASTMultiplyOp*,
    ASTDivideOp*,
    ASTRemainderOp*,
    // Binary operators - comparison
    ASTEqualOp*,
    ASTNotEqualOp*,
    ASTLessThanOp*,
    ASTLessEqualOp*,
    ASTGreaterThanOp*,
    ASTGreaterEqualOp*,
    // Binary operators - logical
    ASTLogicalAndOp*,
    ASTLogicalOrOp*,
    // Binary operators - bitwise
    ASTBitwiseAndOp*,
    ASTBitwiseOrOp*,
    ASTBitwiseXorOp*,
    ASTLeftShiftOp*,
    ASTRightShiftOp*,
    // Binary operators - assignment
    ASTAssignOp*,
    ASTAddAssignOp*,
    ASTSubtractAssignOp*,
    ASTMultiplyAssignOp*,
    ASTDivideAssignOp*,
    ASTRemainderAssignOp*,
    ASTLogicalAndAssignOp*,
    ASTLogicalOrAssignOp*,
    ASTBitwiseAndAssignOp*,
    ASTBitwiseOrAssignOp*,
    ASTBitwiseXorAssignOp*,
    ASTLeftShiftAssignOp*,
    ASTRightShiftAssignOp*,
    // Member access and calls
    ASTMemberAccess*,
    ASTStructInitialization*,
    ASTFunctionCall*,
    // Type expressions
    ASTPrimitiveType*,
    ASTFunctionType*,
    ASTStructType*,
    ASTMutableType*,
    ASTReferenceType*,
    ASTPointerType*,
    ASTTemplateInstantiation*,
    ASTTemplateMemberAccessInstantiation*,
    // Special
    ASTClassSignature*>;

struct ASTNode : public GlobalMemory::MonotonicAllocated {
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}
};

struct ASTCompileTimeConstruct : public ASTNode {};

struct ASTLocalBlock final : public ASTNode {
    std::span<ASTNodeVariant> statements_;
};

struct ASTRoot final : public ASTNode {
    std::span<ASTNodeVariant> statements_;
};

struct ASTExpression : public ASTNode {};

struct ASTExplicitTypeExpr : public ASTExpression {};

/// A hidden type expression that does not appear in source code
struct ASTHiddenTypeExpr : public ASTExplicitTypeExpr {};

struct ASTParenExpr final : public ASTExpression {
    ASTExprVariant inner_;
};

struct ASTConstant final : public ASTExpression {
    const Value* value_;
};

struct ASTSelfExpr final : public ASTExpression {
    bool is_type_;
};

struct ASTIdentifier final : public ASTExpression {
    std::string_view str_;
};

template <typename Op>
struct ASTUnaryOp final : public ASTExpression {
    ASTExprVariant expr_;
};

template <typename Op>
struct ASTBinaryOp final : public ASTExpression {
    ASTExprVariant left_;
    ASTExprVariant right_;
};

struct ASTMemberAccess final : public ASTExpression {
    ASTExprVariant target_;
    std::span<std::string_view> members_;

    // public:
    //     ASTMemberAccess(
    //         const Location& loc, ExprVariant target, std::span<std::string_view> members
    //     ) noexcept
    //         : ASTExpression(loc), target_(target), members_(members) {}
    //     TermWithReceiver eval_term(
    //         TypeChecker& checker, const Type* expected, bool comptime
    //     ) const noexcept final {
    //         if (auto identifier = dynamic_cast<ASTIdentifier*>(target_)) {
    //             return try_namespace_access(checker, identifier->str_);
    //         } else {
    //             Term subject_term = target_->eval_term(checker, nullptr, comptime).subject;
    //             return eval_members(checker, subject_term, members_);
    //         }
    //     }
    //     TermWithReceiver try_namespace_access(
    //         TypeChecker& checker, std::string_view subject
    //     ) const noexcept {
    //         auto [_, subject_value] = checker.lookup(subject);
    //         auto scope_ptr = subject_value->get<const Scope*>();
    //         if (!scope_ptr) {
    //             return eval_members(checker, checker.lookup_term(subject), members_);
    //         }
    //         std::span<std::string_view> members = members_;
    //         while (!members.empty()) {
    //             std::string_view member = members.front();
    //             auto next = (*scope_ptr)[member];
    //             if (!next) {
    //                 throw UnlocatedProblem::make<UndeclaredIdentifierError>(member);
    //                 return {Term::unknown(), {}};
    //             }
    //             if (auto next_scope = next->get<const Scope*>()) {
    //                 scope_ptr = next_scope;
    //                 members = members.subspan(1);
    //             } else if (auto next_term = next->get<Term*>()) {
    //                 if (members.size()) {
    //                     return eval_members(checker, *next_term, members);
    //                 } else {
    //                     return {*next_term, {}};
    //                 }
    //             } else {
    //                 UNREACHABLE();
    //             }
    //         }
    //         /// TODO: evaluates to namespace is invalid
    //         throw;
    //     }

    //     TermWithReceiver eval_members(
    //         TypeChecker& checker, Term current_term, std::span<std::string_view> members
    //     ) const noexcept {
    //         Term subject;
    //         if (current_term.is_unknown()) {
    //             return {Term::unknown(), {}};
    //         }
    //         for (std::string_view member : members) {
    //             try {
    //                 subject = current_term;
    //                 current_term = checker.sema_.eval_access(current_term, member);
    //             } catch (UnlocatedProblem& e) {
    //                 e.report_at(location_);
    //                 return {Term::unknown(), {}};
    //             }
    //         }
    //         return {current_term, subject};
    //     }
};

struct ASTFieldInitialization final : public ASTNode {
    std::string_view identifier_;
    ASTExprVariant value_;
};

struct ASTStructInitialization final : public ASTExpression {
    ASTExprVariant struct_type_;
    std::span<ASTFieldInitialization> field_inits_;

    // Value* eval_comptime(TypeChecker& checker, const StructType* struct_type) const noexcept {
    //     GlobalMemory::Vector<std::pair<std::string_view, Value*>> inits =
    //         field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
    //             std::pair<std::string_view, Term> field = init->eval(checker);
    //             if (!field.second.is_comptime()) {
    //                 Diagnostic::report(NotConstantExpressionError(init->location_));
    //                 return std::pair<std::string_view, Value*>{
    //                     field.first, &UnknownValue::instance
    //                 };
    //             }
    //             return std::pair<std::string_view, Value*>{
    //                 field.first, field.second.get_comptime()
    //             };
    //         }) |
    //         GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, Value*>>>();
    //     GlobalMemory::Vector<std::pair<std::string_view, const Type*>> types =
    //         inits | std::views::transform([&](const auto& init) {
    //             return std::pair<std::string_view, const Type*>{
    //                 init.first, init.second->get_type()
    //             };
    //         }) |
    //         GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const
    //         Type*>>>();
    //     try {
    //         struct_type->validate(types);
    //     } catch (UnlocatedProblem& e) {
    //         e.report_at(location_);
    //         return &UnknownValue::instance;
    //     }
    //     return new StructValue(
    //         struct_type,
    //         inits | GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>()
    //     );
    // }

    // void check_fields(TypeChecker& checker, const StructType* struct_type) const noexcept {
    //     GlobalMemory::Vector<std::pair<std::string_view, const Type*>> inits =
    //         field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
    //             std::pair<std::string_view, Term> field = init->eval(checker);
    //             return std::pair<std::string_view, const Type*>{
    //                 field.first, field.second.effective_type()
    //             };
    //         }) |
    //         GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const
    //         Type*>>>();
    //     try {
    //         struct_type->validate(inits);
    //     } catch (UnlocatedProblem& e) {
    //         e.report_at(location_);
    //     }
    // }
};

struct ASTFunctionCall final : public ASTExpression {
    ASTExprVariant function_;
    std::span<ASTExprVariant> arguments_;
};

struct ASTPrimitiveType final : public ASTExplicitTypeExpr {
    const Type* type_;
};

struct ASTFunctionType final : public ASTExplicitTypeExpr {
    std::span<ASTExprVariant> parameter_types_;
    ASTExprVariant return_type_;
};

struct ASTFieldDeclaration final : public ASTNode {
    std::string_view identifier_;
    ASTExprVariant type_;
};

struct ASTStructType final : public ASTExplicitTypeExpr {
    std::span<ASTFieldDeclaration> fields_;
};

struct ASTMutableType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner_;
};

struct ASTReferenceType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner_;
    bool is_moved_;
};

struct ASTPointerType final : public ASTExplicitTypeExpr {
    ASTExprVariant inner_;
};

struct ASTTemplateInstantiation final : public ASTExpression {
    std::string_view template_name_;
    std::span<ASTExprVariant> arguments_;

    // public:
    //     ASTTemplateInstantiation(
    //         const Location& loc, std::string_view template_name, std::span<ASTExpression*>
    //         arguments
    //     ) noexcept
    //         : ASTExpression(loc), template_name_(template_name), arguments_(arguments) {}
    //     TermWithReceiver eval_term(
    //         TypeChecker& checker, const Type* expected, bool comptime
    //     ) const noexcept final {
    //         GlobalMemory::Vector<Term> args_terms =
    //             arguments_ | std::views::transform([&](ASTExpression* arg) {
    //                 return arg->eval_term(checker, nullptr, comptime).subject;
    //             }) |
    //             GlobalMemory::collect<GlobalMemory::Vector<Term>>();
    //         return {checker.lookup_term_instatiation(template_name_, args_terms), {}};
    //     }
    //     void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

    // protected:
    //     void do_accept(ASTVisitor& visitor) const final;

    // private:
    //     void do_eval_type(
    //         TypeChecker& checker, TypeResolution& out, bool require_complete
    //     ) const noexcept final {
    //         GlobalMemory::Vector<Term> args_terms =
    //             arguments_ | std::views::transform([&](ASTExpression* arg) {
    //                 return arg->eval_term(checker, nullptr, false).subject;
    //             }) |
    //             GlobalMemory::collect<GlobalMemory::Vector<Term>>();
    //         out = checker.lookup_type_instatiation(template_name_, args_terms);
    //     }
};

struct ASTTemplateMemberAccessInstantiation final : public ASTExpression {
    ASTExprVariant target_;
    std::string_view member_;
    std::span<ASTExprVariant> arguments_;

    // public:
    //     ASTTemplateMemberAccessInstantiation(
    //         const Location& loc,
    //         ASTExpression* target,
    //         std::string_view member,
    //         std::span<ASTExpression*> arguments
    //     ) noexcept
    //         : ASTExpression(loc), target_(target), member_(member), arguments_(arguments) {}
    //     TermWithReceiver eval_term(
    //         TypeChecker& checker, const Type* expected, bool comptime
    //     ) const noexcept final {
    //         // TODO
    //         return {Term::unknown(), {}};
    //     }

    // protected:
    //     void do_accept(ASTVisitor& visitor) const final;
    //     void do_eval_type(
    //         TypeChecker& checker, TypeResolution& out, bool require_complete
    //     ) const noexcept final {
    //         UNREACHABLE();
    //     }
};

struct ASTExpressionStatement final : public ASTNode {
    ASTExprVariant expr_;
};

struct ASTDeclaration final : public ASTNode {
    std::string_view identifier_;
    ASTExprVariant declared_type_;
    ASTExprVariant expr_;
    bool is_mutable_;
    bool is_constant_;

    // public:
    //     ASTDeclaration(
    //         const Location& loc,
    //         std::string_view identifier,
    //         ASTExpression* declared_type,
    //         ASTExpression* expr,
    //         bool is_mutable,
    //         bool is_constant
    //     ) noexcept
    //         : ASTRuntimeSymbolDeclaration(loc),
    //           identifier_(identifier),
    //           declared_type_(declared_type),
    //           expr_(expr),
    //           is_mutable_(is_mutable),
    //           is_constant_(is_constant) {
    //         assert(declared_type || expr);
    //         assert(!(is_mutable_ && is_constant_));
    //     }
    //     Term eval_init(TypeChecker& checker) const noexcept final {
    //         TypeResolution declared_type;
    //         if (declared_type_) {
    //             declared_type_->eval_type(checker, declared_type);
    //         }
    //         Term term = Term::lvalue(declared_type.get());
    //         if (expr_) {
    //             Term expr_term = expr_->eval_term(checker, declared_type, is_constant_).subject;
    //             if (is_constant_) {
    //                 term = Term::lvalue(expr_term.get_comptime());
    //             } else if (!declared_type_) {
    //                 term = Term::lvalue(expr_term.effective_type());
    //             }
    //         }
    //         return term;
    //     }
    //     void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

    // protected:
    //     void do_accept(ASTVisitor& visitor) const final;
};

struct ASTTypeAlias final : public ASTCompileTimeConstruct {
    std::string_view identifier_;
    ASTExprVariant type_;
};

struct ASTIfStatement final : public ASTNode {
    ASTExprVariant condition_;
    ASTLocalBlock* if_block_;
    ASTLocalBlock* else_block_;
};

struct ASTForStatement final : public ASTNode {
    ASTDeclaration* initializer_decl_;
    ASTExprVariant initializer_expr_;
    ASTExprVariant condition_;
    ASTExprVariant increment_;
    ASTLocalBlock* body_;
};

struct ASTContinueStatement final : public ASTNode {};

struct ASTBreakStatement final : public ASTNode {};

struct ASTReturnStatement final : public ASTNode {
    ASTExprVariant expr_;
};

struct ASTFunctionParameter final : public ASTNode {
    std::string_view identifier_;
    ASTExprVariant type_;
};

struct ASTFunctionDefinition final : public ASTNode {
    std::string_view identifier_;
    std::span<ASTFunctionParameter> parameters_;
    ASTExprVariant return_type_;
    std::span<ASTNodeVariant> body_;
    bool is_const_;
    bool is_static_;
    bool is_decl_only_;
    bool is_main_ = false;

    // FunctionObject get_func_obj(TypeChecker& checker) const noexcept {
    //     bool any_error = false;
    //     std::span params = parameters_ |
    //                        std::views::transform([&](ASTFunctionParameter* param) -> const Type*
    //                        {
    //                            TypeResolution param_type;
    //                            param->type_->eval_type(checker, param_type);
    //                            return param_type;
    //                        }) |
    //                        GlobalMemory::collect<std::span<const Type*>>();
    //     if (any_error) {
    //         return TypeRegistry::get_unknown();
    //     }
    //     TypeResolution return_type;
    //     return_type_->eval_type(checker, return_type);
    //     /// TODO: handle constexpr functions
    //     return TypeRegistry::get<FunctionType>(params, return_type);
    // }
};

struct ASTConstructorDestructorDefinition final : public ASTNode {
    bool is_constructor_;
    std::span<ASTFunctionParameter> parameters_;
    std::span<ASTNodeVariant> body_;

    // FunctionObject get_func_obj(TypeChecker& checker, const Type* owner_type) const noexcept {
    //     bool any_error = false;
    //     std::span params = parameters_ |
    //                        std::views::transform([&](ASTFunctionParameter* param) -> const Type*
    //                        {
    //                            TypeResolution param_type;
    //                            param->type_->eval_type(checker, param_type);
    //                            return param_type;
    //                        }) |
    //                        GlobalMemory::collect<std::span<const Type*>>();
    //     if (any_error) {
    //         return TypeRegistry::get_unknown();
    //     }
    //     return TypeRegistry::get<FunctionType>(params, owner_type);
    // }
};

// struct ASTClassSignature final : public ASTHiddenTypeExpr {
//     const ASTClassDefinition& owner_;

//     // const Type* resolve_base(TypeChecker& checker) const noexcept;
//     // std::span<const Type*> resolve_interfaces(TypeChecker& checker) const noexcept;
//     // FunctionOverloadSetValue* resolve_constructors(
//     //     TypeChecker& checker, const Type* owner_type
//     // ) const noexcept;
//     // FunctionObject resolve_destructor(TypeChecker& checker) const noexcept;
//     // GlobalMemory::FlatMap<std::string_view, const Type*> resolve_attrs(
//     //     TypeChecker& checker
//     // ) const noexcept;
//     // GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> resolve_methods(
//     //     TypeChecker& checker
//     // ) const noexcept;
// };

struct ASTClassDefinition final : public ASTCompileTimeConstruct {
    std::string_view identifier_;
    std::string_view extends_;
    std::span<std::string_view> implements_;
    std::span<ASTConstructorDestructorDefinition*> constructors_;
    ASTConstructorDestructorDefinition* destructor_;
    std::span<ASTDeclaration*> fields_;
    std::span<ASTTypeAlias*> aliases_;
    std::span<ASTFunctionDefinition*> functions_;
    std::span<ASTClassDefinition*> classes_;
};

struct ASTNamespaceDefinition final : public ASTCompileTimeConstruct {
    std::string_view identifier_;
    std::span<ASTNodeVariant> items_;
};

struct ASTTemplateParameter final : public ASTNode {
    bool is_nttp_;  // true if non-type template parameter, false if type template parameter
    std::string_view identifier_;
    ASTExprVariant constraint_;
    ASTExprVariant default_value_;
};

struct ASTTemplateDefinition final : public ASTCompileTimeConstruct {
    std::string_view identifier_;
    std::span<ASTTemplateParameter> parameters_;
    ASTNodeVariant target_node_;
};

struct ASTTemplateSpecialization final : public ASTNode {};

// ===================== Inline implementations =====================

// inline Scope& TemplateFamily::specialization_resolution(
//     TypeChecker& checker, std::span<Term> arguments
// ) const noexcept {
//     /// TODO: set correct origin as primary/specialization scopes
//     Scope& instantiation_scope = Scope::make_unlinked(*checker.current_scope(), nullptr);
//     for (const ASTTemplateSpecialization* specialization : specializations_) {
//         // if (specialization->match(checker, arguments)) {
//         //     specialization->instantiate(checker, arguments)->merge_into(out_scope);
//         //     return;
//         // }
//     }
//     for (const ASTTemplateParameter* param : primary_->parameters_) {
//         if (param->is_nttp_) {
//             TypeResolution constraint_type;
//             param->constraint_->eval_type(checker, constraint_type);
//             if (!constraint_type->assignable_from(arguments[0]->cast<Value>()->get_type())) {
//                 throw;
//                 return instantiation_scope;
//             }
//             instantiation_scope.add_template_argument(
//                 param->identifier_, Term::lvalue(arguments[0].get_comptime())
//             );
//         } else {
//             instantiation_scope.add_template_argument(
//                 param->identifier_, Term::type(arguments[0]->cast<Type>())
//             );
//         }
//     }
//     return instantiation_scope;
// }

// inline Term TemplateFamily::instantiate(
//     TypeChecker& checker, std::span<Term> arguments
// ) const noexcept {
//     if (arguments.size() != primary_->parameters_.size()) {
//         // Diagnostic::report(
//         //     TemplateArgumentCountMismatchError(location_, main_->parameters_.size(),
//         //     arguments.size())
//         // );
//         return Term::unknown();
//     }
//     Scope& template_scope = specialization_resolution(checker, arguments);
//     TypeChecker::Guard guard(checker, &template_scope);
//     // primary_->target_node_->collect_symbols(template_scope, checker.sema_);
//     // primary_->target_node_->check_types(checker);
//     // const ScopeValue* unevaluated = template_scope[primary_->identifier_];
//     // if (auto alias = unevaluated->get<const ASTExpression*>()) {
//     //     TypeResolution resolved;
//     //     alias->eval_type(checker, resolved);
//     //     return Term::type(resolved.get());
//     // } else {
//     //     /// TODO:
//     //     return Term::unknown();
//     // }
//     return checker.lookup_term(primary_->identifier_);
// }

/*

inline void ASTClassSignature::do_eval_type(
    TypeChecker& checker, TypeResolution& out, bool
) const noexcept {
    InstanceType* incomplete_class = new InstanceType(owner_->identifier_);
    out = incomplete_class;
    TypeChecker::Guard guard(checker, owner_);
    assert(checker.current_scope()->self_type_ == nullptr);
    checker.current_scope()->self_type_ = incomplete_class;
    const Type* base = resolve_base(checker);
    std::span<const Type*> interfaces = resolve_interfaces(checker);
    FunctionOverloadSetValue* constructors = resolve_constructors(checker, out);
    FunctionObject destructor = resolve_destructor(checker);
    GlobalMemory::FlatMap<std::string_view, const Type*> attrs = resolve_attrs(checker);
    GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> methods =
        resolve_methods(checker);
    TypeRegistry::get_at<InstanceType>(
        out,
        checker.current_scope(),
        owner_->identifier_,
        base,
        interfaces,
        constructors,
        destructor,
        std::move(attrs),
        std::move(methods)
    );
}

inline const Type* ASTClassSignature::resolve_base(TypeChecker& checker) const noexcept {
    if (owner_->extends_.empty()) {
        return nullptr;
    }
    TypeResolution result;
    try {
        result = checker.lookup_type(owner_->extends_);
    } catch (UnlocatedProblem& e) {
        e.report_at(owner_->location_);
        return TypeRegistry::get_unknown();
    }
    const Type* type = result;
    if (type->kind_ != Kind::Instance) {
        Diagnostic::report(TypeMismatchError(owner_->location_, "class", type->repr()));
        return TypeRegistry::get_unknown();
    }
    return type;
}

inline std::span<const Type*> ASTClassSignature::resolve_interfaces(
    TypeChecker& checker
) const noexcept {
    auto get_interface_type = [&](std::string_view interface_name) -> const Type* {
        TypeResolution result;
        try {
            result = checker.lookup_type(interface_name);
        } catch (UnlocatedProblem& e) {
            e.report_at(owner_->location_);
            return TypeRegistry::get_unknown();
        }
        const Type* type = result;
        if (type->kind_ != Kind::Interface) {
            Diagnostic::report(TypeMismatchError(owner_->location_, "interface", type->repr()));
            return TypeRegistry::get_unknown();
        }
        return type->cast<InterfaceType>();
    };
    return owner_->implements_ | std::views::transform(get_interface_type) |
           GlobalMemory::collect<std::span<const Type*>>();
}

inline FunctionOverloadSetValue* ASTClassSignature::resolve_constructors(
    TypeChecker& checker, const Type* owner_type
) const noexcept {
    return new FunctionOverloadSetValue(
        owner_->constructors_ | std::views::transform([&](const auto& ctor) {
            return ctor->get_func_obj(checker, owner_type);
        }) |
        GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>()
    );
}

inline FunctionObject ASTClassSignature::resolve_destructor(TypeChecker& checker) const noexcept {
    if (!owner_->destructor_) {
        return nullptr;
    }
    return owner_->destructor_->get_func_obj(checker, nullptr);
}

inline GlobalMemory::FlatMap<std::string_view, const Type*> ASTClassSignature::resolve_attrs(
    TypeChecker& checker
) const noexcept {
    return owner_->fields_ | std::views::transform([&](const auto& field_decl) {
               TypeResolution field_type;
               field_decl->declared_type_->eval_type(checker, field_type);
               return std::pair{field_decl->identifier_, field_type};
           }) |
           GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
}

inline GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*>
ASTClassSignature::resolve_methods(TypeChecker& checker) const noexcept {
    GlobalMemory::Vector non_static_functions =
        owner_->functions_ |
        std::views::filter([](const auto& func_def) { return !func_def->is_static_; }) |
        GlobalMemory::collect<GlobalMemory::Vector<const ASTFunctionDefinition*>>();
    std::ranges::sort(non_static_functions, [](const auto& a, const auto& b) {
        return a->identifier_ < b->identifier_;
    });
    std::ranges::unique(non_static_functions, [](const auto& a, const auto& b) {
        return a->identifier_ == b->identifier_;
    });
    return non_static_functions | std::views::transform([&](const auto& func_def) {
               Term result = checker.lookup_term(func_def->identifier_);
               return std::pair{
                   func_def->identifier_, result.get_comptime()->cast<FunctionOverloadSetValue>()
               };
           }) |
           GlobalMemory::collect<
               GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*>>();
}
*/
