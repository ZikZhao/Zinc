#include "pch.hpp"

#include "ast.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "runtime-str.hpp"
#include "source.hpp"

/// TODO: put is_type_context in transpiler.state

inline constexpr std::string_view no_lint_pragma = "// NOLINTNEXTLINE(misc-definitions-in-headers)";

enum class Section {
    Includes,
    StructuralDeclarations,
    Main,
    Implementation,
    __len__,
};

class BufferTree {
public:
    struct Node : public GlobalMemory::MonotonicAllocated {
        GlobalMemory::String content;
        GlobalMemory::Vector<const Node*> children;
        const void* scope;
        Node* parent;
    };

public:
    static inline const Node newline{.content = "", .scope = nullptr, .parent = nullptr};

private:
    Node* root_;
    GlobalMemory::FlatMap<const void*, Node*> scope_map_;

public:
    BufferTree() noexcept : root_{new Node{.scope = nullptr, .parent = nullptr}} {}

    Node& root() noexcept { return *root_; }
    const Node& root() const noexcept { return *root_; }

    void add(const void* scope, Node* node) noexcept {
        assert(scope != nullptr);
        scope_map_.insert({scope, node});
    }

    Node& find(const Scope* scope) noexcept {
        auto get_path = [](this auto&& self, const Scope* current) -> std::generator<const void*> {
            if (current->parent()) {
                for (const void* step : self(current->parent())) {
                    co_yield step;
                }
            }
            co_yield current;
        };
        Node* current = root_;
        for (const void* step : get_path(scope)) {
            auto it = scope_map_.find(step);
            if (it == scope_map_.end()) {
                Node* new_node = new Node{.scope = step, .parent = current};
                scope_map_.insert({step, new_node});
                current = new_node;
            } else {
                current = it->second;
            }
        }
        return *current;
    }
};

class Cursor {
private:
    using Node = BufferTree::Node;

private:
    BufferTree& tree_;
    Node* target_;

public:
    explicit Cursor(BufferTree& tree) noexcept
        : tree_(tree), target_(new Node{.scope = nullptr, .parent = &tree.root()}) {}

    Cursor(BufferTree& tree, Node& target) noexcept : tree_(tree), target_(&target) {}

    Cursor(const Cursor& other) noexcept
        : tree_(other.tree_),
          target_(new Node{.scope = other.target_->scope, .parent = other.target_->parent}) {}

    ~Cursor() noexcept { commit(); }

    Cursor& operator<<(auto&& args) noexcept {
        std::format_to(
            std::back_inserter(target_->content), "{}", std::forward<decltype(args)>(args)
        );
        return *this;
    }

    Cursor& commit() noexcept {
        if (!target_->content.empty()) {
            target_->parent->children.push_back(target_);
            target_ = new Node{.scope = nullptr, .parent = target_->parent};
        }
        return *this;
    }

    Cursor open_child(const void* scope) noexcept {
        target_->scope = scope;
        Node* new_child = new Node{.scope = nullptr, .parent = target_};
        tree_.add(scope, target_);
        return Cursor(tree_, *new_child);
    }

    Cursor& newline() noexcept {
        assert(target_->content.empty());
        target_->parent->children.push_back(&BufferTree::newline);
        return *this;
    }

    Cursor& collapse(std::string_view trailing) noexcept {
        if (target_->children.empty()) {
            *this << trailing;
        } else {
            commit() << trailing;
        }
        return *this;
    }

    void clear() noexcept { target_->content.clear(); }
};

class Transpiler {
    friend class SectionWriter;

public:
    struct State {
        bool mangle_structural_identifiers = false;
        GlobalMemory::FlatSet<std::string_view> niebloids;
        GlobalMemory::FlatMap<const StructType*, std::size_t> structurals;
        GlobalMemory::FlatSet<const ASTExpression*> types_visited;
    };

private:
    const SourceManager::File& file_;
    std::array<BufferTree, static_cast<std::size_t>(Section::__len__)> sections_;
    TypeChecker& checker_;

public:
    State state_;

public:
    Transpiler(const SourceManager::File& file, TypeChecker& checker)
        : file_(file), checker_(checker) {}

    Cursor root() noexcept { return Cursor(sections_[static_cast<std::size_t>(Section::Main)]); }

    Cursor section(Section section) noexcept {
        return Cursor(sections_[static_cast<std::size_t>(section)]);
    }

    void require_definition(std::string_view identifier) noexcept {
        auto [scope, symbol] = checker_.lookup(identifier);
        assert(scope != nullptr && symbol != nullptr);
        BufferTree& main = sections_[static_cast<std::size_t>(Section::Main)];
        Cursor cursor(main, main.find(scope));
        symbol->get<const ASTExpression*>()->transpile(*this, cursor);
        cursor.clear();
    }

    TypeChecker& checker() noexcept { return checker_; }

    void flush() {
        const GlobalMemory::String stem =
            (std::filesystem::path("out") / file_.path)
                .stem()
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::fstream stream(GlobalMemory::format("out/{}.hpp", stem).c_str(), std::ios::out);
        stream << "#pragma once";
        output(stream, Section::Includes, "Includes");
        output(stream, Section::StructuralDeclarations, "Structural Declarations");
        output(stream, Section::Main, "Main");
        output(stream, Section::Implementation, "Implementation");
        stream << "\n";
    }

private:
    void output(std::ostream& os, Section section, std::string_view title) const noexcept {
        const BufferTree& tree = sections_[static_cast<std::size_t>(section)];
        if (tree.root().children.empty()) {
            return;
        }
        os << "\n\n/* ---------- " << title << " ---------- */\n\n";
        const char* sep = "";
        for (const BufferTree::Node* child : tree.root().children) {
            os << sep;
            output(os, *child, 0);
            sep = (child->children.size() == 0 && child->content != no_lint_pragma) ? "\n\n" : "\n";
        }
    }

    void output(std::ostream& os, const BufferTree::Node& node, std::size_t indent) const noexcept {
        for (std::size_t i = 0; i < indent; i++) {
            os << "    ";
        }
        os << node.content;
        for (const BufferTree::Node* child : node.children) {
            os << "\n";
            output(os, *child, indent + 1);
        }
    }
};

inline void precompile_headers() {
    std::fstream pch_file = std::fstream("out/pch.hpp", std::ios::in);
    pch_file.seekg(0, std::ios::end);
    std::size_t size = static_cast<std::size_t>(pch_file.tellg());
    if (size == runtime_hpp_str().size()) {
        GlobalMemory::String content;
        content.resize(size);
        pch_file.seekg(0, std::ios::beg);
        pch_file.read(content.data(), static_cast<std::streamsize>(size));
        if (content == runtime_hpp_str()) {
            Diagnostic::message("Precompiled header is up to date");
            return;
        }
    }

    pch_file.close();
    pch_file.open("out/pch.hpp", std::ios::out | std::ios::trunc);
    pch_file << runtime_hpp_str();
    pch_file.close();
    const char* pch_command = "g++ -std=c++20 -x c++-header -I out out/pch.hpp -o out/pch.hpp.gch";
    Diagnostic::message(GlobalMemory::format_view("Compiling precompiled header: {}", pch_command));
    if (std::system(pch_command) != 0) {
        Diagnostic::error("Failed to precompile headers");
    }
}

inline int transpile_all(ASTNode* root, SourceManager& sources, TypeChecker& checker) {
    try {
        std::filesystem::create_directory("out");
    } catch (const std::filesystem::filesystem_error& e) {
        Diagnostic::error(
            GlobalMemory::format_view("Failed to create output directory './out': {}", e.what())
        );
    }

    Transpiler transpiler(sources[0], checker);
    Cursor cursor = transpiler.root();
    root->transpile(transpiler, cursor);
    cursor.commit();
    transpiler.flush();
    Diagnostic::message(
        GlobalMemory::format_view("Transformed {} modules to './out'", sources.files.size() - 1)
    );

    precompile_headers();

    const GlobalMemory::String main_stem =
        std::filesystem::path(sources[0].path)
            .stem()
            .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
    GlobalMemory::String compile_command = GlobalMemory::format(
        "g++ -std=c++20 -x c++ -I out -include \"out/{}.hpp\" /dev/null -o \"out/{}\"",
        main_stem,
        main_stem
    );
    Diagnostic::message(
        GlobalMemory::format_view(
            "Compiling output executable to './out/{}': {}", main_stem, compile_command
        )
    );
    if (std::system(GlobalMemory::String(compile_command).c_str()) != 0) {
        Diagnostic::error("Failed to compile output executable");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    transpiler.section(Section::Includes) << "#include \"pch.hpp\"";
    for (const auto& child : statements_) {
        child->transpile(transpiler, cursor);
        cursor.commit();
    }
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor.commit() << "{";
    transpiler.checker().enter(this);
    Cursor local_cursor = cursor.open_child(this);
    for (const auto& stmt : statements_) {
        stmt->transpile(transpiler, local_cursor);
        local_cursor.commit();
    }
    transpiler.checker().exit();
    cursor << "}";
}

inline void ASTHiddenTypeExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    UNREACHABLE();
}

inline void ASTParenExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "(";
    inner_->transpile(transpiler, cursor);
    cursor << ")";
}

inline void ASTConstant::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    switch (value_->kind_) {
    case Kind::Nullptr:
        cursor << "nullptr";
        break;
    case Kind::Integer:
        cursor << value_->cast<IntegerValue>()->value_.to_string();
        break;
    case Kind::Float:
        cursor << GlobalMemory::format_view("0x{:a}", value_->cast<FloatValue>()->value_);
        break;
    case Kind::Boolean:
        cursor << (value_->cast<BooleanValue>()->value_ ? "true" : "false");
        break;
    default:
        UNREACHABLE();
    }
}

inline void ASTSelfExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << (is_type_ ? "decltype(*this)" : "this");
}

inline void ASTIdentifier::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    if (!transpiler.state_.mangle_structural_identifiers) {
        cursor << str_;
        return;
    }

    TypeResolution type = transpiler.checker().lookup_type(str_);
    {
        // SectionWriter void_writer = transpiler[Section::Void];
        transpiler.require_definition(str_);
    }
    if (type->dyn_cast<StructType>()) {
        cursor << GlobalMemory::format_view(
            "$structural_{}", transpiler.state_.structurals.at(type->cast<StructType>())
        );
    } else {
        cursor << str_;
    }
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    /// TODO: handle prefix/postfix
    cursor << OperatorCodeToString(Op::opcode);
    expr_->transpile(transpiler, cursor);
}

template <typename Op>
inline void ASTBinaryOp<Op>::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    left_->transpile(transpiler, cursor);
    cursor << " " << OperatorCodeToString(Op::opcode) << " ";
    right_->transpile(transpiler, cursor);
}

inline void ASTMemberAccess::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    if (auto self = dynamic_cast<ASTSelfExpr*>(target_); self && !self->is_type_) {
        cursor << "this->" << member_;
        return;
    } else if (auto identifier = dynamic_cast<ASTIdentifier*>(target_)) {
        const ScopeValue* symbol = transpiler.checker().lookup(identifier->str_).second;
        cursor << identifier->str_ << (symbol && symbol->get<const Scope*>() ? "::" : ".")
               << member_;
    } else {
        target_->transpile(transpiler, cursor);
        cursor << "." << member_;
    }
}

inline void ASTFieldInitialization::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << "." << identifier_ << " = ";
    value_->transpile(transpiler, cursor);
}

inline void ASTStructInitialization::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    struct_type_->transpile(transpiler, cursor);
    cursor << " {";
    const char* sep = "";
    for (const auto& field : field_inits_) {
        cursor << sep;
        field->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << "}";
}

inline void ASTFunctionCall::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    function_->transpile(transpiler, cursor);
    cursor << "(";
    const char* sep = "";
    for (const auto& arg : arguments_) {
        cursor << sep;
        arg->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ")";
}

inline void ASTPrimitiveType::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    switch (type_->kind_) {
    case Kind::Any:
        cursor << "std::any";
        break;
    case Kind::Integer:
        cursor << "std::";
        if (type_->cast<IntegerType>()->is_signed_) {
            cursor << "int";
        } else {
            cursor << "uint";
        }
        switch (type_->cast<IntegerType>()->bits_) {
        case 8:
            cursor << "8_t";
            break;
        case 16:
            cursor << "16_t";
            break;
        case 32:
            cursor << "32_t";
            break;
        case 64:
            cursor << "64_t";
            break;
        default:
            UNREACHABLE();
        }
        break;
    case Kind::Float:
        switch (type_->cast<FloatType>()->bits_) {
        case 32:
            cursor << "float";
            break;
        case 64:
            cursor << "double";
            break;
        default:
            UNREACHABLE();
        }
        break;
    case Kind::Boolean:
        cursor << "bool";
        break;
    default:
        UNREACHABLE();
    }
}

inline void ASTFunctionType::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "std::function<";
    return_type_->transpile(transpiler, cursor);
    cursor << "(";
    const char* sep = "";
    for (const auto& param_type : parameter_types_) {
        cursor << sep;
        param_type->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ")>";
}

inline void ASTFieldDeclaration::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    type_->transpile(transpiler, cursor);
    cursor << " " << identifier_ << ";";
}

inline void ASTStructType::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    TypeResolution struct_type;
    eval_type(transpiler.checker(), struct_type);
    auto [it, inserted] =
        transpiler.state_.structurals.insert({static_cast<const StructType*>(struct_type), 0});
    if (inserted) {
        it->second = transpiler.state_.structurals.size();
    }
    GlobalMemory::String struct_name = GlobalMemory::format("$structural_{}", it->second);
    cursor << struct_name;

    // Insert structural definition if not defined yet
    if (inserted) {
        transpiler.section(Section::StructuralDeclarations) << "struct " << struct_name << ";";
        Cursor def_cursor = cursor;
        def_cursor << "struct " << struct_name << " {";
        bool prev_state = std::exchange(transpiler.state_.mangle_structural_identifiers, true);
        for (const auto& field : fields_) {
            Cursor field_cursor = def_cursor.open_child(field);
            field->transpile(transpiler, field_cursor);
        }
        transpiler.state_.mangle_structural_identifiers = prev_state;
        def_cursor.collapse("};");
    }
}

inline void ASTMutableTypeExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    expr_->transpile(transpiler, cursor);
}

inline void ASTReferenceTypeExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    expr_->transpile(transpiler, cursor);
    cursor << "&";
}

inline void ASTPointerTypeExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    expr_->transpile(transpiler, cursor);
    cursor << "*";
}

inline void ASTTemplateInstantiation::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << template_name_ << "<";
    const char* sep = "";
    for (const auto& arg : arguments_) {
        cursor << sep;
        arg->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ">";
}

inline void ASTExpressionStatement::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    expr_->transpile(transpiler, cursor);
    cursor << ";";
}

inline void ASTDeclaration::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    if (is_constant_) {
        cursor << "constexpr ";
    } else if (!is_mutable_) {
        cursor << "const ";
    }
    if (declared_type_) {
        declared_type_->transpile(transpiler, cursor);
    } else {
        cursor << "auto";
    }
    cursor << " " << identifier_ << " = ";
    expr_->transpile(transpiler, cursor);
    cursor << ";";
}

inline void ASTTypeAlias::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "using " << identifier_ << " = ";
    type_->transpile(transpiler, cursor);
    cursor << ";";
}

inline void ASTIfStatement::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "if (";
    condition_->transpile(transpiler, cursor);
    cursor << ") {";
    transpiler.checker().enter(&if_block_);
    for (const auto& stmt : if_block_) {
        Cursor then_cursor = cursor.open_child(this);
        stmt->transpile(transpiler, then_cursor);
    }
    transpiler.checker().exit();
    cursor.commit() << "}";
    if (!else_block_.empty()) {
        cursor << " else {";
        transpiler.checker().enter(&else_block_);
        for (const auto& stmt : else_block_) {
            Cursor else_cursor = cursor.open_child(this);
            stmt->transpile(transpiler, else_cursor);
        }
        transpiler.checker().exit();
        cursor.commit() << "}";
    }
}

inline void ASTForStatement::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "for (";
    initializer_->transpile(transpiler, cursor);
    cursor << "; ";
    condition_->transpile(transpiler, cursor);
    cursor << "; ";
    increment_->transpile(transpiler, cursor);
    cursor << ") {";
    transpiler.checker().enter(&body_);
    Cursor local_cursor = cursor.open_child(this);
    for (const auto& stmt : body_) {
        stmt->transpile(transpiler, local_cursor);
        local_cursor.commit();
    }
    transpiler.checker().exit();
    cursor << "}";
}

inline void ASTBreakStatement::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "break;";
}

inline void ASTContinueStatement::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "continue;";
}

inline void ASTReturnStatement::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "return";
    if (expr_) {
        cursor << " ";
        expr_->transpile(transpiler, cursor);
    }
    cursor << ";";
}

inline void ASTFunctionParameter::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    type_->transpile(transpiler, cursor);
    cursor << " " << identifier_;
}

inline void ASTFunctionParameter::transpile_qualifiers(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    TypeResolution qualified_type;
    type_->eval_type(transpiler.checker(), qualified_type);
    if (auto ref_type = qualified_type->dyn_cast<ReferenceType>()) {
        if (ref_type->is_moved_) {
            cursor << "&&";
        } else if (ref_type->referenced_type_->dyn_cast<MutableType>()) {
            cursor << "&";
        } else {
            cursor << "const &";
        }
    } else {
        UNREACHABLE();
    }
}

inline void ASTFunctionDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    bool is_main = transpiler.checker().is_at_top_level() && identifier_ == "main";
    bool will_mangle = !is_main && is_static();
    GlobalMemory::String scoped_identifier = GlobalMemory::format(
        "{}{}{}", transpiler.checker().current_scope()->prefix_, will_mangle ? "$" : "", identifier_
    );

    if (!is_main) {
        return_type_->transpile(transpiler, cursor);
        cursor << (will_mangle ? " $" : " ") << identifier_ << "(";
        const char* sep = "";
        for (size_t index = is_static() ? 0 : 1; index < parameters_.size(); index++) {
            cursor << sep;
            parameters_[index]->transpile(transpiler, cursor);
            sep = ", ";
        }
        cursor << ")";
        if (!is_static()) {
            cursor << " ";
            parameters_[0]->transpile_qualifiers(transpiler, cursor);
        }
        cursor << ";";

        if (is_static() && transpiler.state_.niebloids.insert(identifier_).second) {
            cursor.commit() << "constexpr auto " << identifier_
                            << " = [](auto&&... args) { return $" << identifier_
                            << "(std::forward<decltype(args)>(args)...); };";
        }
    }

    if (is_no_body_) {
        return;
    }

    Cursor def_cursor = transpiler.section(Section::Implementation);
    if (is_main) {
        def_cursor << no_lint_pragma;
        def_cursor.commit();
    } else {
        def_cursor << "inline ";
    }

    return_type_->transpile(transpiler, def_cursor);
    def_cursor << " " << scoped_identifier << "(";
    const char* sep = "";
    for (const auto& param : parameters_ | std::views::drop(is_static() ? 0 : 1)) {
        def_cursor << sep;
        param->transpile(transpiler, def_cursor);
        sep = ", ";
    }
    def_cursor << ") ";
    if (!is_static()) {
        parameters_[0]->transpile_qualifiers(transpiler, def_cursor);
        def_cursor << " ";
    }
    def_cursor << "{";
    transpiler.checker().enter(&body_);
    for (const auto& stmt : body_) {
        Cursor stmt_cursor = def_cursor.open_child(&body_);
        stmt->transpile(transpiler, stmt_cursor);
        stmt_cursor.commit();
    }
    def_cursor.collapse("}");
    transpiler.checker().exit();
}

inline void ASTConstructorDestructorDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    std::string_view classname =
        transpiler.checker().self_type()->cast<InstanceType>()->identifier_;
    cursor << (is_constructor_ ? "" : "~") << classname << "(";
    const char* sep = "";
    for (const auto& param : parameters_ | std::views::drop(1)) {
        cursor << sep;
        param->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ");";

    Cursor def_cursor = transpiler.section(Section::Implementation);
    def_cursor << "inline ";
    def_cursor << classname << "::";
    if (!is_constructor_) {
        def_cursor << "~";
    }
    def_cursor << classname << "(";
    sep = "";
    for (const auto& param : parameters_ | std::views::drop(1)) {
        def_cursor << sep;
        param->transpile(transpiler, def_cursor);
        sep = ", ";
    }
    def_cursor << ") {";
    TypeChecker::Guard guard(transpiler.checker(), &body_);
    for (const auto& stmt : body_) {
        Cursor stmt_cursor = def_cursor.open_child(&body_);
        stmt->transpile(transpiler, stmt_cursor);
    }
    def_cursor.commit() << "}";
}

inline void ASTClassDefinition::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "struct " << identifier_ << " {";

    TypeChecker::Guard guard(transpiler.checker(), this);
    for (const auto& field_decl : fields_) {
        Cursor local_cursor = cursor.open_child(this);
        field_decl->declared_type_->transpile(transpiler, local_cursor);
        local_cursor << " " << field_decl->identifier_ << ";";
    }
    for (const auto& ctor : constructors_) {
        Cursor local_cursor = cursor.open_child(this);
        ctor->transpile(transpiler, local_cursor);
    }
    if (destructor_) {
        Cursor local_cursor = cursor.open_child(this);
        destructor_->transpile(transpiler, cursor);
    }
    for (const auto& func : functions_) {
        Cursor local_cursor = cursor.open_child(this);
        func->transpile(transpiler, local_cursor);
    }
    cursor.commit() << "};";
}

inline void ASTNamespaceDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << "namespace " << identifier_ << " {";
    TypeChecker::Guard guard(transpiler.checker(), this);
    Cursor local_cursor = cursor.open_child(this);
    for (const auto& item : items_) {
        item->transpile(transpiler, local_cursor);
        local_cursor.commit();
    }
}

inline void ASTTemplateParameter::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    if (is_nttp_) {
        constraint_->transpile(transpiler, cursor);
        cursor << identifier_;
    } else {
        cursor << "typename " << identifier_;
    }
    if (default_value_) {
        cursor << " = ";
        default_value_->transpile(transpiler, cursor);
    }
}

inline void ASTTemplateDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << "template <";
    const char* sep = "";
    for (const auto& param : parameters_) {
        cursor << sep;
        param->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ">";
    auto constrainted_types = parameters_ | std::views::filter([](const auto& param) {
                                  return !param->is_nttp_ && param->constraint_;
                              });
    if (!constrainted_types.empty()) {
        sep = " requires(";
        for (const auto& type : constrainted_types) {
            cursor << sep;
            type->constraint_->transpile(transpiler, cursor);
            sep = " && ";
        }
        cursor << ")";
    }

    target_node_->transpile(transpiler, cursor);
}
