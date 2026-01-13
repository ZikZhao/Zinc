#include "pch.hpp"
#include <string_view>

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
        std::filesystem::create_directory("out");
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
    Transpiler& operator<<(const Type* type) {
        switch (type->kind_) {
        case Kind::Integer: {
            const IntegerType* int_type = static_cast<const IntegerType*>(type);
            *current_section_ << "std::";
            if (int_type->is_signed_) {
                *current_section_ << "int";
            } else {
                *current_section_ << "uint";
            }
            switch (int_type->bits_) {
            case 8:
                *current_section_ << "8_t";
                break;
            case 16:
                *current_section_ << "16_t";
                break;
            case 32:
                *current_section_ << "32_t";
                break;
            case 64:
                *current_section_ << "64_t";
                break;
            default:
                assert(false);
                std::unreachable();
            }
            break;
        }
        case Kind::Float: {
            const FloatType* float_type = static_cast<const FloatType*>(type);
            switch (float_type->bits_) {
            case 32:
                *current_section_ << "float";
                break;
            case 64:
                *current_section_ << "double";
                break;
            }
            return *this;
        }
        case Kind::Boolean:
            *current_section_ << "bool";
            break;
        case Kind::Function: {
            const FunctionType* func_type = static_cast<const FunctionType*>(type);
            *current_section_ << "std::function<";
            *this << func_type->return_type_;
            *current_section_ << "(";
            const char* sep = "";
            for (Type* param_type : func_type->parameters_) {
                *current_section_ << sep;
                *this << param_type;
                sep = ", ";
            }
            *current_section_ << ")>";
            break;
        }
        case Kind::Intersection: {
            const IntersectionType* intersection = static_cast<const IntersectionType*>(type);
            *current_section_ << "$PolyFunction<";
            const char* sep = "";
            for (Type* sub_type : intersection->types_) {
                *current_section_ << sep;
                *this << sub_type;
                sep = ", ";
            }
            *current_section_ << ">";
            break;
        }
        default:
            *this << "/* UnsupportedType */";
            break;
        }
        return *this;
    }
    Transpiler& operator<<(const Value* value) {
        switch (value->kind_) {
        case Kind::Integer: {
            const IntegerValue* int_value = static_cast<const IntegerValue*>(value);
            const IntegerType* int_type = static_cast<const IntegerType*>(value->get_type());
            if (int_type->is_signed_) {
                std::format_to(std::back_inserter(**current_section_), "{}", int_value->ivalue_);
            } else {
                std::format_to(std::back_inserter(**current_section_), "{}", int_value->uvalue_);
            }
            break;
        }
        case Kind::Float: {
            const FloatValue* float_value = static_cast<const FloatValue*>(value);
            std::format_to(std::back_inserter(**current_section_), "0x{:a}", float_value->value_);
            break;
        }
        case Kind::Boolean: {
            const BooleanValue* bool_value = static_cast<const BooleanValue*>(value);
            *current_section_ << (bool_value->value_ ? "true" : "false");
            break;
        }
        default:
            *current_section_ << "/* UnsupportedValue */";
            break;
        }
        return *this;
    }
    Transpiler& operator<<(Transpiler& (*manip)(Transpiler&)) { return manip(*this); }
};

inline int transpile(ASTNode* root, SourceManager& sources, TypeChecker& checker) {
    std::filesystem::create_directory("out");
    std::fstream pch_file = std::fstream("out/pch.hpp", std::ios::out);
    pch_file << runtime_hpp_str();
    pch_file.close();
    Transpiler transpiler(sources[0]);
    root->transpile(transpiler, checker);
    transpiler.finalize();
    const GlobalMemory::String main_stem =
        std::filesystem::path(sources[0].path)
            .stem()
            .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>();
    Diagnostic::message(GlobalMemory::format_view("Wrote {} files to ./out", sources.files.size()));
    const char* pch_command = "g++ -std=c++20 -xc++-header -Iout out/pch.hpp -o out/pch.hpp.gch";
    Diagnostic::message(GlobalMemory::format_view("Compiling precompiled header: {}", pch_command));
    int ret = std::system(pch_command);
    if (ret != 0) {
        return ret;
    }
    GlobalMemory::String compile_command = GlobalMemory::format(
        "g++ -std=c++20 -Iout \"out/{}.hpp\" -o \"out/{}\"", main_stem, main_stem
    );
    Diagnostic::message(
        GlobalMemory::format_view(
            "Compiling output executable to ./out/{}: {}", main_stem, compile_command
        )
    );
    return std::system(GlobalMemory::String(compile_command).c_str());
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler[Section::Includes] << "#include \"pch.hpp\"" << Transpiler::newline;
    for (const auto& child : statements_) {
        transpiler[Section::Constants];
        child->transpile(transpiler, checker);
    }
}

inline void ASTBlock::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << "{" << Transpiler::indent << Transpiler::newline;
    for (const auto& stmt : statements_) {
        stmt->transpile(transpiler, checker);
    }
    transpiler << Transpiler::dedent << "}" << Transpiler::newline;
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << Transpiler::newline;
    checker.enter(this);
    ASTBlock::transpile(transpiler, checker);
    checker.exit();
}

inline void ASTConstant::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << value_;
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
    transpiler << OperatorCodeToString(Op::opcode);
    right_->transpile(transpiler, checker);
    transpiler << ")";
}

template <typename Op>
inline void ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>>::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << "(";
    left_->transpile(transpiler, checker);
    transpiler << OperatorCodeToString(Op::opcode) << "=";
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
    transpiler << type_;
}

inline void ASTFunctionType::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << eval(checker)->as_type();
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
    transpiler << get_declared_type(checker, expr_->get_expr_info(checker).type) << " ";
    transpiler << identifier_ << " = ";
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
    transpiler << ") ";
    if_block_->transpile(transpiler, checker);
    if (else_block_) {
        transpiler << "else ";
        else_block_->transpile(transpiler, checker);
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
    transpiler << ") ";
    body_->transpile(transpiler, checker);
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

inline void ASTFunctionDeclaration::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    bool is_main = checker.at_top_level() && identifier_ == "main";
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

    transpiler[is_main ? Section::Main : Section::Implementations] << (is_main ? "" : "inline ");
    return_type_->transpile(transpiler, checker);
    transpiler << " " << checker.get_current_scope_prefix() << (will_mangle ? "$" : "")
               << identifier_ << "(";
    const char* sep = "";
    for (const auto& param : parameters_) {
        transpiler << sep;
        param->transpile(transpiler, checker);
        sep = ", ";
    }
    transpiler << ") ";
    checker.enter(body_.get());
    body_->transpile(transpiler, checker);
    checker.exit();
}

inline void ASTClassDeclaration::transpile(
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
