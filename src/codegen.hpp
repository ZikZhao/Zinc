#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "type_check.hpp"

using TypeStore = std::span<const Type*>;

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

class ObjectCodeGen final {
public:
    static void output(GlobalMemory::String& out, const Object* obj, TypeStore types) {
        if (auto* type = obj->dyn_type()) {
            output(out, type, types);
        } else {
            output(out, obj->cast<Value>(), types);
        }
    }

    static void output(GlobalMemory::String& out, const Type* type, TypeStore types) {
        switch (type->kind_) {
        case Kind::Void:
            out += "void"sv;
            break;
        case Kind::Nullptr:
            out += "std::nullptr_t"sv;
            break;
        case Kind::Integer:
            output(out, type->cast<IntegerType>(), types);
            break;
        case Kind::Float:
            output(out, type->cast<FloatType>(), types);
            break;
        case Kind::Boolean:
            out += "bool"sv;
            break;
        case Kind::Function:
            output(out, type->cast<FunctionType>(), types);
            break;
        case Kind::Struct:
        case Kind::Instance:
            std::format_to(std::back_inserter(out), "t{}"sv, index(types, type));
            break;
        case Kind::Interface:
            /// TODO:
            break;
        case Kind::Mutable:
            /// TODO:
            output(out, type->cast<MutableType>()->target_type_, types);
            break;
        case Kind::Reference:
            output(out, type->cast<ReferenceType>()->target_type_, types);
            if (!type->dyn_cast<MutableType>()) {
                out += " const"sv;
            }
            out += "&"sv;
            break;
        case Kind::Pointer:
            output(out, type->cast<PointerType>()->target_type_, types);
            out += "*"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

    static void output(GlobalMemory::String& out, const IntegerType* type, TypeStore types) {
        std::format_to(std::back_inserter(out), "std::{}int"sv, type->is_signed_ ? "" : "u");
        switch (type->bits_) {
        case 8:
            out += "8_t"sv;
            break;
        case 16:
            out += "16_t"sv;
            break;
        case 32:
            out += "32_t"sv;
            break;
        case 64:
            out += "64_t"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

    static void output(GlobalMemory::String& out, const FloatType* type, TypeStore types) {
        if (type->bits_ == 32) {
            out += "float"sv;
        } else if (type->bits_ == 64) {
            out += "double"sv;
        } else {
            UNREACHABLE();
        }
    }

    static void output(GlobalMemory::String& out, const FunctionType* type, TypeStore types) {
        out += "std::function<"sv;
        output(out, type->return_type_, types);
        out += "("sv;
        strview sep = ""sv;
        for (const Type* param_type : type->parameters_) {
            out += sep;
            output(out, param_type, types);
            sep = ", "sv;
        }
        out += ")>"sv;
    }

    static void output(GlobalMemory::String& out, const Value* value, TypeStore types) {
        switch (value->kind_) {
        case Kind::Nullptr:
            out += "nullptr"sv;
            break;
        case Kind::Integer: {
            out += value->cast<IntegerValue>()->value_.to_string();
            break;
        }
        case Kind::Float:
            std::format_to(std::back_inserter(out), "0x{:a}"sv, value->cast<FloatValue>()->value_);
            break;
        case Kind::Boolean:
            out += value->cast<BooleanValue>()->value_ ? "true"sv : "false"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

private:
    GlobalMemory::Vector<const Type*>& types_;
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;

public:
    ObjectCodeGen(GlobalMemory::Vector<const Type*>& types) noexcept : types_(types) {}

    void order_types() {
        TypeSorter sorter;
        for (const StructType* type :
             std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        for (const InstanceType* type : TypeRegistry::instance->instance_types_) {
            sorter.add(type);
        }
        types_.reserve(
            std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_).size() +
            TypeRegistry::instance->instance_types_.size()
        );
        auto it = std::back_inserter(types_);
        std::ranges::copy(std::move(sorter).iterate(), it);
        std::ranges::sort(types_);
    }

    void output_type_defs(std::ofstream& stream) {
        for (const Type* type : types_) {
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
        stream << "// ----- Type Forward Declarations -----\n"sv;
        stream.write(
            forward_declarations_.data(), static_cast<std::streamsize>(forward_declarations_.size())
        );
        stream << "\n// ----- Type Definitions -----\n"sv;
        stream.write(definitions_.data(), static_cast<std::streamsize>(definitions_.size()));
    }

private:
    void generate_struct(const StructType* type) {
        std::size_t type_index = index(types_, type);
        auto it_fwd = std::back_inserter(forward_declarations_);
        auto it_def = std::back_inserter(definitions_);
        std::format_to(it_fwd, "struct t{};\n"sv, type_index);
        std::format_to(it_def, "struct t{} {{\n"sv, type_index);
        for (const auto& [field_name, field_type] : type->fields_) {
            definitions_ += "    "sv;
            output(definitions_, field_type, types_);
            definitions_ += " "sv;
            definitions_ += field_name;
            definitions_ += ";\n"sv;
        }
        definitions_ += "};\n"sv;
    }

    void generate_class(const InstanceType* type) {
        std::size_t type_index = index(types_, type);
        if (type->scope_->is_extern_) {
            std::format_to(std::back_inserter(definitions_), "using t{} = "sv, type_index);
            GlobalMemory::String qualified_name;
            strview sep = ""sv;
            for (const Scope* current = type->scope_; current; current = current->parent_) {
                if (!current->scope_id_.empty()) {
                    qualified_name.insert(0, sep);
                    qualified_name.insert(0, current->scope_id_);
                    sep = "::"sv;
                }
            }
            definitions_ += qualified_name;
            if (type->primary_template_) {
                sep = "<"sv;
                for (const Object* arg : type->template_args_) {
                    definitions_ += sep;
                    output(definitions_, arg, types_);
                    sep = ", "sv;
                }
                definitions_ += ">"sv;
            }
            definitions_ += ";\n"sv;
            return;
        }
        std::format_to(std::back_inserter(forward_declarations_), "struct t{};\n"sv, type_index);
        std::format_to(std::back_inserter(definitions_), "struct t{} {{\n"sv, type_index);
        for (const auto& [attr_name, attr_type] : type->attrs_) {
            definitions_ += "    "sv;
            output(definitions_, attr_type, types_);
            definitions_ += " "sv;
            definitions_ += attr_name;
            definitions_ += ";\n"sv;
        }
        /// TODO: methods with instantiations
        definitions_ += "};\n"sv;
    }
};

class NameMangler final {
private:
    std::span<const Type*> types_;

public:
    NameMangler(std::span<const Type*> types) noexcept : types_(types) {}

    auto mangle_all_instantiations(CodeGenEnvironment& env) const -> void {
        for (auto& [scope, args] : env.instantiations_) {
            if (scope->is_extern_) {
                continue;
            }
            GlobalMemory::String mangled_part = "0";
            for (const Object* arg : args) {
                std::size_t prev_size = mangled_part.size();
                if (auto* type = arg->dyn_type()) {
                    (*this)(mangled_part, type);
                } else {
                    (*this)(mangled_part, arg->cast<Value>());
                }
                std::format_to(
                    std::inserter(
                        mangled_part,
                        std::next(mangled_part.begin(), static_cast<std::ptrdiff_t>(prev_size))
                    ),
                    "{}"sv,
                    mangled_part.size() - prev_size
                );
            }
            scope->scope_id_ = GlobalMemory::persist(mangled_part);
        }
    }

    auto operator()(GlobalMemory::String& out, const Scope* scope) const -> void {
        if (scope->is_extern_) {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(std::back_inserter(out), "{}::"sv, current->scope_id_);
                }
            }(scope);
        } else {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(
                        std::back_inserter(out),
                        "_{}{}"sv,
                        current->scope_id_.length(),
                        current->scope_id_
                    );
                }
            }(scope);
        }
    }

    auto operator()(GlobalMemory::String& out, const Type* type) const -> void {
        switch (type->kind_) {
        case Kind::Void:
            out += "v"sv;
            break;
        case Kind::Nullptr:
            out += "n"sv;
            break;
        case Kind::Integer: {
            const IntegerType* int_type = type->cast<IntegerType>();
            std::format_to(
                std::back_inserter(out),
                "{}{}"sv,
                int_type->is_signed_ ? "i"sv : "u"sv,
                int_type->bits_
            );
            break;
        }
        case Kind::Float: {
            const FloatType* float_type = type->cast<FloatType>();
            std::format_to(std::back_inserter(out), "f{}"sv, float_type->bits_);
            break;
        }
        case Kind::Boolean:
            out += "b"sv;
            break;
        case Kind::Function: {
            out += "F"sv;
            const FunctionType* func_type = type->cast<FunctionType>();
            std::format_to(std::back_inserter(out), "{}", func_type->parameters_.size() + 1);
            (*this)(out, func_type->return_type_);
            for (const Type* param_type : func_type->parameters_) {
                (*this)(out, param_type);
            }
            break;
        }
        case Kind::Mutable:
            out += "m"sv;
            (*this)(out, type->cast<MutableType>()->target_type_);
            break;
        case Kind::Reference:
            out += "r"sv;
            (*this)(out, type->cast<ReferenceType>()->target_type_);
            break;
        case Kind::Pointer:
            out += "p"sv;
            (*this)(out, type->cast<PointerType>()->target_type_);
            break;
        case Kind::Struct:
        case Kind::Instance: {
            std::format_to(std::back_inserter(out), "t{}"sv, index(types_, type));
            break;
        }
        default:
            UNREACHABLE();
        }
    }

    auto operator()(GlobalMemory::String& out, const Value* value) const -> void { throw; }

    auto operator()(GlobalMemory::String& out, OperatorCode opcode) const noexcept -> void {
        switch (opcode) {
        case OperatorCode::Add:
            out += "add"sv;
            break;
        case OperatorCode::Subtract:
            out += "sub"sv;
            break;
        case OperatorCode::Negate:
            out += "neg"sv;
            break;
        case OperatorCode::Multiply:
            out += "mul"sv;
            break;
        case OperatorCode::Divide:
            out += "div"sv;
            break;
        case OperatorCode::Remainder:
            out += "mod"sv;
            break;
        case OperatorCode::Increment:
            out += "inc"sv;
            break;
        case OperatorCode::PostIncrement:
            out += "post_inc"sv;
            break;
        case OperatorCode::Decrement:
            out += "dec"sv;
            break;
        case OperatorCode::PostDecrement:
            out += "post_dec"sv;
            break;
        case OperatorCode::Equal:
            out += "eq"sv;
            break;
        case OperatorCode::NotEqual:
            out += "ne"sv;
            break;
        case OperatorCode::LessThan:
            out += "lt"sv;
            break;
        case OperatorCode::LessEqual:
            out += "le"sv;
            break;
        case OperatorCode::GreaterThan:
            out += "gt"sv;
            break;
        case OperatorCode::GreaterEqual:
            out += "ge"sv;
            break;
        case OperatorCode::LogicalAnd:
            out += "land"sv;
            break;
        case OperatorCode::LogicalOr:
            out += "lor"sv;
            break;
        case OperatorCode::LogicalNot:
            out += "lnot"sv;
            break;
        case OperatorCode::BitwiseAnd:
            out += "band"sv;
            break;
        case OperatorCode::BitwiseOr:
            out += "bor"sv;
            break;
        case OperatorCode::BitwiseXor:
            out += "bxor"sv;
            break;
        case OperatorCode::BitwiseNot:
            out += "bnot"sv;
            break;
        case OperatorCode::LeftShift:
            out += "shl"sv;
            break;
        case OperatorCode::RightShift:
            out += "shr"sv;
            break;
        case OperatorCode::Assign:
            out += "assign"sv;
            break;
        case OperatorCode::AddAssign:
            out += "add_assign"sv;
            break;
        case OperatorCode::SubtractAssign:
            out += "sub_assign"sv;
            break;
        case OperatorCode::MultiplyAssign:
            out += "mul_assign"sv;
            break;
        case OperatorCode::DivideAssign:
            out += "div_assign"sv;
            break;
        case OperatorCode::RemainderAssign:
            out += "mod_assign"sv;
            break;
        case OperatorCode::LogicalAndAssign:
            out += "land_assign"sv;
            break;
        case OperatorCode::LogicalOrAssign:
            out += "lor_assign"sv;
            break;
        case OperatorCode::BitwiseAndAssign:
            out += "band_assign"sv;
            break;
        case OperatorCode::BitwiseOrAssign:
            out += "bor_assign"sv;
            break;
        case OperatorCode::BitwiseXorAssign:
            out += "bxor_assign"sv;
            break;
        case OperatorCode::LeftShiftAssign:
            out += "shl_assign"sv;
            break;
        case OperatorCode::RightShiftAssign:
            out += "shr_assign"sv;
            break;
        case OperatorCode::Index:
            out += "index"sv;
            break;
        case OperatorCode::Call:
            out += "call"sv;
            break;
        case OperatorCode::Deref:
            out += "deref"sv;
            break;
        case OperatorCode::Pointer:
            out += "ptr"sv;
            break;
        case OperatorCode::SIZE:
            UNREACHABLE();
        }
    }
};

class CodeGen final {
private:
    class Guard final {
    private:
        CodeGen& gen_;
        const Scope* scope_;

    public:
        Guard(CodeGen& gen, const Scope* scope) noexcept
            : gen_(gen), scope_(std::exchange(gen_.current_scope_, scope)) {}
        Guard(CodeGen& gen, const ASTNode* key) noexcept
            : gen_(gen),
              scope_(std::exchange(gen_.current_scope_, gen_.current_scope_->children_.at(key))) {}
        ~Guard() noexcept { gen_.current_scope_ = scope_; }
    };

private:
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;
    CodeGenEnvironment env_;
    NameMangler mangler_;
    std::span<const Type*> types_;
    const Scope* current_scope_;
    std::size_t indent_level_;

public:
    CodeGen(CodeGenEnvironment& env, NameMangler& mangler, std::span<const Type*> types) noexcept
        : env_(env), mangler_(mangler), types_(types) {}

    auto operator()(std::ofstream& stream) -> void {
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
            } else if (auto* ctor_def = std::get_if<const ASTCtorDtorDefinition*>(&node)) {
                (*this)(*ctor_def, func_obj);
            } else if (auto* op_def = std::get_if<const ASTOperatorDefinition*>(&node)) {
                (*this)(*op_def, func_obj);
            } else {
                UNREACHABLE();
            }
            mangled_path.clear();
            newline(false);
        }
        stream << "\n// ----- Function Forward Declarations -----\n"sv;
        stream.write(
            forward_declarations_.data(), static_cast<std::streamsize>(forward_declarations_.size())
        );
        stream << "\n// ----- Function Definitions -----\n"sv;
        stream.write(definitions_.data(), static_cast<std::streamsize>(definitions_.size() - 1));
    }

    auto operator()(ASTNodeVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(ASTExprVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(std::monostate) -> void { UNREACHABLE(); }

    auto operator()(const auto*) -> void {}

    auto operator()(
        const ASTFunctionDefinition* node, strview mangled_path, const FunctionType* func_type
    ) -> void {
        auto gen = [&](GlobalMemory::String& out) {
            if (mangled_path == "main"sv) {
                out += "auto main(int $argc, char** $argv) -> int"sv;
            } else {
                std::format_to(std::back_inserter(out), "auto {}("sv, mangled_path);
                strview sep = ""sv;
                for (std::size_t i = 0; i < node->parameters.size(); i++) {
                    out += sep;
                    const ASTFunctionParameter& param = node->parameters[i];
                    ObjectCodeGen::output(out, func_type->parameters_[i], types_);
                    out += " "sv;
                    out += param.identifier;
                    sep = ", "sv;
                }
                out += ") -> "sv;
                ObjectCodeGen::output(out, func_type->return_type_, types_);
            }
        };
        gen(forward_declarations_);
        forward_declarations_ += ";\n"sv;
        gen(definitions_);
        definitions_ += " {"sv;
        indent_level_++;
        if (mangled_path == "main"sv) {
            newline();
            definitions_ += "const std::vector<std::string_view> $args_vec{$argv, $argv + $argc};";
            newline();
            definitions_ += "const std::span<const std::string_view> args{$args_vec};"sv;
        }
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline(false);
    }

    auto operator()(const ASTCtorDtorDefinition* node, const FunctionType* func_type) -> void {
        auto gen = [&](GlobalMemory::String& out) {
            std::format_to(
                std::back_inserter(out),
                "auto _init_t{}_0"sv,
                index(types_, func_type->return_type_)
            );
            for (const Type* param_type : func_type->parameters_) {
                mangler_(out, param_type);
            }
            out += "("sv;
            strview sep = ""sv;
            for (std::size_t i = 0; i < node->parameters.size(); i++) {
                out += sep;
                const ASTFunctionParameter& param = node->parameters[i];
                ObjectCodeGen::output(out, func_type->parameters_[i], types_);
                out += " "sv;
                out += param.identifier;
                sep = ", "sv;
            }
            out += ") -> "sv;
            ObjectCodeGen::output(out, func_type->return_type_, types_);
        };
        gen(forward_declarations_);
        forward_declarations_ += ";\n"sv;
        gen(definitions_);
        definitions_ += " {"sv;
        indent_level_++;
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline(false);
    }

    auto operator()(const ASTOperatorDefinition* node, const FunctionType* func_type) -> void {
        auto gen = [&](GlobalMemory::String& out) {
            out += "auto _op_"sv;
            mangler_(out, node->opcode);
            out += "_0"sv;
            for (const Type* param_type : func_type->parameters_) {
                mangler_(out, param_type);
            }
            out += "("sv;
            ObjectCodeGen::output(out, func_type->parameters_[0], types_);
            out += " "sv;
            out += node->left.identifier;
            if (node->right) {
                out += ", "sv;
                ObjectCodeGen::output(out, func_type->parameters_[1], types_);
                out += " "sv;
                out += node->right->identifier;
            }
            out += ") -> "sv;
            ObjectCodeGen::output(out, func_type->return_type_, types_);
        };
        gen(forward_declarations_);
        forward_declarations_ += ";\n"sv;
        gen(definitions_);
        definitions_ += " {"sv;
        indent_level_++;
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline(false);
    }

    auto operator()(const ASTLocalBlock* node) -> void {
        definitions_ += "{"sv;
        Guard guard{*this, node};
        indent_level_++;
        for (const ASTNodeVariant& child : node->statements) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
    }

    auto operator()(const ASTParenExpr* node) -> void {
        definitions_ += "("sv;
        (*this)(node->inner);
        definitions_ += ")"sv;
    }

    auto operator()(const ASTConstant* node) -> void {
        switch (node->value->kind_) {
        case Kind::Nullptr:
            definitions_ += "nullptr"sv;
            break;
        case Kind::Integer:
            definitions_ += node->value->cast<IntegerValue>()->value_.to_string();
            break;
        case Kind::Float:
            std::format_to(
                std::back_inserter(definitions_),
                "0x{:a}"sv,
                node->value->cast<FloatValue>()->value_
            );
            break;
        case Kind::Boolean:
            definitions_ += node->value->cast<BooleanValue>()->value_ ? "true"sv : "false"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

    auto operator()(const ASTStringConstant* node) -> void {
        definitions_ += "\"";
        definitions_ += escape_string(node->value);
        definitions_ += "\"sv";
    }

    auto operator()(const ASTSelfExpr* node) -> void {
        assert(!node->is_type);
        definitions_ += "self"sv;
    }

    auto operator()(const ASTIdentifier* node) -> void { definitions_ += node->str; }

    auto operator()(const ASTMemberAccess* node) -> void {
        if (auto* replacement = env_.find(current_scope_, node)) {
            const Scope* member_scope = std::get<const Scope*>(*replacement);
            if (member_scope->is_extern_) {
                (*this)(node->base);
                definitions_ += "::"sv;
                definitions_ += node->member;
            } else {
                mangler_(definitions_, member_scope);
            }
        } else {
            (*this)(node->base);
            definitions_ += "."sv;
            definitions_ += node->member;
        }
    }

    auto operator()(const ASTIndexAccess* node) -> void {
        const auto& [func_type, self_type, is_constructor, do_not_mangle] =
            std::get<CodeGenEnvironment::FunctionCall>(*env_.find(current_scope_, node));
        if (do_not_mangle) {
            (*this)(node->base);
            definitions_ += "["sv;
            (*this)(node->index);
            definitions_ += "]"sv;
        } else {
            definitions_ += "_op_"sv;
            mangler_(definitions_, OperatorCode::Index);
            definitions_ += "_0"sv;
            mangler_(definitions_, func_type->parameters_[0]);
            mangler_(definitions_, func_type->parameters_[1]);
            definitions_ += "("sv;
            (*this)(node->base);
            definitions_ += ", "sv;
            (*this)(node->index);
            definitions_ += ")"sv;
        }
    }

    auto operator()(const ASTUnaryOp* node) -> void {
        if (node->opcode == OperatorCode::PostIncrement ||
            node->opcode == OperatorCode::PostDecrement) {
            (*this)(node->expr);
            definitions_ += GetOperatorString(node->opcode);
        } else {
            definitions_ += GetOperatorString(node->opcode);
            (*this)(node->expr);
        }
    }

    auto operator()(const ASTBinaryOp* node) -> void {
        if (auto* replacement = env_.find(current_scope_, node)) {
            auto [func_type, self_type, _, do_not_mangle] =
                std::get<CodeGenEnvironment::FunctionCall>(*replacement);
            if (!do_not_mangle) {
                definitions_ += "_op_"sv;
                mangler_(definitions_, node->opcode);
                definitions_ += "_0"sv;
                mangler_(definitions_, func_type->parameters_[0]);
                if (!holds_monostate(node->right)) {
                    mangler_(definitions_, func_type->parameters_[1]);
                }
                definitions_ += "("sv;
                (*this)(node->left);
                if (!holds_monostate(node->right)) {
                    definitions_ += ", "sv;
                    (*this)(node->right);
                }
                definitions_ += ")"sv;
                return;
            }
        }
        (*this)(node->left);
        definitions_ += " "sv;
        definitions_ += GetOperatorString(node->opcode);
        definitions_ += " "sv;
        (*this)(node->right);
    }

    auto operator()(const ASTStructInitialization* node) -> void {
        if (!holds_monostate(node->struct_type)) {
            auto* variant = env_.find(current_scope_, node);
            const Type* struct_type = std::get<const Type*>(*variant);
            std::format_to(std::back_inserter(definitions_), "t{}"sv, index(types_, struct_type));
        }
        definitions_ += "{"sv;
        indent_level_++;
        for (const ASTFieldInitialization& field_init : node->field_inits) {
            newline();
            std::format_to(std::back_inserter(definitions_), ".{} = "sv, field_init.identifier);
            (*this)(field_init.value);
            definitions_ += ","sv;
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
    }

    auto operator()(const ASTArrayInitialization* node) -> void {
        definitions_ += "{"sv;
        strview sep = ""sv;
        for (const ASTExprVariant& elem : node->elements) {
            definitions_ += sep;
            (*this)(elem);
            sep = ", "sv;
        }
        definitions_ += "}"sv;
    }

    auto operator()(const ASTFunctionCall* node) -> void {
        const auto& [func_type, self_type, is_constructor, do_not_mangle] =
            std::get<CodeGenEnvironment::FunctionCall>(*env_.find(current_scope_, node));
        if (do_not_mangle) {
            (*this)(node->function);
        } else {
            if (is_constructor) {
                definitions_ += "_init_"sv;
                mangler_(definitions_, func_type->return_type_);
            } else {
                (*this)(node->function);
            }
            definitions_ += "_0"sv;
            if (self_type) {
                mangler_(definitions_, self_type);
            }
            for (auto* param_type : func_type->parameters_) {
                mangler_(definitions_, param_type);
            }
        }
        definitions_ += "("sv;
        const char* sep = "";
        for (ASTExprVariant arg : node->arguments) {
            definitions_ += sep;
            (*this)(arg);
            sep = ", ";
        }
        definitions_ += ")"sv;
    }

    auto operator()(const ASTTernaryOp* node) -> void {
        (*this)(node->condition);
        definitions_ += " ? "sv;
        (*this)(node->true_expr);
        definitions_ += " : "sv;
        (*this)(node->false_expr);
    }

    auto operator()(const ASTAs* node) -> void {
        auto* replacement = env_.find(current_scope_, node);
        definitions_ += "static_cast<"sv;
        ObjectCodeGen::output(definitions_, std::get<const Type*>(*replacement), types_);
        definitions_ += ">("sv;
        (*this)(node->expr);
        definitions_ += ")"sv;
    }

    auto operator()(const ASTLambda* node) -> void {
        definitions_ += "[&]("sv;
        strview sep = ""sv;
        auto* replacement = env_.find(current_scope_, node);
        const FunctionType* lambda_type = std::get<const Type*>(*replacement)->cast<FunctionType>();
        /// TODO: make lambda output as closure instead of directly outputting as C++ lambda
        for (size_t i = 0; i < node->parameters.size(); i++) {
            const auto& param = node->parameters[i];
            definitions_ += sep;
            ObjectCodeGen::output(definitions_, lambda_type->parameters_[i], types_);
            definitions_ += " "sv;
            definitions_ += param.identifier;
            sep = ", "sv;
        }
        definitions_ += ")"sv;
        if (lambda_type->return_type_->kind_ != Kind::Void) {
            definitions_ += " -> "sv;
            ObjectCodeGen::output(definitions_, lambda_type->return_type_, types_);
        }
        Guard guard{*this, node};
        if (auto* node_variant = std::get_if<ASTNodeVariant>(&node->body)) {
            (*this)(*node_variant);
        } else {
            definitions_ += " { return "sv;
            (*this)(std::get<ASTExprVariant>(node->body));
            definitions_ += "; }"sv;
        }
    }

    auto operator()(const ASTExpressionStatement* node) -> void {
        (*this)(node->expr);
        definitions_ += ";"sv;
    }

    auto operator()(const ASTDeclaration* node) -> void {
        const Type* type = std::get<const Type*>(*env_.find(current_scope_, node));
        ObjectCodeGen::output(definitions_, type, types_);
        std::format_to(std::back_inserter(definitions_), " {}", node->identifier);
        if (!holds_monostate(node->expr)) {
            definitions_ += " = "sv;
            (*this)(node->expr);
        }
        definitions_ += ";"sv;
    }

    auto operator()(const ASTIfStatement* node) -> void {
        Guard guard{*this, node};
        definitions_ += "if ("sv;
        (*this)(node->condition);
        definitions_ += ") "sv;
        (*this)(node->if_block);
        if (!holds_monostate(node->else_block)) {
            definitions_ += " else "sv;
            (*this)(node->else_block);
        }
    }

    auto operator()(const ASTSwitchStatement* node) -> void {
        Guard guard{*this, node};
        definitions_ += "switch ("sv;
        (*this)(node->condition);
        definitions_ += ") {\n"sv;
        for (const ASTSwitchCase& switch_case : node->cases) {
            newline();
            if (holds_monostate(switch_case.value)) {
                definitions_ += "default:\n"sv;
            } else {
                definitions_ += "case "sv;
                (*this)(switch_case.value);
                definitions_ += ":\n"sv;
            }
            indent_level_++;
            newline();
            (*this)(switch_case.body);
            indent_level_--;
        }
        newline();
        definitions_ += "}"sv;
    }

    auto operator()(const ASTForStatement* node) -> void {
        Guard guard{*this, node};
        definitions_ += "for ("sv;
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            (*this)(decl);
        } else if (auto* expr = std::get_if<ASTExprVariant>(&node->initializer)) {
            if (!holds_monostate(*expr)) {
                (*this)(*expr);
            }
            definitions_ += "; "sv;
        }
        if (!holds_monostate(node->condition)) {
            (*this)(node->condition);
        }
        definitions_ += "; "sv;
        if (!holds_monostate(node->increment)) {
            (*this)(node->increment);
        }
        definitions_ += ") "sv;
        (*this)(node->body);
    }

    auto operator()(const ASTReturnStatement* node) -> void {
        definitions_ += "return"sv;
        if (!holds_monostate(node->expr)) {
            definitions_ += " "sv;
            (*this)(node->expr);
        }
        definitions_ += ";"sv;
    }

    auto operator()(const ASTTemplateInstantiation* node) -> void {
        auto* replacement = env_.find(current_scope_, node);
        std::span<const Object*> args = std::get<std::span<const Object*>>(*replacement);
        (*this)(node->template_expr);
        strview sep = "<"sv;
        for (const Object* arg : args) {
            definitions_ += sep;
            if (auto* type = arg->dyn_type()) {
                ObjectCodeGen::output(definitions_, type, types_);
            } else {
                ObjectCodeGen::output(definitions_, arg->cast<Value>(), types_);
            }
            sep = ", "sv;
        }
        definitions_ += ">"sv;
    }

private:
    auto newline(bool no_repeat = true) -> void {
        if (no_repeat && !definitions_.empty() && definitions_.back() == '\n') {
            return;
        }
        definitions_ += "\n"sv;
        for (std::size_t i = 0; i < indent_level_; i++) {
            definitions_ += "    "sv;
        }
    }
};

auto codegen(SourceManager& sources, Sema& sema, CodeGenEnvironment& codegen_env) -> int {
    std::filesystem::path out_path = sources.files[1].relative_path_.concat(".cpp");
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return EXIT_FAILURE;
    }
    out << "#include <algorithm>\n"
           "#include <array>\n"
           "#include <cassert>\n"
           "#include <cmath>\n"
           "#include <compare>\n"
           "#include <concepts>\n"
           "#include <expected>\n"
           "#include <filesystem>\n"
           "#include <format>\n"
           "#include <fstream>\n"
           "#include <functional>\n"
           "#include <future>\n"
           "#include <generator>\n"
           "#include <initializer_list>\n"
           "#include <iostream>\n"
           "#include <iterator>\n"
           "#include <map>\n"
           "#include <memory>\n"
           "#include <memory_resource>\n"
           "#include <numeric>\n"
           "#include <print>\n"
           "#include <ranges>\n"
           "#include <set>\n"
           "#include <stdexcept>\n"
           "#include <string>\n"
           "#include <string_view>\n"
           "#include <tuple>\n"
           "#include <type_traits>\n"
           "#include <typeindex>\n"
           "#include <unordered_map>\n"
           "#include <utility>\n"
           "#include <vector>\n"
           "using namespace std::literals;\n\n";

    GlobalMemory::Vector<const Type*> types;
    ObjectCodeGen type_codegen{types};
    type_codegen.order_types();
    NameMangler mangler(types);
    mangler.mangle_all_instantiations(codegen_env);
    type_codegen.output_type_defs(out);
    CodeGen{codegen_env, mangler, types}(out);
    return EXIT_SUCCESS;
}
