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
    Implementations,
    Main,
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
    Buffer buffers_[static_cast<std::size_t>(Section::Main) + 1];
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
        std::fstream stream(GlobalMemory::format("out/{}.hpp", stem).data(), std::ios::out);
        stream << *buffers_[static_cast<std::size_t>(Section::Includes)] << "\n"
               << *buffers_[static_cast<std::size_t>(Section::Declarations)] << "\n"
               << *buffers_[static_cast<std::size_t>(Section::Constants)] << "\n"
               << *buffers_[static_cast<std::size_t>(Section::Implementations)] << "\n"
               << *buffers_[static_cast<std::size_t>(Section::Main)];
        current_section_ = nullptr;
    }
    Transpiler& operator[](Section section) {
        current_section_ = &buffers_[static_cast<std::size_t>(section)];
        current_line_ = 0;
        return *this;
    }
    Transpiler& operator<<(const ASTNode* node) {
        std::size_t line_number = static_cast<std::size_t>(std::distance(
            file_.line_offsets.begin(),
            std::upper_bound(
                file_.line_offsets.begin(), file_.line_offsets.end(), node->location_.begin
            )
        ));
        if (line_number != current_line_) {
            current_section_->newline();
            std::format_to(
                std::back_inserter(**current_section_), "#line {} \"{}\"", line_number, file_.path
            );
            current_section_->newline();
            current_line_ = line_number;
        }
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
        case Kind::String:
            *current_section_ << "std::string";
            break;
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
            *current_section_ << "PolyFunction<";
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
            std::format_to(std::back_inserter(**current_section_), "{}", float_value->value_);
            break;
        }
        case Kind::String: {
            const StringValue* string_value = static_cast<const StringValue*>(value);
            *current_section_ << GlobalMemory::hex_string(string_value->value_);
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
    Diagnostic::message(GlobalMemory::format("Wrote {} files to ./out", sources.files.size()));
    std::string_view pch_command =
        "g++ -std=c++20 -xc++-header -Iout out/pch.hpp -o out/pch.hpp.gch";
    Diagnostic::message(GlobalMemory::format("Compiling precompiled header: {}", pch_command));
    int ret = std::system(pch_command.data());
    if (ret != 0) {
        return ret;
    }
    std::string_view compile_command = GlobalMemory::format(
        "g++ -std=c++20 -Iout \"out/{}.hpp\" -o \"out/{}\"", main_stem, main_stem
    );
    Diagnostic::message(
        GlobalMemory::format(
            "Compiling output executable to ./out/{}: {}", main_stem, compile_command
        )
    );
    return std::system(compile_command.data());
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler[Section::Includes] << "#include \"pch.hpp\"" << Transpiler::newline;
    for (const auto& child : children_) {
        transpiler[Section::Constants];
        child->transpile(transpiler, checker);
        transpiler << Transpiler::newline;
    }
}

inline void ASTBlock::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << "{" << Transpiler::indent << Transpiler::newline;
    for (const auto& stmt : statements_) {
        stmt->transpile(transpiler, checker);
        transpiler << Transpiler::newline;
    }
    transpiler << Transpiler::dedent << "}";
}

inline void ASTLocalBlock::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << Transpiler::newline;
    checker.enter(this);
    ASTBlock::transpile(transpiler, checker);
    checker.exit();
}

inline void ASTConstant::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << this << value_;
}

inline void ASTIdentifier::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << this << str_;
}

template <typename Op>
inline void ASTBinaryOp<Op>::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this << "(";
    left_->transpile(transpiler, checker);
    transpiler << OperatorCodeToString(Op::opcode);
    right_->transpile(transpiler, checker);
    transpiler << ")";
}

template <typename Op>
inline void ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>>::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this << "(";
    left_->transpile(transpiler, checker);
    transpiler << OperatorCodeToString(Op::opcode) << "=";
    right_->transpile(transpiler, checker);
    transpiler << ")";
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    /// TODO: handle prefix/postfix
    transpiler << this << OperatorCodeToString(Op::opcode);
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
    transpiler << this << type_;
}

inline void ASTFunctionType::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this << eval(checker)->as_type();
}

inline void ASTFieldDeclaration::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this;
    type_->transpile(transpiler, checker);
    transpiler << identifier_ << ";";
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
    transpiler << this;
    expr_->transpile(transpiler, checker);
    transpiler << ";";
}

inline void ASTDeclaration::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << this;
    if (is_constant_) {
        transpiler << "constexpr ";
    } else if (!is_mutable_) {
        transpiler << "const ";
    }
    transpiler << get_declared_type(checker, expr_->get_expr_info(checker).type) << " ";
    transpiler << identifier_ << " = ";
    expr_->transpile(transpiler, checker);
    transpiler << ";";
}

inline void ASTTypeAlias::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler[Section::Declarations] << this;
    transpiler << "using " << identifier_ << " = ";
    type_->transpile(transpiler, checker);
    transpiler << ";";
}

inline void ASTIfStatement::transpile(Transpiler& transpiler, TypeChecker& checker) const noexcept {
    transpiler << this;
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
    transpiler << this;
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
    transpiler << this << "break;";
}

inline void ASTContinueStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this << "continue;";
}

inline void ASTReturnStatement::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this << "return";
    if (expr_) {
        transpiler << " ";
        expr_->transpile(transpiler, checker);
    }
    transpiler << ";";
}

inline void ASTFunctionParameter::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    transpiler << this;
    type_->transpile(transpiler, checker);
    transpiler << " " << identifier_;
}

inline void ASTFunctionDefinition::transpile(
    Transpiler& transpiler, TypeChecker& checker
) const noexcept {
    bool is_main = checker.at_top_level() && identifier_ == "main";

    if (!is_main) {
        transpiler[Section::Declarations] << this;
        return_type_->transpile(transpiler, checker);
        transpiler << " $" << identifier_ << "(";
        const char* sep = "";
        for (const auto& param : parameters_) {
            transpiler << sep;
            param->transpile(transpiler, checker);
            sep = ", ";
        }
        transpiler << ");";

        if (transpiler.should_generate_niebloid(identifier_)) {
            transpiler[Section::Constants] << "constexpr auto " << identifier_
                                           << " = [](auto&&... args) { return $" << identifier_
                                           << "(std::forward<decltype(args)>(args)...); };"
                                           << Transpiler::newline;
        }
    }

    transpiler[is_main ? Section::Main : Section::Implementations];
    return_type_->transpile(transpiler, checker);
    transpiler << (is_main ? " " : " $") << identifier_ << "(";
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
