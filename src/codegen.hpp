#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "type_check.hpp"

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
public:
    static void generate(
        GlobalMemory::Vector<const Type*> generated_types, const Type* type, auto out_it
    ) {
        switch (type->kind_) {
        case Kind::Unknown:
            UNREACHABLE();
        case Kind::Any:
            std::format_to(out_it, "{}", "std::any");
            break;
        case Kind::Nullptr:
            std::format_to(out_it, "{}", "std::nullptr_t");
            break;
        case Kind::Integer:
            generate(generated_types, type->cast<IntegerType>(), out_it);
            break;
        case Kind::Float:
            generate(generated_types, type->cast<FloatType>(), out_it);
            break;
        case Kind::Boolean:
            std::format_to(out_it, "{}", "bool");
            break;
        case Kind::Function:
            generate(generated_types, type->cast<FunctionType>(), out_it);
            break;
        case Kind::Array:
            /// TODO:
            break;
        case Kind::Struct:
        case Kind::Instance:
            std::format_to(out_it, "t{}", index(generated_types, type));
            break;
        case Kind::Interface:
            /// TODO:
            break;
        case Kind::Mutable:
            /// TODO:
            generate(generated_types, type->cast<MutableType>()->target_type_, out_it);
            break;
        case Kind::Reference:
            generate(generated_types, type->cast<ReferenceType>()->referenced_type_, out_it);
            std::format_to(out_it, "{}", "&");
            break;
        case Kind::Pointer:
            generate(generated_types, type->cast<PointerType>()->pointed_type_, out_it);
            std::format_to(out_it, "{}", "*");
            break;
        default:
            /// TODO:
            assert(false);
        }
    }

    static void generate(
        GlobalMemory::Vector<const Type*>& generated_types, const IntegerType* type, auto out_it
    ) {
        std::format_to(out_it, "std::{}int", type->is_signed_ ? "" : "u");
        switch (type->bits_) {
        case 8:
            std::format_to(out_it, "{}", "8_t");
            break;
        case 16:
            std::format_to(out_it, "{}", "16_t");
            break;
        case 32:
            std::format_to(out_it, "{}", "32_t");
            break;
        case 64:
            std::format_to(out_it, "{}", "64_t");
            break;
        default:
            UNREACHABLE();
        }
    }

    static void generate(
        GlobalMemory::Vector<const Type*>& generated_types, const FloatType* type, auto out_it
    ) {
        if (type->bits_ == 32) {
            std::format_to(out_it, "{}", "float");
        } else if (type->bits_ == 64) {
            std::format_to(out_it, "{}", "double");
        } else {
            UNREACHABLE();
        }
    }

    static void generate(
        GlobalMemory::Vector<const Type*>& generated_types, const FunctionType* type, auto out_it
    ) {
        std::format_to(out_it, "{}", "std::function<");
        generate(generated_types, type->return_type_, out_it);
        std::format_to(out_it, "{}", "(");
        const char* sep = "";
        for (const Type* param_type : type->parameters_) {
            std::format_to(out_it, "{}", sep);
            generate(generated_types, param_type, out_it);
            sep = ", ";
        }
        std::format_to(out_it, "{}", ")>");
    }

private:
    std::ofstream& stream_;
    GlobalMemory::Vector<const Type*>& generated_types_;
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;

public:
    TypeCodeGen(std::ofstream& stream, GlobalMemory::Vector<const Type*>& generated_types)
        : stream_(stream), generated_types_(generated_types) {}

    void operator()() {
        TypeSorter sorter;
        for (const StructType* type :
             std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        for (const InstanceType* type : TypeRegistry::instance->instance_types_) {
            sorter.add(type);
        }
        generated_types_.reserve(
            std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_).size() +
            TypeRegistry::instance->instance_types_.size()
        );
        std::ranges::copy(std::move(sorter).iterate(), std::back_inserter(generated_types_));
        std::ranges::sort(generated_types_);
        for (const Type* type : generated_types_) {
            switch (type->kind_) {
            case Kind::Struct:
                generate_struct(type->cast<StructType>());
                break;
            case Kind::Instance:
                generate_class(type->cast<InstanceType>());
                break;
            default:
                UNREACHABLE();
            }
        }
        stream_ << forward_declarations_ << "\n" << definitions_;
    }

    void generate_struct(const StructType* type) {
        std::size_t type_index = index(generated_types_, type);
        std::format_to(std::back_inserter(forward_declarations_), "struct t{};\n", type_index);
        std::format_to(std::back_inserter(definitions_), "struct t{} {{\n", type_index);
        for (const auto& [field_name, field_type] : type->fields_) {
            definitions_ += "    ";
            generate(generated_types_, field_type, std::back_inserter(definitions_));
            definitions_ += " ";
            definitions_ += field_name;
            definitions_ += ";\n";
        }
        definitions_ += "};\n";
    }

    void generate_class(const InstanceType* type) {
        std::size_t type_index = index(generated_types_, type);
        std::format_to(std::back_inserter(forward_declarations_), "struct t{};\n", type_index);
        std::format_to(std::back_inserter(definitions_), "struct t{} {{\n", type_index);
        for (const auto& [attr_name, attr_type] : type->attrs_) {
            definitions_ += "    ";
            generate(generated_types_, attr_type, std::back_inserter(definitions_));
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
    std::ofstream& stream_;
    CodeGenEnvironment env_;
    const Scope* current_scope_;
    GlobalMemory::Vector<const Type*> generated_types_;

public:
    CodeGen(
        std::ofstream& stream,
        CodeGenEnvironment& env,
        const Scope* current_scope,
        GlobalMemory::Vector<const Type*> generated_types
    ) noexcept
        : stream_(stream),
          env_(env),
          current_scope_(current_scope),
          generated_types_(std::move(generated_types)) {}

    CodeGen(const CodeGen& other, const Scope* current_scope) noexcept
        : stream_(other.stream_),
          env_(other.env_),
          current_scope_(current_scope),
          generated_types_(other.generated_types_) {}

    auto operator()(ASTNodeVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(ASTExprVariant variant) -> void {
        std::visit(
            [&]<typename T>(T node) -> void {
                if constexpr (std::is_convertible_v<T, const ASTExpression*>) {
                    if (auto* value = env_.find(current_scope_, node)) {
                        TypeCodeGen::generate(
                            generated_types_,
                            std::get<const Type*>(*value),
                            std::ostreambuf_iterator<char>(stream_)
                        );
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
            stream_ << sep << param.identifier;
            (*this)(param.type);
            sep = ", ";
        }
        stream_ << ") -> ";
        (*this)(node->return_type);
        stream_ << " {\n";
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
        (*this)(node->declared_type);
        stream_ << " " << node->identifier << ";\n";
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

auto codegen(SourceManager& sources, CodeGenEnvironment& codegen_env) -> int {
    GlobalMemory::String out_path = sources.files[0].path + ".cpp";
    std::ofstream out(out_path.c_str());
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return EXIT_FAILURE;
    }
    out << "#include <any>\n#include <cstdint>\n#include <functional>\n\n";

    GlobalMemory::Vector<const Type*> generated_types;
    TypeCodeGen{out, generated_types}();
    return EXIT_SUCCESS;
}
