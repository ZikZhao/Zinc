#include "pch.hpp"

#include "ast.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "runtime-str.hpp"
#include "source.hpp"

enum class Section {
    Includes,
    Declarations,
    Constants,
    Main,
    Implementations,
};

class Buffer {
private:
    GlobalMemory::String buffer_;
    std::size_t indent_level_ = 0;
    bool at_line_start_ = true;

public:
    Buffer() noexcept { buffer_.reserve(1024); }

    Buffer& operator<<(std::string_view token) {
        if (at_line_start_) {
            for (std::size_t i = 0; i < indent_level_; ++i) {
                buffer_ += "    ";
            }
            at_line_start_ = false;
        }
        buffer_ += token;
        return *this;
    }

    void indent() noexcept { indent_level_++; }

    void dedent() noexcept {
        if (indent_level_ > 0) {
            indent_level_--;
        }
    }

    void newline() noexcept {
        buffer_ += "\n";
        at_line_start_ = true;
    }

    GlobalMemory::String& operator*() noexcept { return buffer_; }
};

class Transpiler {
public:
    static Transpiler& indent(Transpiler& transpiler) {
        transpiler.current_section_->indent();
        return transpiler;
    }
    static Transpiler& dedent(Transpiler& transpiler) {
        transpiler.current_section_->dedent();
        return transpiler;
    }
    static Transpiler& newline(Transpiler& transpiler) {
        transpiler.current_section_->newline();
        return transpiler;
    }

private:
    const SourceManager::File& file_;
    Buffer buffers_[static_cast<std::size_t>(Section::Implementations) + 1];
    std::size_t current_line_ = 0;
    Buffer* current_section_ = &buffers_[static_cast<std::size_t>(Section::Constants)];
    std::set<std::string_view> niebloids_;

public:
    explicit Transpiler(const SourceManager::File& file) : file_(file) {}
    ~Transpiler() { assert(current_section_ == nullptr); }
    bool should_generate_niebloid(std::string_view name) { return niebloids_.insert(name).second; }
    void finalize() {
        assert(current_section_ != nullptr);
        const GlobalMemory::String stem =
            (std::filesystem::path("out") / file_.path)
                .stem()
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::fstream stream(GlobalMemory::format("out/{}.hpp", stem).c_str(), std::ios::out);
        stream << "#pragma once\n\n"
               << *buffers_[static_cast<std::size_t>(Section::Includes)] << "\n"
               << "/* ---------- Declarations ---------- */\n\n"
               << *buffers_[static_cast<std::size_t>(Section::Declarations)] << "\n"
               << "/* ---------- Constants ---------- */\n\n"
               << *buffers_[static_cast<std::size_t>(Section::Constants)] << "\n"
               << "/* ---------- Main ---------- */\n\n"
               << *buffers_[static_cast<std::size_t>(Section::Main)] << "\n"
               << "/* ---------- Implementations ---------- */\n\n"
               << *buffers_[static_cast<std::size_t>(Section::Implementations)];
        current_section_ = nullptr;
    }
    Transpiler& operator[](Section section) {
        current_section_ = &buffers_[static_cast<std::size_t>(section)];
        current_line_ = 0;
        return *this;
    }
    Transpiler& operator<<(std::string_view token) {
        *current_section_ << token;
        return *this;
    }
    Transpiler& operator<<(Transpiler& (*manip)(Transpiler&)) { return manip(*this); }
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

    Transpiler transpiler(sources[0]);
    root->transpile(transpiler, checker);
    transpiler.finalize();
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

inline void UnknownType::transpile(Transpiler& transpiler) const noexcept {
    // UnknownType can never be instantiated in user code
    std::unreachable();
}

inline void AnyType::transpile(Transpiler& transpiler) const noexcept { transpiler << "std::any"; }

inline void NullType::transpile(Transpiler& transpiler) const noexcept {
    // NullType can never be instantiated in user code
    std::unreachable();
}

inline void IntegerType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "std::";
    if (is_signed_) {
        transpiler << "int";
    } else {
        transpiler << "uint";
    }
    switch (bits_) {
    case 8:
        transpiler << "8_t";
        break;
    case 16:
        transpiler << "16_t";
        break;
    case 32:
        transpiler << "32_t";
        break;
    case 64:
        transpiler << "64_t";
        break;
    default:
        UNREACHABLE();
    }
}

inline void FloatType::transpile(Transpiler& transpiler) const noexcept {
    switch (bits_) {
    case 32:
        transpiler << "float";
        break;
    case 64:
        transpiler << "double";
        break;
    }
}

inline void BooleanType::transpile(Transpiler& transpiler) const noexcept { transpiler << "bool"; }

inline void FunctionType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "std::function<";
    return_type_->transpile(transpiler);
    transpiler << "(";
    const char* sep = "";
    for (Type* param_type : parameters_) {
        transpiler << sep;
        param_type->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ")>";
}

inline void ArrayType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "std::vector<";
    element_type_->transpile(transpiler);
    transpiler << ">";
}

inline void RecordType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "struct {" << Transpiler::indent << Transpiler::newline;
}

inline void InterfaceType::transpile(Transpiler& transpiler) const noexcept {
    /// TODO:
    // transpiler << identifier_;
}

inline void ClassType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << identifier_;
}

inline void IntersectionType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "$PolyFunction<";
    const char* sep = "";
    for (Type* sub_type : types_) {
        transpiler << sep;
        sub_type->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ">";
}

inline void UnionType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "std::variant<";
    const char* sep = "";
    for (Type* sub_type : types_) {
        transpiler << sep;
        sub_type->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ">";
}

inline void UnknownValue::transpile(Transpiler& transpiler) const noexcept { std::unreachable(); }

inline void NullValue::transpile(Transpiler& transpiler) const noexcept { transpiler << "nullptr"; }

inline void IntegerValue::transpile(Transpiler& transpiler) const noexcept {
    transpiler << GlobalMemory::format_view("{}", value_.to_string());
}

inline void FloatValue::transpile(Transpiler& transpiler) const noexcept {
    transpiler << GlobalMemory::format_view("0x{:a}", value_);
}

inline void BooleanValue::transpile(Transpiler& transpiler) const noexcept {
    transpiler << (value_ ? "true" : "false");
}

inline void FunctionValue::transpile(Transpiler& transpiler) const noexcept { std::unreachable(); }

inline void ArrayValue::transpile(Transpiler& transpiler) const noexcept {
    /// TODO:
    return;
}

inline void InstanceValue::transpile(Transpiler& transpiler) const noexcept { std::unreachable(); }

inline void OverloadedFunctionValue::transpile(Transpiler& transpiler) const noexcept {
    std::unreachable();
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler[Section::Includes] << "#include \"pch.hpp\"" << Transpiler::newline;
    for (const auto& child : statements_) {
        transpiler[Section::Constants];
        child->transpile(transpiler, checker);
    }
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << Transpiler::newline << "{" << Transpiler::indent << Transpiler::newline;
    checker.enter(this);
    for (const auto& stmt : statements_) {
        stmt->transpile(transpiler, checker);
    }
    transpiler << Transpiler::dedent << "}" << Transpiler::newline;
    checker.exit();
}

inline void ASTHiddenTypeExpression::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    UNREACHABLE();
}

inline void ASTConstant::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    value_->transpile(transpiler);
}

inline void ASTIdentifier::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << str_;
}

template <typename Op>
inline void ASTBinaryOp<Op>::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "(";
    left_->transpile(transpiler, checker);
    transpiler << " " << OperatorCodeToString(Op::opcode) << " ";
    right_->transpile(transpiler, checker);
    transpiler << ")";
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    /// TODO: handle prefix/postfix
    transpiler << OperatorCodeToString(Op::opcode);
    expr_->transpile(transpiler, checker);
}

inline void ASTFunctionCall::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    function_->transpile(transpiler, checker);
    transpiler << "(";
    const char* sep = "";
    for (const auto& arg : arguments_) {
        transpiler << sep;
        arg->transpile(transpiler, checker);
        sep = ", ";
    }
    transpiler << ")";
}

inline void ASTPrimitiveType::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    type_->transpile(transpiler);
}

inline void ASTFunctionType::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    eval_static(checker)->transpile(transpiler);
}

inline void ASTFieldDeclaration::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    type_->transpile(transpiler, checker);
    transpiler << " " << identifier_ << ";";
}

inline void ASTRecordType::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << "struct {" << Transpiler::indent << Transpiler::newline;
    for (const auto& field : fields_) {
        field->transpile(transpiler, checker);
        transpiler << Transpiler::newline;
    }
    transpiler << Transpiler::dedent << "};" << Transpiler::newline;
}

inline void ASTExpressionStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    expr_->transpile(transpiler, checker);
    transpiler << ";" << Transpiler::newline;
}

inline void ASTDeclaration::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    if (is_constant_) {
        transpiler << "constexpr ";
    } else if (!is_mutable_) {
        transpiler << "const ";
    }
    if (type_) {
        type_->transpile(transpiler, checker);
    } else {
        expr_->resolve_term(checker, nullptr, false).effective_type()->transpile(transpiler);
    }
    transpiler << " " << identifier_ << " = ";
    expr_->transpile(transpiler, checker);
    transpiler << ";" << Transpiler::newline;
}

inline void ASTTypeAlias::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler[Section::Declarations] << "using " << identifier_ << " = ";
    type_->transpile(transpiler, checker);
    transpiler << ";" << Transpiler::newline;
}

inline void ASTIfStatement::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << "if (";
    condition_->transpile(transpiler, checker);
    transpiler << ") {" << Transpiler::indent << Transpiler::newline;
    checker.enter(&if_block_);
    for (const auto& stmt : if_block_) {
        stmt->transpile(transpiler, checker);
    }
    checker.exit();
    transpiler << Transpiler::dedent << "}" << Transpiler::newline;
    if (!else_block_.empty()) {
        transpiler << "else {" << Transpiler::indent << Transpiler::newline;
        checker.enter(&else_block_);
        for (const auto& stmt : else_block_) {
            stmt->transpile(transpiler, checker);
        }
        checker.exit();
        transpiler << Transpiler::dedent << "}" << Transpiler::newline;
    }
}

inline void ASTForStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "for (";
    initializer_->transpile(transpiler, checker);
    condition_->transpile(transpiler, checker);
    transpiler << "; ";
    increment_->transpile(transpiler, checker);
    transpiler << ") {" << Transpiler::indent << Transpiler::newline;
    checker.enter(&body_);
    for (const auto& stmt : body_) {
        stmt->transpile(transpiler, checker);
    }
    checker.exit();
    transpiler << Transpiler::dedent << "}" << Transpiler::newline;
}

inline void ASTBreakStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "break;" << Transpiler::newline;
}

inline void ASTContinueStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "continue;" << Transpiler::newline;
}

inline void ASTReturnStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "return";
    if (expr_) {
        transpiler << " ";
        expr_->transpile(transpiler, checker);
    }
    transpiler << ";" << Transpiler::newline;
}

inline void ASTFunctionParameter::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    type_->transpile(transpiler, checker);
    transpiler << " " << identifier_;
}

inline void ASTFunctionDefinition::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    bool is_main = checker.is_at_top_level() && identifier_ == "main";
    bool will_mangle = !is_main && is_static_;

    if (!is_main) {
        transpiler[Section::Declarations];
        return_type_->transpile(transpiler, checker);
        transpiler << (will_mangle ? " $" : " ") << identifier_ << "(";
        const char* sep = "";
        for (const auto& param : parameters_) {
            transpiler << sep;
            param->transpile(transpiler, checker);
            sep = ", ";
        }
        transpiler << ");" << Transpiler::newline;

        if (is_static_ && transpiler.should_generate_niebloid(identifier_)) {
            transpiler[Section::Constants] << "constexpr auto " << identifier_
                                           << " = [](auto&&... args) { return $" << identifier_
                                           << "(std::forward<decltype(args)>(args)...); };"
                                           << Transpiler::newline;
        }
    }

    if (is_main) {
        transpiler[Section::Main] << "// NOLINTNEXTLINE(misc-definitions-in-headers)"
                                  << Transpiler::newline;
    }
    transpiler[is_main ? Section::Main : Section::Implementations] << (is_main ? "" : "inline ");
    return_type_->transpile(transpiler, checker);
    transpiler << " " << checker.get_current_scope()->prefix_ << (will_mangle ? "$" : "")
               << identifier_ << "(";
    const char* sep = "";
    for (const auto& param : parameters_) {
        transpiler << sep;
        param->transpile(transpiler, checker);
        sep = ", ";
    }
    transpiler << ") {" << Transpiler::indent << Transpiler::newline;
    checker.enter(&body_);
    for (const auto& stmt : body_) {
        stmt->transpile(transpiler, checker);
    }
    transpiler << Transpiler::dedent << "}" << Transpiler::newline;
    checker.exit();
}

inline void ASTClassDefinition::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler[Section::Declarations];
    transpiler << "struct " << identifier_ << " {" << Transpiler::indent << Transpiler::newline;

    checker.enter(this);
    checker.enter(&identifier_);
    for (const auto& field : fields_) {
        field->transpile(transpiler, checker);
        transpiler << Transpiler::newline;
    }
    checker.exit();
    for (const auto& func : functions_) {
        if (!func->is_static_) {
            checker.enter(&identifier_);
        }
        func->transpile(transpiler, checker);
        transpiler << Transpiler::newline;
        if (!func->is_static_) {
            checker.exit();
        }
    }
    checker.exit();

    transpiler[Section::Declarations] << Transpiler::dedent << "};" << Transpiler::newline;
}

inline void ASTNamespaceDefinition::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler[Section::Declarations];
    transpiler << "namespace " << identifier_ << " {" << Transpiler::indent << Transpiler::newline;

    checker.enter(this);
    for (const auto& item : items_) {
        item->transpile(transpiler, checker);
    }
    checker.exit();
}
