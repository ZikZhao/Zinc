#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "type_check.hpp"

static auto type_hash(const void* ptr) -> std::string_view {
    thread_local std::array<char, sizeof(void*) * 2> buffer;
    std::format_to_n(
        buffer.begin(),
        buffer.size(),
        "t{:0{}X}",
        std::bit_cast<std::uintptr_t>(ptr),
        sizeof(void*) * 2
    );
    return {buffer.data(), buffer.size()};
}

static auto func_hash(const void* ptr) -> std::string_view {
    thread_local std::array<char, sizeof(void*) * 2> buffer;
    std::format_to_n(
        buffer.begin(),
        buffer.size(),
        "f{:0{}X}",
        std::bit_cast<std::uintptr_t>(ptr),
        sizeof(void*) * 2
    );
    return {buffer.data(), buffer.size()};
}

struct VariantGetter {
    auto operator()(std::monostate) const noexcept -> const void* { UNREACHABLE(); }
    auto operator()(const void* ptr) const noexcept -> const void* { return ptr; }
};

class TypeSorter {
private:
    struct Edge {
        const Type* child;
        const Type* parent;
        auto operator<=>(const Edge& other) const noexcept -> std::strong_ordering {
            if (auto cmp = child <=> other.child; cmp != 0) {
                return cmp;
            }
            return parent <=> other.parent;
        }
    };

private:
    GlobalMemory::FlatSet<const Type*> types_;
    GlobalMemory::Set<Edge> edges_;

public:
    TypeSorter() noexcept = default;

    void add(const Type* type) noexcept {
        switch (type->kind_) {
        case Kind::Struct:
            types_.insert(type);
            for (const auto& field : type->cast<StructType>()->fields_) {
                edges_.insert({.child = field.second, .parent = type});
            }
            break;
        case Kind::Instance:
            types_.insert(type);
            for (const auto& attr : type->cast<InstanceType>()->attrs_) {
                edges_.insert({.child = attr.second, .parent = type});
            }
            break;
        case Kind::Mutable:
            edges_.insert({.child = type->cast<MutableType>()->target_type_, .parent = type});
            break;
        default:
            /// Indirect dependencies through pointers and references are not added
            /// to the graph since they do not affect type completeness
            break;
        }
    }

    /// Only returns nominal types
    auto iterate() && noexcept -> std::generator<const Type*> {
        GlobalMemory::FlatMap<const Type*, std::size_t> in_degree;
        GlobalMemory::FlatSet<const Type*> candidates = std::move(types_);
        for (const Edge& edge : edges_) {
            in_degree[edge.parent]++;
            candidates.insert(edge.child);
        }
        GlobalMemory::Vector<const Type*> queue;
        for (const Type* type : candidates) {
            if (!in_degree.contains(type)) {
                queue.push_back(type);
            }
        }
        while (!queue.empty()) {
            const Type* child = queue.back();
            queue.pop_back();
            if (child->kind_ == Kind::Struct || child->kind_ == Kind::Instance) {
                co_yield child;
            }
            auto range = std::ranges::equal_range(edges_, child, std::less{}, &Edge::child);
            for (const Edge& edge : range) {
                const Type* parent = edge.parent;
                if (--in_degree[parent] == 0) {
                    queue.push_back(parent);
                }
            }
        }
    }
};

class TypeCodeGen final {
private:
    std::ofstream& stream_;
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;

public:
    TypeCodeGen(std::ofstream& stream) : stream_(stream) {}

    void operator()() {
        TypeSorter sorter;
        for (const StructType* type :
             std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        for (const InstanceType* type : TypeRegistry::instance->instance_types_) {
            sorter.add(type);
        }
        for (const Type* type : std::move(sorter).iterate()) {
            switch (type->kind_) {
            case Kind::Struct:
                generate(type->cast<StructType>());
                break;
            case Kind::Instance:
                generate(type->cast<InstanceType>());
                break;
            default:
                UNREACHABLE();
            }
        }
        stream_ << forward_declarations_ << "\n" << definitions_;
    }

    void generate(const Type* type) {
        switch (type->kind_) {
        case Kind::Unknown:
            UNREACHABLE();
        case Kind::Any:
            definitions_ += "std::any";
            break;
        case Kind::Nullptr:
            definitions_ += "std::nullptr_t";
            break;
        case Kind::Integer:
            generate(type->cast<IntegerType>());
            break;
        case Kind::Float:
            generate(type->cast<FloatType>());
            break;
        case Kind::Boolean:
            definitions_ += "bool";
            break;
        case Kind::Function:
            generate(type->cast<FunctionType>());
            break;
        case Kind::Array:
            /// TODO:
            break;
        case Kind::Struct:
        case Kind::Instance:
            definitions_ += type_hash(type);
            break;
        case Kind::Interface:
            /// TODO:
            break;
        case Kind::Mutable:
            /// TODO:
            generate(type->cast<MutableType>()->target_type_);
            break;
        case Kind::Reference:
            generate(type->cast<ReferenceType>()->referenced_type_);
            definitions_ += "&";
            break;
        case Kind::Pointer:
            generate(type->cast<PointerType>()->pointed_type_);
            definitions_ += "*";
            break;
        default:
            /// TODO:
            assert(false);
        }
    }

    void generate(const IntegerType* type) {
        definitions_ += type->is_signed_ ? "std::int" : "std::uint";
        switch (type->bits_) {
        case 8:
            definitions_ += "8_t";
            break;
        case 16:
            definitions_ += "16_t";
            break;
        case 32:
            definitions_ += "32_t";
            break;
        case 64:
            definitions_ += "64_t";
            break;
        default:
            UNREACHABLE();
        }
    }

    void generate(const FloatType* type) {
        if (type->bits_ == 32) {
            definitions_ += "float";
        } else if (type->bits_ == 64) {
            definitions_ += "double";
        } else {
            UNREACHABLE();
        }
    }

    void generate(const FunctionType* type) {
        definitions_ += "std::function<";
        generate(type->return_type_);
        definitions_ += "(";
        const char* sep = "";
        for (const Type* param_type : type->parameters_) {
            definitions_ += sep;
            generate(param_type);
            sep = ", ";
        }
        definitions_ += ")>";
    }

    void generate(const StructType* type) {
        std::format_to(std::back_inserter(forward_declarations_), "struct {};\n", type_hash(type));
        std::format_to(std::back_inserter(definitions_), "struct {} {{\n", type_hash(type));
        for (const auto& [field_name, field_type] : type->fields_) {
            definitions_ += "    ";
            generate(field_type);
            definitions_ += " ";
            definitions_ += field_name;
            definitions_ += ";\n";
        }
        definitions_ += "};\n";
    }

    void generate(const InstanceType* type) {
        std::format_to(std::back_inserter(forward_declarations_), "struct {};\n", type_hash(type));
        std::format_to(std::back_inserter(definitions_), "struct {} {{\n", type_hash(type));
        for (const auto& [attr_name, attr_type] : type->attrs_) {
            definitions_ += "    ";
            generate(attr_type);
            definitions_ += " ";
            definitions_ += attr_name;
            definitions_ += ";\n";
        }
        /// TODO: methods with instantiations
        definitions_ += "};\n";
    }
};

class CodeGen final {
private:
    using Environment = GlobalMemory::FlatMap<const ASTExpression*, std::string_view>;

    class Guard {
    private:
        CodeGen& codegen_;
        const Scope* prev_scope;
        const Environment* prev_env;

    public:
        Guard(CodeGen& codegen, const Scope* scope) noexcept
            : codegen_(codegen),
              prev_scope(std::exchange(codegen.current_scope_, scope)),
              prev_env(std::exchange(codegen.current_env_, &codegen.scope_envs_.at(scope))) {}
        Guard(const Guard&) = delete;
        Guard(Guard&&) = delete;
        auto operator=(const Guard&) = delete;
        auto operator=(Guard&&) = delete;
        ~Guard() noexcept {
            codegen_.current_scope_ = prev_scope;
            codegen_.current_env_ = prev_env;
        }
    };

private:
    std::ofstream& stream_;
    const GlobalMemory::FlatMap<const Scope*, Environment>& scope_envs_;
    const Scope* current_scope_;
    const Environment* current_env_;

public:
    CodeGen(
        std::ofstream& stream, decltype(scope_envs_) scope_envs, const Scope* current_scope
    ) noexcept
        : stream_(stream),
          scope_envs_(scope_envs),
          current_scope_(current_scope),
          current_env_(&scope_envs_.at(current_scope_)) {}

    auto operator()(ASTNodeVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(ASTExprVariant variant) -> void {
        std::visit(
            [&]<typename T>(T node) -> void {
                if constexpr (std::is_convertible_v<T, const ASTExpression*>) {
                    if (auto it = current_env_->find(node); it != current_env_->end()) {
                        stream_ << it->second;
                        return;
                    }
                }
                return std::visit(*this, variant);
            },
            variant
        );
    }

    auto operator()(std::monostate) -> void { UNREACHABLE(); }

    auto operator()(const auto*) -> void { UNREACHABLE(); }

    auto operator()(const ASTFunctionDefinition* node) -> void {
        stream_ << "auto " << node->identifier << "(";
        const char* sep = "";
        for (const auto& param : node->parameters) {
            stream_ << sep << param.identifier
                    << type_hash(std::visit(VariantGetter{}, param.type));
            sep = ", ";
        }
        stream_ << ") -> " << type_hash(std::visit(VariantGetter{}, node->return_type)) << " {\n";
        // Guard guard(*this, node);
        for (const ASTNodeVariant& child : node->body) {
            (*this)(child);
        }
        stream_ << "}\n";
    }

    auto operator()(const ASTLocalBlock* node) -> void {
        for (const ASTNodeVariant& child : node->statements) {
            (*this)(child);
        }
    }

    auto operator()(const ASTParenExpr* node) -> void {
        stream_ << "(";
        (*this)(node->inner);
        stream_ << ")";
    }

    auto operator()(const ASTConstant* node) -> void {
        /// TODO:
        // stream_ << node->value;
    }

    auto operator()(const ASTSelfExpr* node) -> void {
        stream_ << (node->is_type ? "decltype(*this)" : "(*this)");
    }

    auto operator()(const ASTIdentifier* node) -> void { stream_ << node->str; }

    template <ASTUnaryOpClass Op>
    auto operator()(const Op* node) -> void {
        /// TODO: postfix unary ops
        stream_ << GetOperatorString(Op::opcode);
        (*this)(node->expr);
    }

    template <ASTBinaryOpClass Op>
    auto operator()(const Op* node) -> void {
        (*this)(node->left);
        stream_ << " " << GetOperatorString(Op::opcode) << " ";
        (*this)(node->right);
    }

    auto operator()(const ASTMemberAccess* node) -> void {
        (*this)(node->target);
        for (std::string_view member : node->members) {
            stream_ << "." << member;
        }
    }

    auto operator()(const ASTStructInitialization* node) -> void {
        stream_ << "{\n";
        for (const ASTFieldInitialization& field_init : node->field_inits) {
            stream_ << "." << field_init.identifier << " = ";
            (*this)(field_init.value);
            stream_ << ",\n";
        }
        stream_ << "}";
    }

    auto operator()(const ASTFunctionCall* node) -> void {
        (*this)(node->function);
        stream_ << "(";
        const char* sep = "";
        for (const ASTExprVariant& arg : node->arguments) {
            stream_ << sep;
            (*this)(arg);
            sep = ", ";
        }
        stream_ << ")";
    }

    auto operator()(const ASTExpressionStatement* node) -> void {
        (*this)(node->expr);
        stream_ << ";\n";
    }

    auto operator()(const ASTDeclaration* node) -> void {
        stream_ << type_hash(std::visit(VariantGetter{}, node->declared_type)) << " "
                << node->identifier << ";\n";
    }

    auto operator()(const ASTIfStatement* node) -> void {
        stream_ << "if (";
        (*this)(node->condition);
        stream_ << ") \n";
        (*this)(node->if_block);
        stream_ << "\n";
        if (node->else_block) {
            stream_ << "else \n";
            (*this)(node->else_block);
            stream_ << "\n";
        }
    }

    auto operator()(const ASTForStatement* node) -> void {
        stream_ << "for (";
        if (node->initializer_decl) {
            (*this)(node->initializer_decl);
        } else if (nonnull(node->initializer_expr)) {
            (*this)(node->initializer_expr);
        }
        stream_ << "; ";
        if (nonnull(node->condition)) {
            (*this)(node->condition);
        }
        stream_ << "; ";
        if (nonnull(node->increment)) {
            (*this)(node->increment);
        }
        stream_ << ") \n";
        (*this)(node->body);
    }
};

auto codegen(SourceManager& sources) -> int {
    GlobalMemory::String out_path = sources.files[0].path + ".cpp";
    std::ofstream out(out_path.c_str());
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return EXIT_FAILURE;
    }
    out << "#include <any>\n#include <cstdint>\n#include <functional>\n\n";
    TypeCodeGen{out}();
    return EXIT_SUCCESS;
}
