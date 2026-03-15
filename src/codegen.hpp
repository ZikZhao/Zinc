#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "type_check.hpp"

using TypeSeq = std::span<const Type*>;

class TypeSorter final {
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
    static void output(const Type* type, TypeSeq types, GlobalMemory::String& out) {
        switch (type->kind_) {
        case Kind::Unknown:
            UNREACHABLE();
        case Kind::Void:
            out += "void";
            break;
        case Kind::Any:
            out += "std::any";
            break;
        case Kind::Nullptr:
            out += "std::nullptr_t";
            break;
        case Kind::Integer:
            output(type->cast<IntegerType>(), types, out);
            break;
        case Kind::Float:
            output(type->cast<FloatType>(), types, out);
            break;
        case Kind::Boolean:
            out += "bool";
            break;
        case Kind::Function:
            output(type->cast<FunctionType>(), types, out);
            break;
        case Kind::Array:
            /// TODO:
            break;
        case Kind::Struct:
        case Kind::Instance:
            std::format_to(std::back_inserter(out), "t{}", index(types, type));
            break;
        case Kind::Interface:
            /// TODO:
            break;
        case Kind::Mutable:
            /// TODO:
            output(type->cast<MutableType>()->target_type_, types, out);
            break;
        case Kind::Reference:
            output(type->cast<ReferenceType>()->referenced_type_, types, out);
            out += "&";
            break;
        case Kind::Pointer:
            output(type->cast<PointerType>()->pointed_type_, types, out);
            out += "*";
            break;
        default:
            /// TODO:
            assert(false);
        }
    }

    static void output(const IntegerType* type, TypeSeq types, GlobalMemory::String& out) {
        std::format_to(std::back_inserter(out), "std::{}int", type->is_signed_ ? "" : "u");
        switch (type->bits_) {
        case 8:
            out += "8_t";
            break;
        case 16:
            out += "16_t";
            break;
        case 32:
            out += "32_t";
            break;
        case 64:
            out += "64_t";
            break;
        default:
            UNREACHABLE();
        }
    }

    static void output(const FloatType* type, TypeSeq types, GlobalMemory::String& out) {
        if (type->bits_ == 32) {
            out += "float";
        } else if (type->bits_ == 64) {
            out += "double";
        } else {
            UNREACHABLE();
        }
    }

    static void output(const FunctionType* type, TypeSeq types, GlobalMemory::String& out) {
        out += "std::function<";
        output(type->return_type_, types, out);
        out += "(";
        const char* sep = "";
        for (const Type* param_type : type->parameters_) {
            out += sep;
            output(param_type, types, out);
            sep = ", ";
        }
        out += ")>";
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
            output(field_type, generated_types_, definitions_);
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
            output(attr_type, generated_types_, definitions_);
            definitions_ += " ";
            definitions_ += attr_name;
            definitions_ += ";\n";
        }
        /// TODO: methods with instantiations
        definitions_ += "};\n";
    }
};

class ValueCodeGen final {
public:
    void operator()(const Value* value, TypeSeq types, GlobalMemory::String& out) {
        switch (value->kind_) {
        case Kind::Any:
            std::format_to(std::back_inserter(out), "{}", "std::any{}");
            break;
        case Kind::Nullptr:
            std::format_to(std::back_inserter(out), "{}", "nullptr");
            break;
        case Kind::Integer: {
            GlobalMemory::String int_repr = value->cast<IntegerValue>()->value_.to_string();
            std::format_to(std::back_inserter(out), "{}", int_repr);
            break;
        }
        case Kind::Float:
            std::format_to(std::back_inserter(out), "{:a}", value->cast<FloatValue>()->value_);
            break;
        case Kind::Boolean:
            std::format_to(
                std::back_inserter(out),
                "{}",
                value->cast<BooleanValue>()->value_ ? "true" : "false"
            );
            break;
        case Kind::Array:
            throw;
            break;
        case Kind::Function:
            throw;
            break;
        case Kind::Struct: {
            std::format_to(
                std::back_inserter(out), "{}{{", index(types, value->cast<StructValue>()->type_)
            );
            const char* sep = "";
            for (const auto& [field_name, field_value] : value->cast<StructValue>()->fields_) {
                std::format_to(std::back_inserter(out), "{}.{} = ", sep, field_name);
                (*this)(field_value, types, out);
                sep = ", ";
            }
            out += "}";
            break;
        }
        case Kind::Instance: {
            std::format_to(
                std::back_inserter(out), "{}{{", index(types, value->cast<InstanceValue>()->type_)
            );
            const char* sep = "";
            for (const auto& [field_name, field_value] : value->cast<InstanceValue>()->attrs_) {
                std::format_to(std::back_inserter(out), "{}.{} = ", sep, field_name);
                (*this)(field_value, types, out);
                sep = ", ";
            }
            out += "}";
            break;
        }
        default:
            UNREACHABLE();
        }
    }
};

class NameMangler final {
private:
    std::span<const Type*> types_;

public:
    NameMangler(std::span<const Type*> types) noexcept : types_(types) {}

    auto mangle_all_instantiations(CodeGenEnvironment& env) const -> void {
        for (auto& [scope, args] : env.instantiations_) {
            GlobalMemory::String mangled_part = "0";
            for (const Object* arg : args) {
                std::size_t prev_size = mangled_part.size();
                if (auto* type = arg->dyn_type()) {
                    (*this)(type, mangled_part);
                } else {
                    (*this)(arg->cast<Value>(), mangled_part);
                }
                std::format_to(
                    std::inserter(
                        mangled_part,
                        std::next(mangled_part.begin(), static_cast<std::ptrdiff_t>(prev_size))
                    ),
                    "{}",
                    mangled_part.size() - prev_size
                );
            }
            scope->scope_id_ = GlobalMemory::persist(mangled_part);
        }
    }

    auto operator()(const Scope* scope, GlobalMemory::String& mangled) const -> void {
        if (scope->is_extern_) {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(std::back_inserter(mangled), "{}::", current->scope_id_);
                }
            }(scope);
        } else {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(
                        std::back_inserter(mangled),
                        "_{}{}",
                        current->scope_id_.length(),
                        current->scope_id_
                    );
                }
            }(scope);
        }
    }

    auto operator()(const Type* type, GlobalMemory::String& mangled) const -> void {
        switch (type->kind_) {
        case Kind::Unknown:
            UNREACHABLE();
        case Kind::Void:
            mangled += "v";
            break;
        case Kind::Any:
            mangled += "a";
            break;
        case Kind::Nullptr:
            mangled += "n";
            break;
        case Kind::Integer: {
            const IntegerType* int_type = type->cast<IntegerType>();
            std::format_to(
                std::back_inserter(mangled),
                "{}{}",
                int_type->is_signed_ ? "i" : "u",
                int_type->bits_
            );
            break;
        }
        case Kind::Float: {
            const FloatType* float_type = type->cast<FloatType>();
            std::format_to(std::back_inserter(mangled), "f{}", float_type->bits_);
            break;
        }
        case Kind::Boolean:
            mangled += "b";
            break;
        case Kind::Array:
            mangled += "A";
            (*this)(type->cast<ArrayType>()->element_type_, mangled);
            break;
        case Kind::Function:
        case Kind::Interface:
        case Kind::Mutable:
        case Kind::Reference:
        case Kind::Pointer:
            /// TODO
            UNREACHABLE();
            break;
        case Kind::Struct:
        case Kind::Instance: {
            std::format_to(std::back_inserter(mangled), "t{}", index(types_, type));
            break;
        }
        default:
            UNREACHABLE();
        }
    }

    auto operator()(const Value* value, GlobalMemory::String& mangled) const -> void {
        assert(false);
    }
};

class CodeGen final {
private:
    class Guard final {
    private:
        CodeGen& gen_;
        const Scope* scope_;
        std::size_t indent_;

    public:
        Guard(CodeGen& gen, const Scope* scope) noexcept
            : gen_(gen), scope_(std::exchange(gen_.current_scope_, scope)), indent_(0) {}
        Guard(CodeGen& gen, const ASTNode* key) noexcept
            : gen_(gen),
              scope_(std::exchange(gen_.current_scope_, gen_.current_scope_->children_.at(key))),
              indent_(gen_.indent_level_++) {}
        ~Guard() noexcept {
            gen_.current_scope_ = scope_;
            gen_.indent_level_ = indent_;
        }
    };

private:
    std::ofstream& stream_;
    CodeGenEnvironment env_;
    NameMangler mangler_;
    std::span<const Type*> types_;
    GlobalMemory::String buffer_;
    const Scope* current_scope_;
    std::size_t indent_level_;

public:
    CodeGen(
        std::ofstream& stream,
        CodeGenEnvironment& env,
        NameMangler& mangler,
        std::span<const Type*> types
    ) noexcept
        : stream_(stream), env_(env), mangler_(mangler), types_(types) {
        buffer_.reserve(8192);
    }

    auto operator()() -> void {
        GlobalMemory::String mangled_path;
        for (const auto& [scope, node, func_obj] : env_.functions_) {
            current_scope_ = scope;
            if (auto* func_def = std::get_if<const ASTFunctionDefinition*>(&node)) {
                if ((*func_def)->identifier == "main" && scope->parent_->parent_ == nullptr) {
                    (*this)(*func_def, "main", func_obj);
                } else {
                    CodeGenEnvironment::mangle_path(mangled_path, scope, (*func_def)->identifier);
                    (*this)(*func_def, mangled_path, func_obj);
                }
            } else if (
                auto* ctor_def = std::get_if<const ASTConstructorDestructorDefinition*>(&node)
            ) {
                CodeGenEnvironment::mangle_path(mangled_path, scope, "init");
                (*this)(*ctor_def, mangled_path, func_obj);
            } else {
                UNREACHABLE();
            }
            mangled_path.clear();
            newline();
            stream_ << buffer_;
            buffer_.clear();
        }
    }

    auto operator()(ASTNodeVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(ASTExprVariant variant) -> void {
        std::visit(
            [&]<typename T>(T node) -> void {
                if constexpr (std::is_convertible_v<T, const ASTExpression*>) {
                    if (auto* value = env_.find(current_scope_, node)) {
                        TypeCodeGen::output(std::get<const Type*>(*value), types_, buffer_);
                        return;
                    }
                }
                return std::visit(*this, variant);
            },
            variant
        );
    }

    auto operator()(std::monostate) -> void { UNREACHABLE(); }

    auto operator()(const auto*) -> void {}

    auto operator()(
        const ASTFunctionDefinition* node,
        std::string_view mangled_path,
        const FunctionType* func_obj
    ) -> void {
        buffer_.append("auto ").append(mangled_path).append("(");
        const char* sep = "";
        for (std::size_t i = 0; i < node->parameters.size(); i++) {
            buffer_ += sep;
            const ASTFunctionParameter& param = node->parameters[i];
            TypeCodeGen::output(func_obj->parameters_[i], types_, buffer_);
            buffer_.append(" ").append(param.identifier);
            sep = ", ";
        }
        buffer_ += ") -> ";
        if (mangled_path == "main") {
            buffer_ += "int";
        } else {
            TypeCodeGen::output(func_obj->return_type_, types_, buffer_);
        }
        buffer_ += " {";
        indent_level_++;
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        buffer_ += "}";
        newline();
    }

    auto operator()(
        const ASTConstructorDestructorDefinition* node,
        std::string_view mangled_path,
        const FunctionType* func_obj
    ) -> void {
        buffer_.append("auto ").append(mangled_path).append("(");
        const char* sep = "";
        for (std::size_t i = 0; i < node->parameters.size(); i++) {
            buffer_ += sep;
            const ASTFunctionParameter& param = node->parameters[i];
            output_type(func_obj->parameters_[i]);
            buffer_.append(" ").append(param.identifier);
            sep = ", ";
        }
        buffer_ += ") -> ";
        output_type(func_obj->return_type_);
        buffer_ += " {";
        indent_level_++;
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        buffer_ += "}";
        newline();
    }

    auto operator()(const ASTLocalBlock* node) -> void {
        Guard guard{*this, node};
        for (const ASTNodeVariant& child : node->statements) {
            (*this)(child);
        }
    }

    auto operator()(const ASTParenExpr* node) -> void {
        buffer_ += "(";
        (*this)(node->inner);
        buffer_ += ")";
    }

    auto operator()(const ASTConstant* node) -> void {
        /// TODO:
        // buffer_ << node->value;
    }

    auto operator()(const ASTSelfExpr* node) -> void {
        buffer_ += (node->is_type ? "decltype(*this)" : "(*this)");
    }

    auto operator()(const ASTIdentifier* node) -> void { buffer_ += node->str; }

    template <ASTUnaryOpClass Op>
    auto operator()(const Op* node) -> void {
        /// TODO: postfix unary ops
        buffer_ += GetOperatorString(Op::opcode);
        (*this)(node->expr);
    }

    template <ASTBinaryOpClass Op>
    auto operator()(const Op* node) -> void {
        (*this)(node->left);
        buffer_.append(" ").append(GetOperatorString(Op::opcode)).append(" ");
        (*this)(node->right);
    }

    auto operator()(const ASTStructInitialization* node) -> void {
        buffer_ += "{\n";
        for (const ASTFieldInitialization& field_init : node->field_inits) {
            buffer_.append(".").append(field_init.identifier).append(" = ");
            (*this)(field_init.value);
            buffer_ += ",";
            newline();
        }
        buffer_ += "}";
    }

    auto operator()(const ASTFunctionCall* node) -> void {
        (*this)(node->function);
        buffer_ += "(";
        const char* sep = "";
        for (const ASTExprVariant& arg : node->arguments) {
            buffer_ += sep;
            (*this)(arg);
            sep = ", ";
        }
        buffer_ += ")";
    }

    auto operator()(const ASTExpressionStatement* node) -> void {
        (*this)(node->expr);
        buffer_ += ";";
    }

    auto operator()(const ASTDeclaration* node) -> void {
        output_type(std::get<const Type*>(*env_.find(current_scope_, node)));
        buffer_.append(" ").append(node->identifier);
        if (nonnull(node->expr)) {
            buffer_ += " = ";
            (*this)(node->expr);
        }
        buffer_ += ";";
    }

    auto operator()(const ASTIfStatement* node) -> void {
        buffer_ += "if (";
        (*this)(node->condition);
        buffer_ += ") ";
        (*this)(node->if_block);
        if (node->else_block) {
            buffer_ += " else ";
            (*this)(node->else_block);
            ;
        }
    }

    auto operator()(const ASTForStatement* node) -> void {
        buffer_ += "for (";
        if (node->initializer_decl) {
            (*this)(node->initializer_decl);
        } else if (nonnull(node->initializer_expr)) {
            (*this)(node->initializer_expr);
        }
        buffer_ += "; ";
        if (nonnull(node->condition)) {
            (*this)(node->condition);
        }
        buffer_ += "; ";
        if (nonnull(node->increment)) {
            (*this)(node->increment);
        }
        buffer_ += ") ";
        (*this)(node->body);
    }

private:
    auto newline() -> void {
        buffer_ += "\n";
        for (std::size_t i = 0; i < indent_level_; i++) {
            buffer_ += "    ";
        }
    }

    auto output_type(const Type* type) -> void { TypeCodeGen::output(type, types_, buffer_); }
};

auto codegen(SourceManager& sources, Sema& sema, CodeGenEnvironment& codegen_env) -> int {
    GlobalMemory::String out_path = sources.files[0].path + ".cpp";
    std::ofstream out(out_path.c_str());
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return EXIT_FAILURE;
    }
    out << "#include <any>\n#include <cstdint>\n#include <functional>\n\n";

    GlobalMemory::Vector<const Type*> types;
    TypeCodeGen{out, types}();
    NameMangler mangler(types);
    mangler.mangle_all_instantiations(codegen_env);
    CodeGen{out, codegen_env, mangler, types}();
    return EXIT_SUCCESS;
}
