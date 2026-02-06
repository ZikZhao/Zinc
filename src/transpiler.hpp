#include "pch.hpp"

#include "ast.hpp"
#include "diagnosis.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "runtime-str.hpp"
#include "source.hpp"

enum class Section {
    Includes,
    StructuralDeclarations,
    Declarations,
    Constants,
    Main,
    __len__,
};

class SectionWriter {
public:
    static SectionWriter& indent(SectionWriter& writer) {
        writer.indent_level_++;
        return writer;
    }

    static SectionWriter& dedent(SectionWriter& writer) {
        if (writer.indent_level_ > 0) {
            writer.indent_level_--;
        }
        return writer;
    }

    static SectionWriter& newline(SectionWriter& writer) {
        writer.buffer_ += "\n";
        writer.at_line_start_ = true;
        return writer;
    }

private:
    Transpiler& transpiler_;
    SectionWriter* previous_;
    GlobalMemory::String& target_;
    GlobalMemory::String buffer_;
    std::size_t indent_level_ = 0;
    bool at_line_start_ = true;

public:
    SectionWriter(Transpiler& transpiler, GlobalMemory::String& target) noexcept;

    ~SectionWriter() noexcept;

    SectionWriter& operator<<(std::string_view token) noexcept {
        if (at_line_start_) {
            for (std::size_t i = 0; i < indent_level_; ++i) {
                buffer_ += "    ";
            }
            at_line_start_ = false;
        }
        buffer_ += token;
        return *this;
    }

    SectionWriter& operator<<(auto* right)
        requires requires {
            std::declval<decltype(right)>()->transpile(std::declval<Transpiler&>());
        }
    {
        right->transpile(transpiler_);
        return *this;
    }

    SectionWriter& operator<<(SectionWriter& (*manip)(SectionWriter&)) noexcept {
        return manip(*this);
    }

    Transpiler& transpiler() const noexcept { return transpiler_; }
};

class Transpiler {
    friend class SectionWriter;

public:
    struct State {
        GlobalMemory::Set<std::string_view> niebloids;
        GlobalMemory::Map<const RecordType*, std::size_t> structurals;
        GlobalMemory::Set<const ASTExpression*> types_visited;
        std::stack<
            std::function<void(Transpiler&)>,
            GlobalMemory::Vector<std::function<void(Transpiler&)>>>
            structural_impls;
    };

private:
    const SourceManager::File& file_;
    GlobalMemory::String sections_[static_cast<std::size_t>(Section::__len__)];
    std::optional<SectionWriter> default_writer_;
    SectionWriter* current_;
    TypeChecker& checker_;

public:
    State state_;

public:
    Transpiler(const SourceManager::File& file, TypeChecker& checker)
        : file_(file),
          default_writer_({*this, sections_[static_cast<std::size_t>(Section::Main)]}),
          current_(&*default_writer_),
          checker_(checker) {}

    TypeChecker& checker() noexcept { return checker_; }

    void finalize() {
        default_writer_.reset();
        const GlobalMemory::String stem =
            (std::filesystem::path("out") / file_.path)
                .stem()
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
        std::fstream stream(GlobalMemory::format("out/{}.hpp", stem).c_str(), std::ios::out);
        stream << "#pragma once\n\n"
               << strip_buffer(Section::Includes)
               << "\n\n/* ---------- Structural Declarations ---------- */\n\n"
               << strip_buffer(Section::StructuralDeclarations)
               << "\n\n/* ---------- Declarations ---------- */\n\n"
               << strip_buffer(Section::Declarations)
               << "\n\n/* ---------- Constants ---------- */\n\n"
               << strip_buffer(Section::Constants)  //
               << "\n\n/* ---------- Main ---------- */\n\n"
               << strip_buffer(Section::Main);
    }

    SectionWriter operator[](Section section) {
        return SectionWriter(*this, sections_[static_cast<std::size_t>(section)]);
    }

    SectionWriter& operator<<(auto&& right) {
        return (*current_) << std::forward<decltype(right)>(right);
    }

private:
    std::string_view strip_buffer(Section section) const noexcept {
        const GlobalMemory::String& buffer = sections_[static_cast<std::size_t>(section)];
        std::size_t begin = 0;
        std::size_t end = buffer.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(buffer[begin]))) {
            begin++;
        }
        while (end > begin && std::isspace(static_cast<unsigned char>(buffer[end - 1]))) {
            end--;
        }
        return std::string_view(buffer.data() + begin, end - begin);
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
    root->transpile(transpiler);
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

inline void UnknownType::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

inline void AnyType::transpile(Transpiler& transpiler) const noexcept { transpiler << "std::any"; }

inline void NullType::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

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
    for (const Type* param_type : parameters_) {
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
    std::size_t id = transpiler.state_.structurals.at(const_cast<RecordType*>(this));
    transpiler << "$structural_" << GlobalMemory::format_view("{}", id);
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
    for (const Type* sub_type : types_) {
        transpiler << sep;
        sub_type->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ">";
}

inline void UnionType::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "std::variant<";
    const char* sep = "";
    for (const Type* sub_type : types_) {
        transpiler << sep;
        sub_type->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ">";
}

inline void ReferenceType::transpile(Transpiler& transpiler) const noexcept {
    referenced_type_->transpile(transpiler);
    transpiler << "*" << (is_mutable_ ? "" : " const");
}

inline void UnknownValue::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

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

inline void FunctionValue::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

inline void ArrayValue::transpile(Transpiler& transpiler) const noexcept {
    /// TODO:
    return;
}

inline void InstanceValue::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler) const noexcept {
    transpiler[Section::Includes] << "#include \"pch.hpp\"" << SectionWriter::newline;
    for (const auto& child : statements_) {
        transpiler[Section::Constants];
        child->transpile(transpiler);
    }
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler) const noexcept {
    transpiler << SectionWriter::newline << "{" << SectionWriter::indent << SectionWriter::newline;
    transpiler.checker().enter(this);
    for (const auto& stmt : statements_) {
        stmt->transpile(transpiler);
    }
    transpiler << SectionWriter::dedent << "}" << SectionWriter::newline;
    transpiler.checker().exit();
}

inline void ASTHiddenTypeExpr::transpile(Transpiler& transpiler) const noexcept { UNREACHABLE(); }

inline void ASTConstant::transpile(Transpiler& transpiler) const noexcept {
    value_->transpile(transpiler);
}

inline void ASTIdentifier::transpile(Transpiler& transpiler) const noexcept { transpiler << str_; }

template <typename Op>
inline void ASTBinaryOp<Op>::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "(" << left_ << " " << OperatorCodeToString(Op::opcode) << " " << right_ << ")";
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(Transpiler& transpiler) const noexcept {
    /// TODO: handle prefix/postfix
    transpiler << "(" << OperatorCodeToString(Op::opcode) << expr_ << ")";
}

inline void ASTFunctionCall::transpile(Transpiler& transpiler) const noexcept {
    transpiler << function_ << "(";
    const char* sep = "";
    for (const auto& arg : arguments_) {
        transpiler << sep;
        arg->transpile(transpiler);
        sep = ", ";
    }
    transpiler << ")";
}

inline void ASTPrimitiveType::transpile(Transpiler& transpiler) const noexcept {
    type_->transpile(transpiler);
}

inline void ASTFunctionType::transpile(Transpiler& transpiler) const noexcept {
    TypeResolution func_type;
    eval_type(transpiler.checker(), func_type);
    func_type->transpile(transpiler);
}

inline void ASTFieldDeclaration::transpile(Transpiler& transpiler) const noexcept {
    transpiler << type_ << " " << identifier_ << ";";
}

inline void ASTStructType::transpile(Transpiler& transpiler) const noexcept {
    TypeResolution record_type;
    eval_type(transpiler.checker(), record_type);
    auto [it, inserted] = transpiler.state_.structurals.insert(
        {static_cast<const RecordType*>(record_type.get()), 0}
    );
    if (inserted) {
        it->second = transpiler.state_.structurals.size();
    }
    GlobalMemory::String struct_name = GlobalMemory::format("$structural_{}", it->second);
    transpiler << struct_name;
    if (inserted) {
        transpiler.state_.structural_impls.push([this, id = it->second](Transpiler& transpiler) {
            GlobalMemory::String struct_name = GlobalMemory::format("$structural_{}", id);
            transpiler[Section::StructuralDeclarations] << "struct " << struct_name << ";\n";
            SectionWriter writer = transpiler[Section::Main];
            writer << "struct " << struct_name << " {" << SectionWriter::indent
                   << SectionWriter::newline;
            for (const auto& field : fields_) {
                writer << field << SectionWriter::newline;
            }
            writer << SectionWriter::dedent << "};" << SectionWriter::newline;
        });
    }
}

inline void ASTReferenceExpr::transpile(Transpiler& transpiler) const noexcept {
    transpiler << expr_ << (is_mutable_ ? "" : " const") << "*";
}

inline void ASTExpressionStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << expr_ << ";" << SectionWriter::newline;
}

inline void ASTDeclaration::transpile(Transpiler& transpiler) const noexcept {
    if (is_constant_) {
        transpiler << "constexpr ";
    } else if (!is_mutable_) {
        transpiler << "const ";
    }
    if (type_) {
        transpiler << type_;
    } else {
        transpiler << expr_->eval_term(transpiler.checker(), nullptr, false).effective_type();
    }
    transpiler << " " << identifier_ << " = " << expr_ << ";" << SectionWriter::newline;
}

inline void ASTTypeAlias::transpile(Transpiler& transpiler) const noexcept {
    transpiler[Section::Declarations] << "using " << identifier_ << " = " << type_ << ";"
                                      << SectionWriter::newline;
    while (!transpiler.state_.structural_impls.empty()) {
        std::function<void(Transpiler&)> func = std::move(transpiler.state_.structural_impls.top());
        transpiler.state_.structural_impls.pop();
        func(transpiler);
    }
}

inline void ASTIfStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "if (" << condition_ << ") {" << SectionWriter::indent << SectionWriter::newline;
    transpiler.checker().enter(&if_block_);
    for (const auto& stmt : if_block_) {
        stmt->transpile(transpiler);
    }
    transpiler.checker().exit();
    transpiler << SectionWriter::dedent << "}" << SectionWriter::newline;
    if (!else_block_.empty()) {
        transpiler << "else {" << SectionWriter::indent << SectionWriter::newline;
        transpiler.checker().enter(&else_block_);
        for (const auto& stmt : else_block_) {
            stmt->transpile(transpiler);
        }
        transpiler.checker().exit();
        transpiler << SectionWriter::dedent << "}" << SectionWriter::newline;
    }
}

inline void ASTForStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "for (" << initializer_ << "; " << condition_ << "; " << increment_ << ") {"
               << SectionWriter::indent << SectionWriter::newline;
    transpiler.checker().enter(&body_);
    for (const auto& stmt : body_) {
        stmt->transpile(transpiler);
    }
    transpiler.checker().exit();
    transpiler << SectionWriter::dedent << "}" << SectionWriter::newline;
}

inline void ASTBreakStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "break;" << SectionWriter::newline;
}

inline void ASTContinueStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "continue;" << SectionWriter::newline;
}

inline void ASTReturnStatement::transpile(Transpiler& transpiler) const noexcept {
    transpiler << "return";
    if (expr_) {
        transpiler << " " << expr_;
    }
    transpiler << ";" << SectionWriter::newline;
}

inline void ASTFunctionParameter::transpile(Transpiler& transpiler) const noexcept {
    transpiler << type_ << " " << identifier_;
}

inline void ASTFunctionDefinition::transpile(Transpiler& transpiler) const noexcept {
    bool is_main = transpiler.checker().is_at_top_level() && identifier_ == "main";
    bool will_mangle = !is_main && is_static_;

    if (!is_main) {
        SectionWriter writer = transpiler[Section::Declarations];
        writer << return_type_ << (will_mangle ? " $" : " ") << identifier_ << "(";
        const char* sep = "";
        for (const auto& param : parameters_) {
            writer << sep << param;
            sep = ", ";
        }
        writer << ");" << SectionWriter::newline;

        if (is_static_ && transpiler.state_.niebloids.insert(identifier_).second) {
            transpiler[Section::Constants] << "constexpr auto " << identifier_
                                           << " = [](auto&&... args) { return $" << identifier_
                                           << "(std::forward<decltype(args)>(args)...); };"
                                           << SectionWriter::newline;
        }
    }

    if (is_main) {
        transpiler << "// NOLINTNEXTLINE(misc-definitions-in-headers)" << SectionWriter::newline;
    } else {
        transpiler << "inline ";
    }
    transpiler << return_type_ << " " << (will_mangle ? "$" : "") << identifier_ << "(";
    const char* sep = "";
    for (const auto& param : parameters_) {
        transpiler << sep << param;
        sep = ", ";
    }
    transpiler << ") {" << SectionWriter::indent << SectionWriter::newline;
    transpiler.checker().enter(&body_);
    for (const auto& stmt : body_) {
        stmt->transpile(transpiler);
    }
    transpiler << SectionWriter::dedent << "}" << SectionWriter::newline;
    transpiler.checker().exit();
}

inline void ASTClassDefinition::transpile(Transpiler& transpiler) const noexcept {
    transpiler[Section::Declarations] << "struct " << identifier_ << " {" << SectionWriter::indent
                                      << SectionWriter::newline;

    transpiler.checker().enter(this);
    transpiler.checker().enter(&identifier_);
    for (const auto& field : fields_) {
        field->transpile(transpiler);
        transpiler << SectionWriter::newline;
    }
    transpiler.checker().exit();
    for (const auto& func : functions_) {
        if (!func->is_static_) {
            transpiler.checker().enter(&identifier_);
        }
        func->transpile(transpiler);
        transpiler << SectionWriter::newline;
        if (!func->is_static_) {
            transpiler.checker().exit();
        }
    }
    transpiler.checker().exit();

    transpiler << SectionWriter::dedent << "};" << SectionWriter::newline;
}

inline void ASTNamespaceDefinition::transpile(Transpiler& transpiler) const noexcept {
    transpiler[Section::Declarations] << "namespace " << identifier_ << " {"
                                      << SectionWriter::indent << SectionWriter::newline;
    transpiler.checker().enter(this);
    for (const auto& item : items_) {
        item->transpile(transpiler);
    }
    transpiler.checker().exit();
}

inline SectionWriter::SectionWriter(Transpiler& transpiler, GlobalMemory::String& target) noexcept
    : transpiler_(transpiler), target_(target) {
    previous_ = std::exchange(transpiler.current_, this);
    buffer_.reserve(256);
}

inline SectionWriter::~SectionWriter() noexcept {
    target_ += buffer_;
    transpiler_.current_ = previous_;
}
