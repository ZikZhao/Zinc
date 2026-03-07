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

class Cursor {
private:
    GlobalMemory::String& target_;
    GlobalMemory::String buffer_;
    std::size_t indent_level_ = 0;
    bool at_line_start_ = true;

public:
    explicit Cursor(GlobalMemory::String& target) noexcept : target_(target) {}

    ~Cursor() noexcept { target_ += buffer_; }

    Cursor& operator<<(auto&& args) noexcept {
        if (at_line_start_) {
            for (std::size_t i = 0; i < indent_level_; ++i) {
                buffer_ += "    ";
            }
            at_line_start_ = false;
        }
        std::format_to(std::back_inserter(buffer_), "{}", std::forward<decltype(args)>(args));
        return *this;
    }

    Cursor& flush() noexcept {
        target_ += buffer_;
        buffer_.clear();
        return *this;
    }

    Cursor& newline() noexcept {
        buffer_ += "\n";
        at_line_start_ = true;
        return *this;
    }

    Cursor& indent() noexcept {
        ++indent_level_;
        return *this;
    }

    Cursor& dedent() noexcept {
        assert(indent_level_ > 0);
        --indent_level_;
        return *this;
    }
};

class Transpiler {
    friend class SectionWriter;

public:
    struct State {
        bool in_template_context = false;
        GlobalMemory::String prefix;
        GlobalMemory::FlatSet<std::string_view> niebloids;
        GlobalMemory::FlatMap<const ASTStructType*, std::size_t> structurals;
        GlobalMemory::FlatMap<std::string_view, GlobalMemory::String> identifier_map;
        GlobalMemory::FlatSet<const ASTExpression*> types_visited;
    };

private:
    const SourceManager::File& file_;
    std::array<GlobalMemory::String, static_cast<std::size_t>(Section::__len__)> sections_;

public:
    State state_;
    const DependencyGraph& dep_graph_;

public:
    Transpiler(const SourceManager::File& file, const DependencyGraph& dep_graph) noexcept
        : file_(file), dep_graph_(dep_graph) {}

    Cursor section(Section section) noexcept {
        return Cursor(sections_[static_cast<std::size_t>(section)]);
    }

    std::generator<const ASTNode*> iterate(
        const ASTNode* origin,
        const std::span<ASTNode*>& fallback,
        GlobalMemory::FlatSet<const ASTNode*> fixed = {}
    ) const noexcept {
        if (dep_graph_.has_origin(origin)) {
            for (const ASTNode* stmt : dep_graph_.iterate(origin, fixed)) {
                co_yield stmt;
            }
        } else {
            for (const ASTNode* stmt : fallback) {
                co_yield stmt;
            }
        }
    }

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
        os << "\n// ---------- " << title << " ----------\n\n";
        os << sections_[static_cast<std::size_t>(section)];
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

inline int transpile_all(ASTNode* root, SourceManager& sources, const DependencyGraph& dep_graph) {
    try {
        std::filesystem::create_directory("out");
    } catch (const std::filesystem::filesystem_error& e) {
        Diagnostic::error(
            GlobalMemory::format_view("Failed to create output directory './out': {}", e.what())
        );
    }

    Transpiler transpiler(sources[0], dep_graph);
    Cursor cursor = transpiler.section(Section::Main);
    root->transpile(transpiler, cursor);
    cursor.flush();
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
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    transpiler.section(Section::Includes) << "#include \"pch.hpp\"";
    for (const auto& child : transpiler.iterate(this, statements_)) {
        child->transpile(transpiler, cursor);
        cursor.newline();
    }
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "{";
    cursor.indent();
    for (const auto& stmt : transpiler.iterate(this, statements_)) {
        stmt->transpile(transpiler, cursor);
    }
    cursor.dedent() << "}";
    cursor.newline();
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
    if (auto it = transpiler.state_.identifier_map.find(str_);
        it != transpiler.state_.identifier_map.end()) {
        cursor << it->second;
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
    auto [it, inserted] = transpiler.state_.structurals.insert({this, 0});
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
        for (const auto& field : fields_) {
            field->transpile(transpiler, def_cursor);
        }
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
    cursor << ") ";
    if_block_->transpile(transpiler, cursor);
    if (else_block_) {
        cursor << " else {";
        else_block_->transpile(transpiler, cursor);
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
    for (const auto& stmt : body_) {
        /// TODO: refactor to local block
        stmt->transpile(transpiler, cursor);
        cursor.newline();
    }
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
    auto ref_node = static_cast<ASTReferenceTypeExpr*>(type_);
    /// TODO: handle value category
}

inline void ASTFunctionDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    bool will_mangle = !is_main_ && is_static_;
    GlobalMemory::String scoped_identifier = GlobalMemory::format(
        "{}{}{}", transpiler.state_.prefix, will_mangle ? "$" : "", identifier_
    );

    if (is_main_) {
        transpile_definition(transpiler);
        return;
    }

    return_type_->transpile(transpiler, cursor);
    cursor << (will_mangle ? " $" : " ") << identifier_ << "(";
    const char* sep = "";
    for (size_t index = is_static_ ? 0 : 1; index < parameters_.size(); index++) {
        cursor << sep;
        parameters_[index]->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ")";
    if (!is_static_) {
        cursor << " ";
        parameters_[0]->transpile_qualifiers(transpiler, cursor);
    }

    if (is_decl_only_) {
        cursor << ";";
        return;
    } else if (transpiler.state_.in_template_context) {
        transpile_body(transpiler, cursor);
    } else {
        cursor << ";";
        transpile_definition(transpiler);
    }

    // if (is_static_ && transpiler.state_.niebloids.insert(identifier_).second) {
    //     cursor.newline() << "constexpr auto " << identifier_ << " = [](auto&&... args) {
    //     return
    //     $"
    //                     << identifier_ << "(std::forward<decltype(args)>(args)...); };";
    // }
}

inline void ASTFunctionDefinition::transpile_definition(Transpiler& transpiler) const noexcept {
    bool will_mangle = !is_main_ && is_static_;
    GlobalMemory::String scoped_identifier = GlobalMemory::format(
        "{}{}{}", transpiler.state_.prefix, will_mangle ? "$" : "", identifier_
    );

    Cursor def_cursor = transpiler.section(Section::Implementation);
    if (is_main_) {
        def_cursor << no_lint_pragma;
        def_cursor.newline();
    } else {
        def_cursor << "inline ";
    }

    return_type_->transpile(transpiler, def_cursor);
    def_cursor << " " << scoped_identifier << "(";
    const char* sep = "";
    for (const auto& param : parameters_ | std::views::drop(is_static_ ? 0 : 1)) {
        def_cursor << sep;
        param->transpile(transpiler, def_cursor);
        sep = ", ";
    }
    def_cursor << ") ";
    if (!is_static_) {
        parameters_[0]->transpile_qualifiers(transpiler, def_cursor);
    }

    transpile_body(transpiler, def_cursor);
}

inline void ASTFunctionDefinition::transpile_body(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << " {";
    cursor.indent().newline();
    auto fixed = parameters_ | GlobalMemory::collect<GlobalMemory::FlatSet<const ASTNode*>>();
    for (const auto& stmt : transpiler.iterate(this, body_, fixed)) {
        stmt->transpile(transpiler, cursor);
    }
    cursor.dedent() << "}";
    cursor.newline();
}

inline void ASTConstructorDestructorDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor, std::string_view classname
) const noexcept {
    cursor << (is_constructor_ ? "" : "~") << classname << "(";
    const char* sep = "";
    for (const auto& param : parameters_ | std::views::drop(1)) {
        cursor << sep;
        param->transpile(transpiler, cursor);
        sep = ", ";
    }
    cursor << ")";

    if (transpiler.state_.in_template_context) {
        transpile_body(transpiler, cursor);
    } else {
        cursor << ";";
        transpile_definition(transpiler, classname);
    }
}

inline void ASTConstructorDestructorDefinition::transpile_definition(
    Transpiler& transpiler, std::string_view classname
) const noexcept {
    Cursor def_cursor = transpiler.section(Section::Implementation);
    def_cursor << "inline ";
    def_cursor << classname << "::";
    if (!is_constructor_) {
        def_cursor << "~";
    }
    def_cursor << classname << "(";
    const char* sep = "";
    for (const auto& param : parameters_ | std::views::drop(1)) {
        def_cursor << sep;
        param->transpile(transpiler, def_cursor);
        sep = ", ";
    }
    def_cursor << ")";

    transpile_body(transpiler, def_cursor);
}

inline void ASTConstructorDestructorDefinition::transpile_body(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << " {";
    cursor.indent().newline();
    auto fixed = parameters_ | GlobalMemory::collect<GlobalMemory::FlatSet<const ASTNode*>>();
    for (const auto& stmt : transpiler.iterate(this, body_, fixed)) {
        stmt->transpile(transpiler, cursor);
    }
    cursor.dedent() << "}";
    cursor.newline();
}

inline void ASTClassDefinition::transpile(Transpiler& transpiler, Cursor& cursor) const noexcept {
    cursor << "struct " << identifier_ << " {";
    cursor.indent().newline();

    for (const auto& field_decl : fields_) {
        field_decl->declared_type_->transpile(transpiler, cursor);
        cursor << " " << field_decl->identifier_ << ";";
        cursor.newline();
    }
    for (const auto& ctor : constructors_) {
        ctor->transpile(transpiler, cursor, identifier_);
        cursor.newline();
    }
    if (destructor_) {
        destructor_->transpile(transpiler, cursor, identifier_);
        cursor.newline();
    }
    for (const auto& func : functions_) {
        func->transpile(transpiler, cursor);
        cursor.newline();
    }
    cursor.dedent() << "};";
    cursor.newline();
}

inline void ASTNamespaceDefinition::transpile(
    Transpiler& transpiler, Cursor& cursor
) const noexcept {
    cursor << "namespace " << identifier_ << " {";
    cursor.indent().newline();
    for (const auto& item : transpiler.iterate(this, items_)) {
        item->transpile(transpiler, cursor);
        cursor.newline();
    }
    cursor.dedent() << "}";
    cursor.newline();
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
    cursor.newline();
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

    bool prev_state = std::exchange(transpiler.state_.in_template_context, true);
    target_node_->transpile(transpiler, cursor);
    transpiler.state_.in_template_context = prev_state;
}
