#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "source.hpp"

class Transpiler;

class ASTNode;
class ASTCompileTimeConstruct;
class ASTRoot;
class ASTLocalBlock;
class ASTExpression;
class ASTConstant;
class ASTIdentifier;
template <typename Op>
class ASTUnaryOp;
template <typename Op>
class ASTBinaryOp;
class ASTFunctionCall;
class ASTPrimitiveType;
class ASTStructType;
class ASTRuntimeSymbolDeclaration;
class ASTDeclaration;
class ASTFieldDeclaration;
class ASTTypeAlias;
class ASTIfStatement;
class ASTForStatement;
class ASTContinueStatement;
class ASTBreakStatement;
class ASTReturnStatement;
class ASTFunctionParameter;
class ASTFunctionDefinition;
class ASTClassDefinition;
class ASTTemplateDefinition;
class ASTTemplateSpecialization;

class TemplateFamily;
class Scope;
class TypeChecker;

struct TermWithReceiver {
    Term subject;
    Term receiver;
};

using FunctionOverloadDecl =
    PointerVariant<const ASTFunctionDefinition*, const ASTTemplateDefinition*>;

using ScopeValue = PointerVariant<
    Term*,                                        // type/comptime (from template)
    const ASTExpression*,                         // type alias
    const ASTRuntimeSymbolDeclaration*,           // constant/variable/parameters
    GlobalMemory::Vector<FunctionOverloadDecl>*,  // function overloads
    TemplateFamily*,                              // template definition
    const Scope*>;                                // namespace

class TemplateFamily final : public GlobalMemory::MonotonicAllocated {
public:
    const ASTTemplateDefinition* primary_;
    GlobalMemory::Vector<const ASTTemplateSpecialization*> specializations_;

public:
    TemplateFamily(const ASTTemplateDefinition* primary) noexcept : primary_(primary) {}
    Scope& specialization_resolution(
        TypeChecker& checker, std::span<Term> arguments
    ) const noexcept;
    Term instantiate(TypeChecker& checker, std::span<Term> arguments) const noexcept;
};

class Scope final : public GlobalMemory::MonotonicAllocated {
    friend class TypeChecker;

public:
    static Scope& root(Scope& std_scope, const ASTNode* root) {
        Scope* scope = new Scope(nullptr, root);
        scope->add_namespace("std", std_scope);
        return *scope;
    }

    static Scope& make(Scope& parent, const ASTNode* origin) {
        Scope* scope = new Scope(&parent, origin);
        parent.children_.insert({origin, scope});
        return *scope;
    }

    static Scope& make_unlinked(Scope& parent, const ASTNode* origin) {
        Scope* scope = new Scope(&parent, origin);
        return *scope;
    }

private:
    GlobalMemory::FlatMap<const void*, Scope*> children_;
    GlobalMemory::FlatMap<std::string_view, ScopeValue> identifiers_;

public:
    Scope* const parent_ = nullptr;
    const ASTNode* const origin_ = nullptr;
    const Type* self_type_ = nullptr;

private:
    Scope(Scope* parent, const ASTNode* origin) noexcept : parent_(parent), origin_(origin) {}

public:
    Scope() noexcept = default;
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    void add_template_argument(std::string_view identifier, Term type) {
        auto [_, inserted] = identifiers_.insert({identifier, new Term(type)});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_alias(std::string_view identifier, const ASTExpression* expr) {
        auto [_, inserted] = identifiers_.insert({identifier, expr});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_variable(std::string_view identifier, const ASTRuntimeSymbolDeclaration* decl) {
        auto [_, inserted] = identifiers_.insert({identifier, decl});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_function(std::string_view identifier, const ASTFunctionDefinition* expr) {
        if (auto it = identifiers_.find(identifier); it == identifiers_.end()) {
            auto overloads = GlobalMemory::alloc<GlobalMemory::Vector<FunctionOverloadDecl>>();
            overloads->push_back(expr);
            identifiers_[identifier] = overloads;
        } else {
            auto overloads = it->second.get<GlobalMemory::Vector<FunctionOverloadDecl>*>();
            if (!overloads) {
                throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
            }
            overloads->push_back(expr);
        }
    }

    void add_template(std::string_view identifier, const ASTTemplateDefinition* definition) {
        auto [_, inserted] = identifiers_.insert({identifier, new TemplateFamily(definition)});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    void add_template(
        std::string_view identifier, const ASTTemplateSpecialization* specialization
    ) {
        auto it = identifiers_.find(identifier);
        if (it == identifiers_.end()) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        auto family = it->second.get<TemplateFamily*>();
        if (!family) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
        family->specializations_.push_back(specialization);
    }

    void add_namespace(std::string_view identifier, Scope& scope) {
        auto [_, inserted] = identifiers_.insert({identifier, &scope});
        if (!inserted) {
            throw UnlocatedProblem::make<RedeclaredIdentifierError>(identifier);
        }
    }

    const ScopeValue* operator[](std::string_view identifier) const noexcept {
        auto it = identifiers_.find(identifier);
        if (it != identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }
};

class DependencyGraph final {
public:
    struct Edge {
        const ASTNode* provider;
        const ASTNode* user;
    };
    struct EdgeLess {
        bool operator()(const Edge& lhs, const Edge& rhs) const noexcept {
            if (lhs.provider != rhs.provider) {
                return lhs.provider < rhs.provider;
            }
            return lhs.user < rhs.user;
        }
    };

private:
    GlobalMemory::FlatMap<const ASTNode*, GlobalMemory::FlatSet<Edge, EdgeLess>> origin_map_;

public:
    void add_edge(const ASTNode* origin, const ASTNode* user, const ASTNode* provider) noexcept {
        origin_map_[origin].insert({provider, user});
    }
    void add_edge(const ASTNode* origin, const ASTNode* user, ScopeValue provider) noexcept;
    bool has_origin(const ASTNode* origin) const noexcept { return origin_map_.contains(origin); }
    std::generator<const ASTNode*> iterate(const ASTNode* origin) const noexcept {
        assert(origin_map_.contains(origin));
        GlobalMemory::FlatMap<const ASTNode*, std::size_t> provider_count;
        for (const Edge& edge : origin_map_.at(origin)) {
            std::ignore = provider_count[edge.provider];
            provider_count[edge.user]++;
        }
        GlobalMemory::Vector<const ASTNode*> ready;
        for (auto [node, count] : provider_count) {
            if (count == 0) {
                ready.push_back(node);
            }
        }
        std::reverse(ready.begin(), ready.end());
        while (!ready.empty()) {
            const ASTNode* node = ready.back();
            ready.pop_back();
            co_yield node;
            for (const Edge& edge : origin_map_.at(origin)) {
                if (edge.provider == node) {
                    std::size_t& count = provider_count[edge.user];
                    count--;
                    if (count == 0) {
                        ready.push_back(edge.user);
                    }
                }
            }
        }
    }
};

class TypeChecker final {
public:
    class Guard final {
    private:
        TypeChecker& checker_;
        Scope* prev_;

    public:
        Guard(TypeChecker& checker, Scope* scope) noexcept
            : checker_(checker), prev_(std::exchange(checker.current_scope_, scope)) {}
        Guard(TypeChecker& checker, const void* child) noexcept
            : checker_(checker),
              prev_(
                  std::exchange(checker.current_scope_, checker.current_scope_->children_.at(child))
              ) {}
        ~Guard() noexcept { checker_.current_scope_ = prev_; }
    };

private:
    Scope* current_scope_;
    GlobalMemory::Map<std::pair<const Scope*, std::string_view>, TypeResolution> type_cache_;
    DependencyGraph& dep_graph_;
    // GlobalMemory::FlatMap<std::pair<const Scope*, const ASTExpression*>, Type*> ptr_cache_;

public:
    MemberAccessHandler& sema_;
    const ASTNode* current_node_ = nullptr;

public:
    TypeChecker(Scope& root, DependencyGraph& dep_graph, MemberAccessHandler& sema) noexcept
        : current_scope_(&root), dep_graph_(dep_graph), sema_(sema) {}

    void enter(const void* child) noexcept { current_scope_ = current_scope_->children_.at(child); }

    void exit() noexcept { current_scope_ = current_scope_->parent_; }

    Scope* current_scope() noexcept { return current_scope_; }

    bool is_at_top_level() const noexcept { return current_scope_->parent_ == nullptr; }

    const Type* self_type() const noexcept {
        auto current = current_scope_;
        do {
            if (current->self_type_) {
                return current->self_type_;
            }
            current = current->parent_;
        } while (current);
        return nullptr;
    }

    void add_node_order(const ASTNode* first, const ASTNode* second) {
        dep_graph_.add_edge(current_scope_->origin_, first, second);
    }

    const ScopeValue* operator[](std::string_view identifier) const noexcept {
        auto it = current_scope_->identifiers_.find(identifier);
        if (it != current_scope_->identifiers_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    std::pair<Scope*, const ScopeValue*> lookup(std::string_view identifier) const noexcept {
        Scope* scope = current_scope_;
        const ASTNode* user = current_node_;
        while (scope) {
            auto it = scope->identifiers_.find(identifier);
            if (it != scope->identifiers_.end()) {
                dep_graph_.add_edge(scope->origin_, user, it->second);
                return {scope, &it->second};
            }
            user = scope->origin_;
            scope = scope->parent_;
        }
        return {nullptr, nullptr};
    }

    /// TODO: injected class name by template
    TypeResolution lookup_type(std::string_view identifier);

    Term lookup_term(std::string_view identifier);

    TypeResolution lookup_type_instatiation(
        std::string_view identifier, std::span<Term> arguments
    ) {
        auto [scope, value] = lookup(identifier);
        if (!value) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        if (auto family = value->get<TemplateFamily*>()) {
            Guard guard(*this, scope);
            Term result = family->instantiate(*this, arguments);
            if (!result.is_type()) {
                throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
            }
            return result.get_type();
        } else {
            /// TODO: better error reporting
            throw;
        }
    }

    Term lookup_term_instatiation(std::string_view identifier, std::span<Term> arguments) {
        auto [scope, value] = lookup(identifier);
        if (!value) {
            throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
        }
        if (auto family = value->get<TemplateFamily*>()) {
            Guard guard(*this, scope);
            Term result = family->instantiate(*this, arguments);
            return result;
        } else {
            /// TODO: better error reporting
            throw;
        }
    }
};

class ASTNode : public GlobalMemory::MonotonicAllocated {
protected:
    static void check_types_loop(TypeChecker& checker, std::span<ASTNode*> nodes);

public:
    Location location_;
    ASTNode(const Location& loc) noexcept : location_(loc) {}

public:
    virtual ~ASTNode() noexcept = default;
    virtual void collect_symbols(Scope& scope, MemberAccessHandler& sema) {}
    void check_types(TypeChecker& checker) {
        const ASTNode* prev_node = std::exchange(checker.current_node_, this);
        do_check_types(checker);
        checker.current_node_ = prev_node;
    }
    virtual void transpile(Transpiler& transpiler, Cursor& cursor) const { UNREACHABLE(); };

private:
    virtual void do_check_types(TypeChecker& checker) {}
};

class ASTCompileTimeConstruct : public ASTNode {
    using ASTNode::ASTNode;
};

class ASTLocalBlock final : public ASTNode {
public:
    std::span<ASTNode*> statements_;

public:
    ASTLocalBlock(const Location& loc, std::span<ASTNode*> statements) noexcept
        : ASTNode(loc), statements_(statements) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        Scope& local_scope = Scope::make(scope, this);
        for (auto& stmt : statements_) {
            stmt->collect_symbols(local_scope, sema);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        checker.enter(this);
        check_types_loop(checker, statements_);
        checker.exit();
    }
};

class ASTRuntimeSymbolDeclaration : public ASTNode {
public:
    ASTRuntimeSymbolDeclaration(const Location& loc) noexcept : ASTNode(loc) {}
    virtual Term eval_init(TypeChecker& checker) const noexcept = 0;
};

class ASTRoot final : public ASTNode {
public:
    std::span<ASTNode*> statements_;
    ASTRoot(const Location& loc, std::span<ASTNode*> statements) noexcept
        : ASTNode(loc), statements_(statements) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        for (auto& child : statements_) {
            child->collect_symbols(scope, sema);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final { check_types_loop(checker, statements_); }
};

class ASTExpression : public ASTNode {
public:
    ASTExpression(const Location& loc) noexcept : ASTNode(loc) {}
    void eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete = true
    ) const noexcept {
        do_eval_type(checker, out, require_complete);
        assert(require_complete ? out.is_sized() : true);
    }
    virtual TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept = 0;

private:
    void do_check_types(TypeChecker& checker) final {
        std::ignore = eval_term(checker, nullptr, false).subject;
    }
    virtual void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept = 0;
};

class ASTExplicitTypeExpr : public ASTExpression {
public:
    ASTExplicitTypeExpr(const Location& loc) noexcept : ASTExpression(loc) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        TypeResolution type;
        eval_type(checker, type);
        return {Term::type(type.get()), {}};
    }
};

/// A hidden type expression that does not appear in source code
class ASTHiddenTypeExpr : public ASTExplicitTypeExpr {
public:
    ASTHiddenTypeExpr() noexcept : ASTExplicitTypeExpr({}) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTParenExpr final : public ASTExpression {
public:
    ASTExpression* inner_;

public:
    ASTParenExpr(const Location& loc, ASTExpression* inner) noexcept
        : ASTExpression(loc), inner_(inner) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        return inner_->eval_term(checker, expected, comptime);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        inner_->eval_type(checker, out, require_complete);
    }
};

class ASTConstant final : public ASTExpression {
public:
    const Value* value_;

public:
    template <ValueClass V>
    ASTConstant(const Location& loc, std::string_view str, std::type_identity<V>)
        : ASTExpression(loc), value_(Value::from_literal<V>(str)) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        if (expected) {
            try {
                Value* typed_value = value_->resolve_to(expected);
                return {Term::prvalue(typed_value), {}};
            } catch (UnlocatedProblem& e) {
                e.report_at(location_);
                return {Term::unknown(), {}};
            }
        } else {
            return {Term::prvalue(value_->resolve_to(nullptr)), {}};
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        /// TODO: return literal type
        out = {};
    }
};

class ASTSelfExpr final : public ASTExpression {
public:
    bool is_type_;

public:
    ASTSelfExpr(const Location& loc, bool is_type = false) noexcept
        : ASTExpression(loc), is_type_(is_type) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        if (is_type_) {
            return {Term::type(checker.self_type()), {}};
        } else {
            return {checker.lookup_term("self"), {}};
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        if (!is_type_) {
            Diagnostic::report(SymbolCategoryMismatchError(location_, false));
            out = TypeRegistry::get_unknown();
        } else {
            const Type* self_type = checker.self_type();
            if (!self_type) {
                /// TODO: throw not in class error
            }
            out = self_type;
        }
    }
};

class ASTIdentifier final : public ASTExpression {
public:
    const std::string_view str_;

public:
    ASTIdentifier(const Location& loc, std::string_view name) noexcept
        : ASTExpression(loc), str_(name) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        try {
            Term term = checker.lookup_term(str_);
            if (comptime && !term.is_comptime()) {
                Diagnostic::report(NotConstantExpressionError(location_));
                return {Term::unknown(), {}};
            }
            return {term, {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return {Term::unknown(), {}};
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        TypeResolution result = checker.lookup_type(str_);
        if (!result.is_sized()) {
            if (require_complete) {
                Diagnostic::report(CircularTypeDependencyError(location_));
                out = TypeRegistry::get_unknown();
                return;
            }
        }
        out = result;
    }
};

template <typename Op>
class ASTUnaryOp final : public ASTExpression {
public:
    ASTExpression* const expr_;

public:
    ASTUnaryOp(const Location& loc, ASTExpression* expr) noexcept
        : ASTExpression(loc), expr_(expr) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        Term expr_term = expr_->eval_term(checker, expected, comptime).subject;
        return {checker.sema_.eval_value_op(Op::opcode, expr_term), {}};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        TypeResolution expr_result;
        expr_->eval_type(checker, expr_result);
        try {
            out = TypeResolution(checker.sema_.eval_type_op(Op::opcode, expr_result));
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            out = TypeRegistry::get_unknown();
        }
    }
};

template <typename Op>
class ASTBinaryOp final : public ASTExpression {
public:
    ASTExpression* const left_;
    ASTExpression* const right_;

public:
    ASTBinaryOp(const Location& loc, ASTExpression* left, ASTExpression* right) noexcept
        : ASTExpression(loc), left_(left), right_(right) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        Term left_term = left_->eval_term(checker, expected, comptime).subject;
        Term right_term = right_->eval_term(checker, expected, comptime).subject;
        try {
            return {checker.sema_.eval_value_op(Op::opcode, left_term, right_term), {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return {Term::unknown(), {}};
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        TypeResolution left_result;
        left_->eval_type(checker, left_result);
        TypeResolution right_result;
        right_->eval_type(checker, right_result);
        try {
            out = checker.sema_.eval_type_op(Op::opcode, left_result, right_result);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            out = TypeRegistry::get_unknown();
        }
    }
};

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

class ASTMemberAccess final : public ASTExpression {
public:
    ASTExpression* target_;
    std::span<std::string_view> members_;

public:
    ASTMemberAccess(
        const Location& loc, ASTExpression* target, std::span<std::string_view> members
    ) noexcept
        : ASTExpression(loc), target_(target), members_(members) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        if (auto identifier = dynamic_cast<ASTIdentifier*>(target_)) {
            return try_namespace_access(checker, identifier->str_);
        } else {
            Term subject_term = target_->eval_term(checker, nullptr, comptime).subject;
            return eval_members(checker, subject_term, members_);
        }
    }

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        // TODO
    };

    TermWithReceiver try_namespace_access(
        TypeChecker& checker, std::string_view subject
    ) const noexcept {
        auto [_, subject_value] = checker.lookup(subject);
        auto scope_ptr = subject_value->get<const Scope*>();
        if (!scope_ptr) {
            return eval_members(checker, checker.lookup_term(subject), members_);
        }
        std::span<std::string_view> members = members_;
        while (!members.empty()) {
            std::string_view member = members.front();
            auto next = (*scope_ptr)[member];
            if (!next) {
                throw UnlocatedProblem::make<UndeclaredIdentifierError>(member);
                return {Term::unknown(), {}};
            }
            if (auto next_scope = next->get<const Scope*>()) {
                scope_ptr = next_scope;
                members = members.subspan(1);
            } else if (auto next_term = next->get<Term*>()) {
                if (members.size()) {
                    return eval_members(checker, *next_term, members);
                } else {
                    return {*next_term, {}};
                }
            } else {
                UNREACHABLE();
            }
        }
        /// TODO: evaluates to namespace is invalid
        throw;
    }

    TermWithReceiver eval_members(
        TypeChecker& checker, Term current_term, std::span<std::string_view> members
    ) const noexcept {
        Term subject;
        if (current_term.is_unknown()) {
            return {Term::unknown(), {}};
        }
        for (std::string_view member : members) {
            try {
                subject = current_term;
                current_term = checker.sema_.eval_access(current_term, member);
            } catch (UnlocatedProblem& e) {
                e.report_at(location_);
                return {Term::unknown(), {}};
            }
        }
        return {current_term, subject};
    }
};

class ASTFieldInitialization final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* value_;

public:
    ASTFieldInitialization(
        const Location& loc, std::string_view identifier, ASTExpression* value
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), value_(value) {}
    std::pair<std::string_view, Term> eval(TypeChecker& checker) const noexcept {
        Term value_term = value_->eval_term(checker, nullptr, false).subject;
        if (value_term.is_type()) {
            Diagnostic::report(SymbolCategoryMismatchError(location_, true));
            return {identifier_, Term::unknown()};
        }
        return {identifier_, value_term};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTStructInitialization final : public ASTExpression {
public:
    ASTExpression* struct_type_;
    std::span<ASTFieldInitialization*> field_inits_;

public:
    ASTStructInitialization(
        const Location& loc,
        ASTExpression* struct_type,
        std::span<ASTFieldInitialization*> field_inits
    ) noexcept
        : ASTExpression(loc), struct_type_(struct_type), field_inits_(field_inits) {}

    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        TypeResolution struct_type_res;
        struct_type_->eval_type(checker, struct_type_res);
        if (auto struct_type = struct_type_res->dyn_cast<StructType>()) {
            if (comptime) {
                return {Term::prvalue(eval_comptime(checker, struct_type)), {}};
            } else {
                check_fields(checker, struct_type);
                return {Term::prvalue(struct_type), {}};
            }
        } else {
            Diagnostic::report(
                TypeMismatchError(struct_type_->location_, "struct", struct_type_res->repr())
            );
            return {Term::unknown(), {}};
        }
    }

protected:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        UNREACHABLE();
    }

private:
    Value* eval_comptime(TypeChecker& checker, const StructType* struct_type) const noexcept {
        GlobalMemory::Vector<std::pair<std::string_view, Value*>> inits =
            field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
                std::pair<std::string_view, Term> field = init->eval(checker);
                if (!field.second.is_comptime()) {
                    Diagnostic::report(NotConstantExpressionError(init->location_));
                    return std::pair<std::string_view, Value*>{
                        field.first, &UnknownValue::instance
                    };
                }
                return std::pair<std::string_view, Value*>{
                    field.first, field.second.get_comptime()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, Value*>>>();
        GlobalMemory::Vector<std::pair<std::string_view, const Type*>> types =
            inits | std::views::transform([&](const auto& init) {
                return std::pair<std::string_view, const Type*>{
                    init.first, init.second->get_type()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const Type*>>>();
        try {
            struct_type->validate(types);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return &UnknownValue::instance;
        }
        return new StructValue(
            struct_type,
            inits | GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>()
        );
    }

    void check_fields(TypeChecker& checker, const StructType* struct_type) const noexcept {
        GlobalMemory::Vector<std::pair<std::string_view, const Type*>> inits =
            field_inits_ | std::views::transform([&](ASTFieldInitialization* init) {
                std::pair<std::string_view, Term> field = init->eval(checker);
                return std::pair<std::string_view, const Type*>{
                    field.first, field.second.effective_type()
                };
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<std::pair<std::string_view, const Type*>>>();
        try {
            struct_type->validate(inits);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTFunctionCall final : public ASTExpression {
public:
    ASTExpression* function_;
    std::span<ASTExpression*> arguments_;

public:
    ASTFunctionCall(
        const Location& loc, ASTExpression* function, std::span<ASTExpression*> arguments
    ) noexcept
        : ASTExpression(loc), function_(function), arguments_(arguments) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        bool any_error = false;
        GlobalMemory::Vector<Term> args_terms =
            arguments_ | std::views::transform([&](ASTExpression* arg) {
                Term arg_term = arg->eval_term(checker, nullptr, comptime).subject;
                any_error |= arg_term.is_unknown();
                return arg_term;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        auto [func, receiver] = function_->eval_term(checker, nullptr, comptime);
        if (func.is_type()) {
            Term instance = Term::lvalue(TypeRegistry::get<MutableType>(func.get_type()));
            args_terms.insert(args_terms.begin(), instance);
        } else if (receiver) {
            args_terms.insert(args_terms.begin(), receiver);
        }
        if (any_error) {
            return {Term::unknown(), {}};
        }
        try {
            return {checker.sema_.eval_call(func, args_terms), {}};
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
            return {Term::unknown(), {}};
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        // TODO
        out = {};
    }
};

class ASTPrimitiveType final : public ASTExplicitTypeExpr {
public:
    const Type* type_;

public:
    ASTPrimitiveType(const Location& loc, const Type* type) noexcept
        : ASTExplicitTypeExpr(loc), type_(type) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = type_;
    }
};

class ASTFunctionType final : public ASTExplicitTypeExpr {
public:
    std::span<ASTExpression*> parameter_types_;
    ASTExpression* return_type_;

public:
    ASTFunctionType(
        const Location& loc, std::span<ASTExpression*> parameter_types, ASTExpression* return_type
    ) noexcept
        : ASTExplicitTypeExpr(loc), parameter_types_(parameter_types), return_type_(return_type) {}

    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<FunctionType>();
        bool any_error = false;
        std::span<const Type*> param_types =
            parameter_types_ | std::views::transform([&](ASTExpression* param_expr) -> const Type* {
                TypeResolution param_type;
                param_expr->eval_type(checker, param_type);
                return param_type;
            }) |
            GlobalMemory::collect<std::span<const Type*>>();
        if (any_error) {
            out = TypeRegistry::get_unknown();
            return;
        }
        TypeResolution return_type;
        return_type_->eval_type(checker, return_type);
        TypeRegistry::get_at<FunctionType>(out, param_types, return_type);
    }
};

class ASTFieldDeclaration final : public ASTNode {
public:
    std::string_view identifier_;
    ASTExpression* type_;

public:
    ASTFieldDeclaration(
        const Location& loc, std::string_view identifier, ASTExpression* type
    ) noexcept
        : ASTNode(loc), identifier_(std::move(identifier)), type_(type) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTStructType final : public ASTExplicitTypeExpr {
public:
    std::span<ASTFieldDeclaration*> fields_;

public:
    ASTStructType(const Location& loc, std::span<ASTFieldDeclaration*> fields) noexcept
        : ASTExplicitTypeExpr(loc), fields_(fields) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<StructType>();
        GlobalMemory::FlatMap<std::string_view, const Type*> field_map =
            fields_ |
            std::views::transform(
                [&](ASTFieldDeclaration* decl) -> std::pair<std::string_view, const Type*> {
                    TypeResolution field_type;
                    decl->type_->eval_type(checker, field_type);
                    return {decl->identifier_, field_type};
                }
            ) |
            GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, const Type*>>();
        TypeRegistry::get_at<StructType>(out, field_map);
    }
};

class ASTMutableTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;

public:
    ASTMutableTypeExpr(const Location& loc, ASTExpression* expr) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<MutableType>();
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out, expr_type);
        }
        TypeRegistry::get_at<MutableType>(out, expr_type);
    }
};

class ASTReferenceTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;
    bool is_moved_;

public:
    ASTReferenceTypeExpr(const Location& loc, ASTExpression* expr, bool is_moved) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr), is_moved_(is_moved) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<ReferenceType>();
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out, expr_type);
        }
        TypeRegistry::get_at<ReferenceType>(out, expr_type, is_moved_);
    }
};

class ASTPointerTypeExpr final : public ASTExplicitTypeExpr {
public:
    ASTExpression* expr_;

public:
    ASTPointerTypeExpr(const Location& loc, ASTExpression* expr) noexcept
        : ASTExplicitTypeExpr(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final {
        out = std::type_identity<PointerType>();
        TypeResolution expr_type;
        expr_->eval_type(checker, expr_type, false);
        if (!expr_type.is_sized()) {
            TypeRegistry::add_ref_dependency(out, expr_type);
        }
        TypeRegistry::get_at<PointerType>(out, expr_type);
    }
};

class ASTTemplateInstantiation final : public ASTExpression {
public:
    std::string_view template_name_;
    std::span<ASTExpression*> arguments_;

public:
    ASTTemplateInstantiation(
        const Location& loc, std::string_view template_name, std::span<ASTExpression*> arguments
    ) noexcept
        : ASTExpression(loc), template_name_(template_name), arguments_(arguments) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        GlobalMemory::Vector<Term> args_terms =
            arguments_ | std::views::transform([&](ASTExpression* arg) {
                return arg->eval_term(checker, nullptr, comptime).subject;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        return {checker.lookup_term_instatiation(template_name_, args_terms), {}};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

protected:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        GlobalMemory::Vector<Term> args_terms =
            arguments_ | std::views::transform([&](ASTExpression* arg) {
                return arg->eval_term(checker, nullptr, false).subject;
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<Term>>();
        out = checker.lookup_type_instatiation(template_name_, args_terms);
    }
};

class ASTTemplateMemberAccessInstantiation final : public ASTExpression {
public:
    ASTExpression* target_;
    std::string_view member_;
    std::span<ASTExpression*> arguments_;

public:
    ASTTemplateMemberAccessInstantiation(
        const Location& loc,
        ASTExpression* target,
        std::string_view member,
        std::span<ASTExpression*> arguments
    ) noexcept
        : ASTExpression(loc), target_(target), member_(member), arguments_(arguments) {}
    TermWithReceiver eval_term(
        TypeChecker& checker, const Type* expected, bool comptime
    ) const noexcept final {
        // TODO
        return {Term::unknown(), {}};
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

protected:
    void do_eval_type(
        TypeChecker& checker, TypeResolution& out, bool require_complete
    ) const noexcept final {
        UNREACHABLE();
    }
};

class ASTExpressionStatement final : public ASTNode {
public:
    ASTExpression* const expr_;

public:
    ASTExpressionStatement(const Location& loc, ASTExpression* expr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final { expr_->check_types(checker); }
};

class ASTDeclaration final : public ASTRuntimeSymbolDeclaration {
public:
    std::string_view identifier_;
    ASTExpression* declared_type_;
    ASTExpression* expr_;
    bool is_mutable_;
    bool is_constant_;

public:
    ASTDeclaration(
        const Location& loc,
        std::string_view identifier,
        ASTExpression* declared_type,
        ASTExpression* expr,
        bool is_mutable,
        bool is_constant
    ) noexcept
        : ASTRuntimeSymbolDeclaration(loc),
          identifier_(identifier),
          declared_type_(declared_type),
          expr_(expr),
          is_mutable_(is_mutable),
          is_constant_(is_constant) {
        assert(declared_type || expr);
        assert(!(is_mutable_ && is_constant_));
    }
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        try {
            scope.add_variable(identifier_, this);
        } catch (UnlocatedProblem& e) {
            e.report_at(location_);
        }
    }
    Term eval_init(TypeChecker& checker) const noexcept final {
        TypeResolution declared_type;
        if (declared_type_) {
            declared_type_->eval_type(checker, declared_type);
        }
        Term term = Term::lvalue(declared_type.get());
        if (expr_) {
            Term expr_term = expr_->eval_term(checker, declared_type, is_constant_).subject;
            if (is_constant_) {
                term = Term::lvalue(expr_term.get_comptime());
            } else if (!declared_type_) {
                term = Term::lvalue(expr_term.effective_type());
            }
        }
        return term;
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final { checker.lookup(identifier_); }
};

class ASTTypeAlias final : public ASTCompileTimeConstruct {
public:
    std::string_view identifier_;
    ASTExpression* const type_;

public:
    ASTTypeAlias(const Location& loc, std::string_view identifier, ASTExpression* type) noexcept
        : ASTCompileTimeConstruct(loc), identifier_(std::move(identifier)), type_(type) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        scope.add_alias(identifier_, type_);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final { checker.lookup_type(identifier_); }
};

class ASTIfStatement final : public ASTNode {
public:
    ASTExpression* condition_;
    ASTLocalBlock* if_block_;
    ASTLocalBlock* else_block_;
    ASTIfStatement(
        const Location& loc,
        ASTExpression* condition,
        ASTLocalBlock* if_block,
        ASTLocalBlock* else_block
    ) noexcept
        : ASTNode(loc), condition_(condition), if_block_(if_block), else_block_(else_block) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        Scope& condition_scope = Scope::make(scope, this);
        if_block_->collect_symbols(condition_scope, sema);
        if (else_block_) {
            else_block_->collect_symbols(condition_scope, sema);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        TypeChecker::Guard guard(checker, this);
        std::ignore = condition_->eval_term(checker, &BooleanType::instance, false).subject;
        if_block_->check_types(checker);
        if (else_block_) {
            else_block_->check_types(checker);
        }
    }
};

class ASTForStatement final : public ASTNode {
public:
    ASTNode* const initializer_;  // Declaration or expression
    ASTExpression* const condition_;
    ASTExpression* const increment_;
    const std::span<ASTNode*> body_;
    ASTForStatement(
        const Location& loc,
        ASTDeclaration* initializer,
        ASTExpression* condition,
        ASTExpression* increment,
        std::span<ASTNode*> body
    ) noexcept
        : ASTNode(loc),
          initializer_(initializer),
          condition_(condition),
          increment_(increment),
          body_(body) {}
    ASTForStatement(
        const Location& loc,
        ASTExpression* initializer,
        ASTExpression* condition,
        ASTExpression* increment,
        std::span<ASTNode*> body
    ) noexcept
        : ASTNode(loc),
          initializer_(initializer),
          condition_(condition),
          increment_(increment),
          body_(body) {}
    ASTForStatement(const Location& loc, std::span<ASTNode*> body) noexcept
        : ASTNode(loc),
          initializer_(nullptr),
          condition_(nullptr),
          increment_(nullptr),
          body_(body) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        Scope& local_scope = Scope::make(scope, this);
        if (initializer_) {
            initializer_->collect_symbols(local_scope, sema);
        }
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, sema);
        }
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        TypeChecker::Guard guard(checker, this);
        if (initializer_) {
            initializer_->check_types(checker);
        }
        if (condition_) {
            std::ignore = condition_->eval_term(checker, &BooleanType::instance, false).subject;
        }
        if (increment_) {
            std::ignore = increment_->eval_term(checker, nullptr, false).subject;
        }
        check_types_loop(checker, body_);
    }
};

class ASTContinueStatement final : public ASTNode {
public:
    ASTContinueStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTContinueStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTBreakStatement final : public ASTNode {
public:
    ASTBreakStatement(const Location& loc) noexcept : ASTNode(loc) {}
    ~ASTBreakStatement() noexcept final = default;
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTReturnStatement final : public ASTNode {
public:
    ASTExpression* const expr_;

public:
    ASTReturnStatement(const Location& loc, ASTExpression* expr = nullptr) noexcept
        : ASTNode(loc), expr_(expr) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        if (expr_) std::ignore = expr_->eval_term(checker, nullptr, false).subject;
    }
};

class ASTFunctionParameter final : public ASTRuntimeSymbolDeclaration {
public:
    std::string_view identifier_;
    ASTExpression* type_;
    ASTFunctionParameter(
        const Location& loc, std::string_view identifier, ASTExpression* type
    ) noexcept
        : ASTRuntimeSymbolDeclaration(loc), identifier_(identifier), type_(type) {}
    Term eval_init(TypeChecker& checker) const noexcept final {
        TypeResolution param_type;
        type_->eval_type(checker, param_type);
        return Term::lvalue(param_type.get());
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
    void transpile_qualifiers(Transpiler& transpiler, Cursor& cursor) const noexcept;
};

class ASTFunctionDefinition final : public ASTCompileTimeConstruct {
public:
    std::string_view identifier_;
    std::span<ASTFunctionParameter*> parameters_;
    ASTExpression* return_type_;
    std::span<ASTNode*> body_;
    bool is_const_;
    bool is_static_;
    bool is_decl_only_;
    bool is_main_ = false;

public:
    ASTFunctionDefinition(
        const Location& loc,
        std::string_view identifier,
        std::span<ASTFunctionParameter*> parameters,
        ASTExpression* return_type,
        std::span<ASTNode*> body,
        bool is_const,
        bool is_static,
        bool is_decl_only
    ) noexcept
        : ASTCompileTimeConstruct(loc),
          identifier_(identifier),
          parameters_(parameters),
          return_type_(return_type),
          body_(body),
          is_const_(is_const),
          is_static_(is_static),
          is_decl_only_(is_decl_only) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        scope.add_function(identifier_, this);
        if (scope.self_type_ == nullptr ||
            (parameters_.size() ? parameters_[0]->identifier_ != "self" : true)) {
            is_static_ = true;
        }
        if (is_static_ && scope.parent_ == nullptr && identifier_ == "main") {
            is_main_ = true;
        }
        Scope& local_scope = Scope::make(scope, this);
        for (auto& param : parameters_) {
            local_scope.add_variable(param->identifier_, param);
        }
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, sema);
        }
    }
    FunctionObject get_func_obj(TypeChecker& checker) const noexcept {
        bool any_error = false;
        std::span params = parameters_ |
                           std::views::transform([&](ASTFunctionParameter* param) -> const Type* {
                               TypeResolution param_type;
                               param->type_->eval_type(checker, param_type);
                               return param_type;
                           }) |
                           GlobalMemory::collect<std::span<const Type*>>();
        if (any_error) {
            return TypeRegistry::get_unknown();
        }
        TypeResolution return_type;
        return_type_->eval_type(checker, return_type);
        /// TODO: handle constexpr functions
        return TypeRegistry::get<FunctionType>(params, return_type);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
    void transpile_definition(Transpiler& transpiler) const noexcept;
    void transpile_body(Transpiler& transpiler, Cursor& cursor) const noexcept;

private:
    void do_check_types(TypeChecker& checker) final {
        TypeChecker::Guard guard(checker, this);
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
    }
};

class ASTConstructorDestructorDefinition final : public ASTNode {
public:
    bool is_constructor_;
    std::span<ASTFunctionParameter*> parameters_;
    std::span<ASTNode*> body_;

public:
    ASTConstructorDestructorDefinition(
        const Location& loc,
        bool is_constructor,
        std::span<ASTFunctionParameter*> parameters,
        std::span<ASTNode*> body
    ) noexcept
        : ASTNode(loc), is_constructor_(is_constructor), parameters_(parameters), body_(body) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        Scope& local_scope = Scope::make(scope, this);
        for (auto& param : parameters_) {
            local_scope.add_variable(param->identifier_, param);
        }
        for (auto& stmt : body_) {
            stmt->collect_symbols(local_scope, sema);
        }
    }
    FunctionObject get_func_obj(TypeChecker& checker, const Type* owner_type) const noexcept {
        bool any_error = false;
        std::span params = parameters_ |
                           std::views::transform([&](ASTFunctionParameter* param) -> const Type* {
                               TypeResolution param_type;
                               param->type_->eval_type(checker, param_type);
                               return param_type;
                           }) |
                           GlobalMemory::collect<std::span<const Type*>>();
        if (any_error) {
            return TypeRegistry::get_unknown();
        }
        return TypeRegistry::get<FunctionType>(params, owner_type);
    }

    using ASTNode::transpile;
    void transpile(
        Transpiler& transpiler, Cursor& cursor, std::string_view classname
    ) const noexcept;
    void transpile_definition(Transpiler& transpiler, std::string_view classname) const noexcept;
    void transpile_body(Transpiler& transpiler, Cursor& cursor) const noexcept;

private:
    void do_check_types(TypeChecker& checker) final {
        TypeChecker::Guard guard(checker, this);
        for (auto& stmt : body_) {
            stmt->check_types(checker);
        }
    }
};

class ASTClassSignature final : public ASTHiddenTypeExpr {
public:
    const ASTClassDefinition* owner_;

public:
    ASTClassSignature(const ASTClassDefinition* owner) noexcept : owner_(owner) {}
    void do_eval_type(TypeChecker& checker, TypeResolution& out, bool) const noexcept final;

private:
    const Type* resolve_base(TypeChecker& checker) const noexcept;
    std::span<const Type*> resolve_interfaces(TypeChecker& checker) const noexcept;
    FunctionOverloadSetValue* resolve_constructors(
        TypeChecker& checker, const Type* owner_type
    ) const noexcept;
    FunctionObject resolve_destructor(TypeChecker& checker) const noexcept;
    GlobalMemory::FlatMap<std::string_view, const Type*> resolve_attrs(
        TypeChecker& checker
    ) const noexcept;
    GlobalMemory::FlatMap<std::string_view, FunctionOverloadSetValue*> resolve_methods(
        TypeChecker& checker
    ) const noexcept;
};

class ASTClassDefinition final : public ASTCompileTimeConstruct {
public:
    std::string_view identifier_;
    std::string_view extends_;
    std::span<std::string_view> implements_;
    std::span<ASTConstructorDestructorDefinition*> constructors_;
    ASTConstructorDestructorDefinition* destructor_;
    std::span<ASTDeclaration*> fields_;
    std::span<ASTTypeAlias*> aliases_;
    std::span<ASTFunctionDefinition*> functions_;
    std::span<ASTClassDefinition*> classes_;

public:
    ASTClassDefinition(
        const Location& loc,
        std::string_view identifier,
        std::string_view extends,
        std::span<std::string_view> implements,
        std::span<ASTConstructorDestructorDefinition*> constructors,
        ASTConstructorDestructorDefinition* destructor,
        std::span<ASTDeclaration*> fields,
        std::span<ASTTypeAlias*> aliases,
        std::span<ASTFunctionDefinition*> functions,
        std::span<ASTClassDefinition*> classes
    ) noexcept
        : ASTCompileTimeConstruct(loc),
          identifier_(identifier),
          extends_(extends),
          implements_(implements),
          constructors_(constructors),
          destructor_(destructor),
          fields_(fields),
          aliases_(aliases),
          functions_(functions),
          classes_(classes) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        auto signature = new ASTClassSignature(this);
        scope.add_alias(identifier_, signature);
        Scope& class_scope = Scope::make(scope, this);
        class_scope.self_type_ = reinterpret_cast<const Type*>(1);
        for (auto& ctor : constructors_) {
            ctor->collect_symbols(class_scope, sema);
        }
        if (destructor_) {
            destructor_->collect_symbols(class_scope, sema);
        }
        for (auto& func : functions_) {
            func->collect_symbols(class_scope, sema);
        }
        class_scope.self_type_ = nullptr;
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        checker.lookup_type(identifier_);  // trigger self type injection
        TypeChecker::Guard guard(checker, this);
        for (auto& field : fields_) {
            field->check_types(checker);
        }
        for (auto& ctor : constructors_) {
            ctor->check_types(checker);
        }
        if (destructor_) {
            destructor_->check_types(checker);
        }
        for (auto& func : functions_) {
            func->check_types(checker);
        }
    }
};

class ASTNamespaceDefinition final : public ASTCompileTimeConstruct {
public:
    std::string_view identifier_;
    std::span<ASTNode*> items_;
    ASTNamespaceDefinition(
        const Location& loc, std::string_view identifier, std::span<ASTNode*> items
    ) noexcept
        : ASTCompileTimeConstruct(loc), identifier_(identifier), items_(items) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        Scope& namespace_scope = Scope::make(scope, this);
        for (auto& item : items_) {
            item->collect_symbols(namespace_scope, sema);
        }
        scope.add_namespace(identifier_, namespace_scope);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;

private:
    void do_check_types(TypeChecker& checker) final {
        TypeChecker::Guard guard(checker, this);
        check_types_loop(checker, items_);
    }
};

class ASTTemplateParameter final : public ASTNode {
public:
    bool is_nttp_;  // true if non-type template parameter, false if type template parameter
    std::string_view identifier_;
    const ASTExpression* constraint_;
    const ASTExpression* default_value_;

public:
    ASTTemplateParameter(
        const Location& loc,
        bool is_nttp,
        std::string_view identifier,
        const ASTExpression* constraint,
        const ASTExpression* default_value
    ) noexcept
        : ASTNode(loc),
          is_nttp_(is_nttp),
          identifier_(identifier),
          constraint_(constraint),
          default_value_(default_value) {}
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTTemplateDefinition final : public ASTCompileTimeConstruct {
public:
    std::string_view identifier_;
    std::span<ASTTemplateParameter*> parameters_;
    ASTNode* target_node_;

public:
    ASTTemplateDefinition(
        const Location& loc,
        std::string_view identifier,
        std::span<ASTTemplateParameter*> parameters,
        ASTNode* target_node
    ) noexcept
        : ASTCompileTimeConstruct(loc),
          identifier_(identifier),
          parameters_(parameters),
          target_node_(target_node) {}
    void collect_symbols(Scope& scope, MemberAccessHandler& sema) final {
        scope.add_template(identifier_, this);
        Scope& template_scope = Scope::make(scope, this);
        target_node_->collect_symbols(template_scope, sema);
    }
    void transpile(Transpiler& transpiler, Cursor& cursor) const noexcept final;
};

class ASTTemplateSpecialization final : public ASTNode {};

// ===================== Inline implementations =====================

inline Scope& TemplateFamily::specialization_resolution(
    TypeChecker& checker, std::span<Term> arguments
) const noexcept {
    /// TODO: set correct origin as primary/specialization scopes
    Scope& instantiation_scope = Scope::make_unlinked(*checker.current_scope(), nullptr);
    for (const ASTTemplateSpecialization* specialization : specializations_) {
        // if (specialization->match(checker, arguments)) {
        //     specialization->instantiate(checker, arguments)->merge_into(out_scope);
        //     return;
        // }
    }
    for (const ASTTemplateParameter* param : primary_->parameters_) {
        if (param->is_nttp_) {
            TypeResolution constraint_type;
            param->constraint_->eval_type(checker, constraint_type);
            if (!constraint_type->assignable_from(arguments[0]->cast<Value>()->get_type())) {
                throw;
                return instantiation_scope;
            }
            instantiation_scope.add_template_argument(
                param->identifier_, Term::lvalue(arguments[0].get_comptime())
            );
        } else {
            instantiation_scope.add_template_argument(
                param->identifier_, Term::type(arguments[0]->cast<Type>())
            );
        }
    }
    return instantiation_scope;
}

inline Term TemplateFamily::instantiate(
    TypeChecker& checker, std::span<Term> arguments
) const noexcept {
    if (arguments.size() != primary_->parameters_.size()) {
        // Diagnostic::report(
        //     TemplateArgumentCountMismatchError(location_, main_->parameters_.size(),
        //     arguments.size())
        // );
        return Term::unknown();
    }
    Scope& template_scope = specialization_resolution(checker, arguments);
    TypeChecker::Guard guard(checker, &template_scope);
    primary_->target_node_->collect_symbols(template_scope, checker.sema_);
    primary_->target_node_->check_types(checker);
    // const ScopeValue* unevaluated = template_scope[primary_->identifier_];
    // if (auto alias = unevaluated->get<const ASTExpression*>()) {
    //     TypeResolution resolved;
    //     alias->eval_type(checker, resolved);
    //     return Term::type(resolved.get());
    // } else {
    //     /// TODO:
    //     return Term::unknown();
    // }
    return checker.lookup_term(primary_->identifier_);
}

inline void DependencyGraph::add_edge(
    const ASTNode* origin, const ASTNode* user, ScopeValue provider
) noexcept {
    if (auto alias = provider.get<const ASTExpression*>()) {
        origin_map_[origin].insert({alias, user});
    } else if (auto decl = provider.get<const ASTRuntimeSymbolDeclaration*>()) {
        origin_map_[origin].insert({decl, user});
    } else if (auto func = provider.get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
        /// TODO:
        // origin_map_[origin].insert({user, func});
    } else if (auto temp = provider.get<TemplateFamily*>()) {
        origin_map_[origin].insert({temp->primary_, user});
        for (const ASTTemplateSpecialization* specialization : temp->specializations_) {
            origin_map_[origin].insert({temp->primary_, specialization});
            origin_map_[origin].insert({specialization, user});
        }
    } else if (auto scope = provider.get<const Scope*>()) {
        origin_map_[origin].insert({scope->origin_, user});
    }
}

inline TypeResolution TypeChecker::lookup_type(std::string_view identifier) {
    auto [scope, value] = lookup(identifier);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    } else if (auto term = value->get<Term*>()) {
        if (!term->is_type()) {
            throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
        }
        return term->get_type();
    } else if (!value->get<const ASTExpression*>()) {
        /// TODO: template instantiation
        throw UnlocatedProblem::make<SymbolCategoryMismatchError>(true);
    }
    // Check cache
    auto [it_id_cache, inserted] = type_cache_.insert({{scope, identifier}, TypeResolution()});
    if (!inserted) {
        return it_id_cache->second;
    }
    // Cache miss; resolve
    auto type_alias = value->get<const ASTExpression*>();
    Guard guard(*this, scope);
    const ASTNode* prev_node = std::exchange(current_node_, type_alias);
    type_alias->eval_type(*this, it_id_cache->second);
    current_node_ = prev_node;
    // Ignore incomplete types in cache to prevent type interning bypassed
    if (TypeRegistry::is_type_incomplete(it_id_cache->second)) {
        const Type* incomplete_type = it_id_cache->second;
        type_cache_.erase(it_id_cache);
        return incomplete_type;
    } else {
        return it_id_cache->second;
    }
}

inline Term TypeChecker::lookup_term(std::string_view identifier) {
    auto [scope, value] = lookup(identifier);
    if (!scope) {
        throw UnlocatedProblem::make<UndeclaredIdentifierError>(identifier);
    }
    if (auto term = value->get<Term*>()) {
        return *term;
    } else if (auto alias = value->get<const ASTExpression*>()) {
        if (auto it = type_cache_.find({scope, identifier}); it != type_cache_.end()) {
            return Term::type(it->second);
        }
        Guard guard(*this, scope);
        TypeResolution out;
        alias->eval_type(*this, out);
        return Term::type(out);
    } else if (auto decl = value->get<const ASTRuntimeSymbolDeclaration*>()) {
        return decl->eval_init(*this);
    } else if (auto func = value->get<GlobalMemory::Vector<FunctionOverloadDecl>*>()) {
        Guard guard(*this, scope);
        GlobalMemory::Vector<FunctionObject> func_vec =
            *func | std::views::transform([&](const FunctionOverloadDecl& func_decl) {
                if (auto func_def = func_decl.get<const ASTFunctionDefinition*>()) {
                    return func_def->get_func_obj(*this);
                } else {
                    /// TODO: handle template functions
                    return FunctionObject{};
                }
            }) |
            GlobalMemory::collect<GlobalMemory::Vector<FunctionObject>>();
        return Term::prvalue(new FunctionOverloadSetValue(func_vec));
    } else {
        /// TODO: throw
        assert(false);
    }
}

inline void ASTNode::check_types_loop(TypeChecker& checker, std::span<ASTNode*> children) {
    ASTNode* prev_node = nullptr;
    for (ASTNode* node : children) {
        node->check_types(checker);
        if (prev_node) {
            checker.add_node_order(node, prev_node);
        }
        if (!dynamic_cast<ASTCompileTimeConstruct*>(node)) {
            prev_node = node;
        }
    }
}

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
