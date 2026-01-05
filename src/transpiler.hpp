#include "pch.hpp"
#include <algorithm>
#include <iomanip>
#include <ios>
#include <variant>

#include "ast.hpp"
#include "object.hpp"
#include "operations.hpp"
#include "runtime-str.hpp"
#include "source.hpp"

class CppWriter {
public:
    static CppWriter& indent(CppWriter& writer) {
        writer.indent_level_++;
        return writer;
    }
    static CppWriter& dedent(CppWriter& writer) {
        if (writer.indent_level_ > 0) {
            writer.indent_level_--;
        }
        return writer;
    }
    static CppWriter& newline(CppWriter& writer) {
        writer.stream_ << "\n";
        writer.at_line_start_ = true;
        return writer;
    }

private:
    std::ostream& stream_;
    const SourceManager::File& file_;
    std::size_t indent_level_ = 0;
    bool at_line_start_ = true;
    std::size_t current_line_ = 0;

public:
    explicit CppWriter(std::ostream& out, const SourceManager::File& file)
        : stream_(out), file_(file) {}
    CppWriter& operator<<(const ASTNode* node) {
        std::size_t line_number = static_cast<std::size_t>(std::distance(
            file_.line_offsets.begin(),
            std::upper_bound(
                file_.line_offsets.begin(), file_.line_offsets.end(), node->location_.begin
            )
        ));
        if (line_number != current_line_) {
            stream_ << "\n#line " << line_number << " \"" << file_.path << "\"\n";
            at_line_start_ = true;
            current_line_ = line_number;
        }
        return *this;
    }
    CppWriter& operator<<(std::string_view token) {
        check_indent();
        stream_ << token;
        return *this;
    }
    CppWriter& operator<<(const Type* type) {
        check_indent();
        switch (type->kind_) {
        case Kind::Integer: {
            const IntegerType* int_type = static_cast<const IntegerType*>(type);
            stream_ << "std::";
            if (int_type->is_signed_) {
                stream_ << "int";
            } else {
                stream_ << "uint";
            }
            switch (int_type->bits_) {
            case 8:
                stream_ << "8_t";
                break;
            case 16:
                stream_ << "16_t";
                break;
            case 32:
                stream_ << "32_t";
                break;
            case 64:
                stream_ << "64_t";
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
                stream_ << "float";
                break;
            case 64:
                stream_ << "double";
                break;
            }
            return *this;
        }
        case Kind::String:
            stream_ << "std::string";
            break;
        case Kind::Boolean:
            stream_ << "bool";
            break;
        default:
            stream_ << "/* UnsupportedType */";
            break;
        }
        return *this;
    }
    CppWriter& operator<<(const Value* value) {
        check_indent();
        switch (value->kind_) {
        case Kind::Integer: {
            const IntegerValue* int_value = static_cast<const IntegerValue*>(value);
            const IntegerType* int_type = static_cast<const IntegerType*>(value->get_type());
            if (int_type->is_signed_) {
                stream_ << int_value->ivalue_;
            } else {
                stream_ << int_value->uvalue_;
            }
            break;
        }
        case Kind::Float: {
            const FloatValue* float_value = static_cast<const FloatValue*>(value);
            stream_ << std::hexfloat << float_value->value_;
            break;
        }
        case Kind::String: {
            const StringValue* string_value = static_cast<const StringValue*>(value);
            stream_ << std::quoted(string_value->value_);
            break;
        }
        case Kind::Boolean: {
            const BooleanValue* bool_value = static_cast<const BooleanValue*>(value);
            stream_ << (bool_value->value_ ? "true" : "false");
            break;
        }
        default:
            stream_ << "/* UnsupportedValue */";
            break;
        }
        return *this;
    }
    CppWriter& operator<<(CppWriter& (*manip)(CppWriter&)) { return manip(*this); }

private:
    void check_indent() {
        if (at_line_start_) {
            for (std::size_t i = 0; i < indent_level_; i++) {
                stream_ << "    ";
            }
            at_line_start_ = false;
        }
    }
};

inline void transpile(ASTNode* root, SourceManager& sources, TypeChecker& checker) {
    std::filesystem::create_directory("out");
    std::fstream pch_file = std::fstream("out/pch.hpp", std::ios::out);
    pch_file << runtime_hpp_str();
    pch_file.close();
    std::fstream output_file = std::fstream("out/out.cpp", std::ios::out);
    CppWriter writer(output_file, sources[0]);
    root->transpile(writer, checker);
    output_file.close();
    std::system("g++ -std=c++20 -xc++-header -Iout out/pch.hpp -o out/pch.hpp.gch");
    std::system("g++ -std=c++20 -Iout out/out.cpp -o out/out");
}

/// ===================== Inline implementations of AST nodes =====================

inline void ASTRoot::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << "#include \"pch.hpp\"" << CppWriter::newline;
    for (const auto& child : children_) {
        child->transpile(writer, checker);
        writer << CppWriter::newline;
    }
}

inline void ASTBlock::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << "{" << CppWriter::indent << CppWriter::newline;
    for (const auto& stmt : statements_) {
        stmt->transpile(writer, checker);
        writer << CppWriter::newline;
    }
    writer << CppWriter::dedent << "}";
}

inline void ASTLocalBlock::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << CppWriter::newline;
    checker.enter(this);
    ASTBlock::transpile(writer, checker);
    checker.exit();
}

inline void ASTConstant::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << value_;
}

inline void ASTIdentifier::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << str_;
}

template <typename Op>
inline void ASTBinaryOp<Op>::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << "(";
    left_->transpile(writer, checker);
    writer << OperatorCodeToString(Op::opcode);
    right_->transpile(writer, checker);
    writer << ")";
}

template <typename Op>
inline void ASTBinaryOp<OperatorFunctors::OperateAndAssign<Op>>::transpile(
    CppWriter& writer, TypeChecker& checker
) const noexcept {
    writer << this << "(";
    left_->transpile(writer, checker);
    writer << OperatorCodeToString(Op::opcode) << "=";
    right_->transpile(writer, checker);
    writer << ")";
}

template <typename Op>
inline void ASTUnaryOp<Op>::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    /// TODO: handle prefix/postfix
    writer << this << OperatorCodeToString(Op::opcode);
    expr_->transpile(writer, checker);
}

inline void ASTFunctionCall::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    function_->transpile(writer, checker);
    writer << "(";
    for (const auto& arg : arguments_) {
        arg->transpile(writer, checker);
        writer << ",";
    }
    writer << ")";
}

inline void ASTPrimitiveType::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << type_;
}

inline void ASTFunctionType::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    if (std::holds_alternative<Components>(representation_)) {
        writer << "std::function<";
        const auto& comps = std::get<Components>(representation_);
        std::get<1>(comps)->transpile(writer, checker);
        writer << "(";
        const char* sep = "";
        for (const auto& param_expr : std::get<0>(comps)) {
            writer << sep;
            param_expr->transpile(writer, checker);
            sep = ", ";
        }
        writer << ")";
    } else {
        writer << std::get<FunctionType*>(representation_);
    }
}

inline void ASTFieldDeclaration::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    type_->transpile(writer, checker);
    writer << identifier_ << ";";
}

inline void ASTRecordType::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << "struct {" << CppWriter::indent << CppWriter::newline;
    for (const auto& field : fields_) {
        field->transpile(writer, checker);
        writer << CppWriter::newline;
    }
    writer << CppWriter::dedent << "};" << CppWriter::newline;
}

inline void ASTExpressionStatement::transpile(
    CppWriter& writer, TypeChecker& checker
) const noexcept {
    writer << this;
    expr_->transpile(writer, checker);
    writer << ";";
}

inline void ASTDeclaration::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    if (is_constant_) {
        writer << "constexpr ";
    } else if (!is_mutable_) {
        writer << "const ";
    }
    writer << get_declared_type(checker, expr_->get_expr_info(checker).type) << " ";
    writer << identifier_ << " = ";
    expr_->transpile(writer, checker);
    writer << ";";
}

inline void ASTTypeAlias::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    writer << "using " << identifier_ << " = ";
    type_->transpile(writer, checker);
    writer << ";";
}

inline void ASTIfStatement::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    writer << "if (";
    condition_->transpile(writer, checker);
    writer << ") ";
    if_block_->transpile(writer, checker);
    if (else_block_) {
        writer << "else ";
        else_block_->transpile(writer, checker);
    }
}

inline void ASTForStatement::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this;
    writer << "for (";
    initializer_->transpile(writer, checker);
    condition_->transpile(writer, checker);
    writer << "; ";
    increment_->transpile(writer, checker);
    writer << ") ";
    body_->transpile(writer, checker);
}

inline void ASTBreakStatement::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << "break;";
}

inline void ASTContinueStatement::transpile(
    CppWriter& writer, TypeChecker& checker
) const noexcept {
    writer << this << "continue;";
}

inline void ASTReturnStatement::transpile(CppWriter& writer, TypeChecker& checker) const noexcept {
    writer << this << "return";
    if (expr_) {
        writer << " ";
        expr_->transpile(writer, checker);
    }
    writer << ";";
}

inline void ASTFunctionParameter::transpile(
    CppWriter& writer, TypeChecker& checker
) const noexcept {
    writer << this;
    type_->transpile(writer, checker);
    writer << " " << identifier_;
}

inline void ASTFunctionDefinition::transpile(
    CppWriter& writer, TypeChecker& checker
) const noexcept {
    writer << this;
    return_type_->transpile(writer, checker);
    writer << " " << identifier_ << "(";
    const char* sep = "";
    for (const auto& param : parameters_) {
        writer << sep;
        param->transpile(writer, checker);
        sep = ", ";
    }
    writer << ") ";
    checker.enter(body_.get());
    body_->transpile(writer, checker);
    checker.exit();
}
