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
    struct Node : public GlobalMemory::MemoryManaged {
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
        stream << "#pragma once\n\n";
        output(stream, Section::Includes);
        stream << "\n\n/* ---------- Structural Declarations ---------- */\n\n";
        output(stream, Section::StructuralDeclarations);
        stream << "\n\n/* ---------- Main ---------- */\n\n";
        output(stream, Section::Main);
        stream << "\n\n/* ---------- Implementation ---------- */\n\n";
        output(stream, Section::Implementation);
        stream << "\n";
    }

private:
    void output(std::ostream& os, Section section) const noexcept {
        const BufferTree& tree = sections_[static_cast<std::size_t>(section)];
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

inline void transpile(ASTNode* root, SourceManager& sources, TypeChecker& checker) {
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
        GlobalMemory::format_view("Transformed {} modules to './out'", sources.files.size())
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
    }
}

/// ===================== Inline implementations of Objects =====================

inline void UnknownType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void AnyType::transpile(Cursor& cursor) const noexcept { cursor << "std::any"; }

inline void NullType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void IntegerType::transpile(Cursor& cursor) const noexcept {
    cursor << "std::";
    if (is_signed_) {
        cursor << "int";
    } else {
        cursor << "uint";
    }
    switch (bits_) {
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
}

inline void FloatType::transpile(Cursor& cursor) const noexcept {
    switch (bits_) {
    case 32:
        cursor << "float";
        break;
    case 64:
        cursor << "double";
        break;
    }
}

inline void BooleanType::transpile(Cursor& cursor) const noexcept { cursor << "bool"; }

inline void FunctionType::transpile(Cursor& cursor) const noexcept {
    cursor << "std::function<";
    return_type_->transpile(cursor);
    cursor << "(";
    const char* sep = "";
    for (const Type* param_type : parameters_) {
        cursor << sep;
        param_type->transpile(cursor);
        sep = ", ";
    }
    cursor << ")>";
}

inline void ArrayType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void StructType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void InterfaceType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void ClassType::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void IntersectionType::transpile(Cursor& cursor) const noexcept {
    cursor << "$PolyFunction<";
    const char* sep = "";
    for (const Type* sub_type : types_) {
        cursor << sep;
        sub_type->transpile(cursor);
        sep = ", ";
    }
    cursor << ">";
}

inline void UnionType::transpile(Cursor& cursor) const noexcept {
    cursor << "std::variant<";
    const char* sep = "";
    for (const Type* sub_type : types_) {
        cursor << sep;
        sub_type->transpile(cursor);
        sep = ", ";
    }
    cursor << ">";
}

inline void ReferenceType::transpile(Cursor& cursor) const noexcept {
    referenced_type_->transpile(cursor);
    cursor << "*" << (is_mutable_ ? "" : " const");
}

inline void UnknownValue::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void NullValue::transpile(Cursor& cursor) const noexcept { cursor << "nullptr"; }

inline void IntegerValue::transpile(Cursor& cursor) const noexcept {
    cursor << GlobalMemory::format_view("{}", value_.to_string());
}

inline void FloatValue::transpile(Cursor& cursor) const noexcept {
    cursor << GlobalMemory::format_view("0x{:a}", value_);
}

inline void BooleanValue::transpile(Cursor& cursor) const noexcept {
    cursor << (value_ ? "true" : "false");
}

inline void FunctionValue::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

inline void ArrayValue::transpile(Cursor& cursor) const noexcept {
    /// TODO:
    return;
}

inline void InstanceValue::transpile(Cursor& cursor) const noexcept { UNREACHABLE(); }

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

inline void ASTConstant::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    value_->transpile(cursor);
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
inline void ASTBinaryOp<Op>::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "(";
    left_->transpile(transpiler, cursor);
    cursor << " " << OperatorCodeToString(Op::opcode) << " ";
    right_->transpile(transpiler, cursor);
    cursor << ")";
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    /// TODO: handle prefix/postfix
    cursor << "(" << OperatorCodeToString(Op::opcode);
    expr_->transpile(transpiler, cursor);
    cursor << ")";
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
    type_->transpile(cursor);
}

inline void ASTFunctionType::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    TypeResolution func_type;
    eval_type(transpiler.checker(), func_type);
    func_type->transpile(cursor);
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

inline void ASTReferenceExpr::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    expr_->transpile(transpiler, cursor);
    cursor << (is_mutable_ ? "" : " const") << "*";
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
    if (type_) {
        type_->transpile(transpiler, cursor);
    } else {
        expr_->eval_term(transpiler.checker(), nullptr, false).effective_type()->transpile(cursor);
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
    Cursor then_cursor = cursor.open_child(this);
    for (const auto& stmt : if_block_) {
        stmt->transpile(transpiler, then_cursor);
        then_cursor.commit();
    }
    then_cursor.commit();
    transpiler.checker().exit();
    cursor << "}";
    if (!else_block_.empty()) {
        cursor << "else {";
        transpiler.checker().enter(&else_block_);
        Cursor else_cursor = cursor.open_child(this);
        for (const auto& stmt : else_block_) {
            stmt->transpile(transpiler, else_cursor);
        }
        transpiler.checker().exit();
        else_cursor.commit();
        cursor << "}";
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

inline void ASTFunctionDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    bool is_main = transpiler.checker().is_at_top_level() && identifier_ == "main";
    bool will_mangle = !is_main && is_static_;

    if (!is_main) {
        return_type_->transpile(transpiler, cursor);
        cursor << (will_mangle ? " $" : " ") << identifier_ << "(";
        const char* sep = "";
        for (const auto& param : parameters_) {
            param->transpile(transpiler, cursor);
            cursor << sep;
            sep = ", ";
        }
        cursor << ");";

        if (is_static_ && transpiler.state_.niebloids.insert(identifier_).second) {
            cursor << "constexpr auto " << identifier_ << " = [](auto&&... args) { return $"
                   << identifier_ << "(std::forward<decltype(args)>(args)...); };";
        }
    }

    Cursor def_cursor = transpiler.section(Section::Implementation);
    if (is_main) {
        def_cursor << no_lint_pragma;
        def_cursor.commit();
    } else {
        def_cursor << "inline ";
    }

    return_type_->transpile(transpiler, def_cursor);
    def_cursor << " " << (will_mangle ? "$" : "") << identifier_ << "(";
    const char* sep = "";
    for (const auto& param : parameters_) {
        param->transpile(transpiler, def_cursor);
        def_cursor << sep;
        sep = ", ";
    }
    def_cursor << ") {";
    transpiler.checker().enter(&body_);
    for (const auto& stmt : body_) {
        Cursor stmt_cursor = def_cursor.open_child(&body_);
        stmt->transpile(transpiler, stmt_cursor);
        stmt_cursor.commit();
    }
    def_cursor.collapse("}");
    transpiler.checker().exit();
}

inline void ASTClassDefinition::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "struct " << identifier_ << " {";

    transpiler.checker().enter(this);
    transpiler.checker().enter(&identifier_);
    Cursor local_cursor = cursor.open_child(this);
    for (const auto& field : fields_) {
        field->transpile(transpiler, local_cursor);
        local_cursor.commit();
    }
    transpiler.checker().exit();
    for (const auto& func : functions_) {
        if (!func->is_static_) {
            transpiler.checker().enter(&identifier_);
        }
        func->transpile(transpiler, local_cursor);
        local_cursor.commit();
        if (!func->is_static_) {
            transpiler.checker().exit();
        }
    }
    transpiler.checker().exit();

    cursor << "};";
}

inline void ASTNamespaceDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << "namespace " << identifier_ << " {";
    transpiler.checker().enter(this);
    Cursor local_cursor = cursor.open_child(this);
    for (const auto& item : items_) {
        item->transpile(transpiler, local_cursor);
        local_cursor.commit();
    }
    transpiler.checker().exit();
}
