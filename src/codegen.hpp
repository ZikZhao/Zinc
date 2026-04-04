#pragma once
#include "pch.hpp"

#include "object.hpp"
#include "type_check.hpp"

inline constexpr strview output_header =
    "#include <algorithm>\n"
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
    "#include <initializer_list>\n"
    "#include <iostream>\n"
    "#include <iterator>\n"
    "#include <map>\n"
    "#include <memory>\n"
    "#include <memory_resource>\n"
    "#include <numeric>\n"
    "#include <ranges>\n"
    "#include <set>\n"
    "#include <stdexcept>\n"
    "#include <string>\n"
    "#include <string_view>\n"
    "#include <tuple>\n"
    "#include <type_traits>\n"
    "#include <typeindex>\n"
    "#include <unordered_map>\n"
    "#include <unordered_set>\n"
    "#include <utility>\n"
    "#include <variant>\n"
    "#include <vector>\n"
    "using namespace std::literals;\n\n";

using TypeMap = GlobalMemory::FlatMap<const Type*, std::size_t>;

auto flush_without_sdl_prefix(strview code, std::ostream& out) -> void {
    while (true) {
        std::size_t sdl_pos = code.find("sdl::SDL", 0);
        if (sdl_pos == strview::npos) {
            out.write(code.data(), static_cast<std::streamsize>(code.size()));
            break;
        }
        out.write(code.data(), static_cast<std::streamsize>(sdl_pos));
        code = code.substr(sdl_pos + 5);
    }
}

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

    auto size() const noexcept -> std::size_t { return types_.size(); }

    void add(const Type* type) noexcept {
        if (!types_.insert(type).second) return;
        switch (type->kind_) {
        case Kind::Function:
            for (const Type* param_type : type->cast<FunctionType>()->parameters_) {
                edges_.insert({.child = param_type, .parent = type});
                add(param_type);
            }
            edges_.insert({.child = type->cast<FunctionType>()->return_type_, .parent = type});
            add(type->cast<FunctionType>()->return_type_);
            break;
        case Kind::Struct:
            for (const auto& field : type->cast<StructType>()->fields_) {
                edges_.insert({.child = field.second, .parent = type});
                add(field.second);
            }
            break;
        case Kind::Instance: {
            const InstanceType* instance = type->cast<InstanceType>();
            for (const auto& attr : type->cast<InstanceType>()->attrs_) {
                edges_.insert({.child = attr.second, .parent = type});
                add(attr.second);
            }
            if (instance->primary_template_ && instance->scope_->is_extern_) {
                for (const Object* template_arg : instance->template_args_) {
                    if (auto template_arg_type = template_arg->dyn_cast<Type>()) {
                        edges_.insert({.child = template_arg_type, .parent = type});
                        add(template_arg_type);
                    }
                }
            }
            break;
        }
        case Kind::Reference:
            if (!is_forward_declared(type->cast<ReferenceType>()->target_type_)) {
                edges_.insert({.child = type->cast<ReferenceType>()->target_type_, .parent = type});
            }
            add(type->cast<ReferenceType>()->target_type_);
            break;
        case Kind::Pointer:
            if (!is_forward_declared(type->cast<PointerType>()->target_type_)) {
                edges_.insert({.child = type->cast<PointerType>()->target_type_, .parent = type});
            }
            add(type->cast<PointerType>()->target_type_);
            break;
        case Kind::Union:
            for (const Type* variant_type : type->cast<UnionType>()->types_) {
                edges_.insert({.child = variant_type, .parent = type});
                add(variant_type);
            }
            break;
        case Kind::Dynamic:
            edges_.insert({.child = type->cast<DynamicType>()->target_type_, .parent = type});
            add(type->cast<DynamicType>()->target_type_);
        default:
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
            // if (child->kind_ == Kind::Struct || child->kind_ == Kind::Instance ||
            //     child->kind_ == Kind::Union) {
            //     co_yield child;
            // }
            switch (child->kind_) {
            case Kind::Struct:
            case Kind::Interface:
            case Kind::Instance:
            case Kind::Dynamic:
            case Kind::Union:
                co_yield child;
                break;
            default:
                break;
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

private:
    auto is_forward_declared(const Type* type) const noexcept -> bool {
        if (auto inst_type = type->dyn_cast<InstanceType>()) {
            return !inst_type->scope_->is_extern_;
        } else if (type->dyn_cast<StructType>()) {
            return true;
        }
        return false;
    }
};

class ObjectGen final {
public:
    static void output(GlobalMemory::String& out, const Object* obj, TypeMap& type_map) {
        if (auto* type = obj->dyn_cast<Type>()) {
            output(out, type, type_map);
        } else {
            output(out, obj->cast<Value>(), type_map);
        }
    }

    static void output(GlobalMemory::String& out, const Type* type, TypeMap& type_map) {
        switch (type->kind_) {
        case Kind::Struct:
        case Kind::Interface:
        case Kind::Instance:
        case Kind::Dynamic:
        case Kind::Union:
            std::format_to(std::back_inserter(out), "_t{}"sv, type_map.at(type));
            break;
        case Kind::Void:
            out += "void"sv;
            break;
        case Kind::Nullptr:
            out += "std::nullptr_t"sv;
            break;
        case Kind::Integer:
            output(out, type->cast<IntegerType>(), type_map);
            break;
        case Kind::Float:
            output(out, type->cast<FloatType>(), type_map);
            break;
        case Kind::Boolean:
            out += "bool"sv;
            break;
        case Kind::Function:
            output(out, type->cast<FunctionType>(), type_map);
            break;
        case Kind::Reference:
            output(out, type->cast<ReferenceType>()->target_type_, type_map);
            if (type->cast<ReferenceType>()->is_moved_) {
                out += "&&"sv;
            } else {
                if (!type->cast<ReferenceType>()->is_mutable_) {
                    out += " const"sv;
                }
                out += "&"sv;
            }
            break;
        case Kind::Pointer:
            output(out, type->cast<PointerType>()->target_type_, type_map);
            if (!type->cast<PointerType>()->is_mutable_) {
                out += " const"sv;
            }
            out += "*"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

    static void output(GlobalMemory::String& out, const IntegerType* type, TypeMap& type_map) {
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

    static void output(GlobalMemory::String& out, const FloatType* type, TypeMap& type_map) {
        if (type->bits_ == 32) {
            out += "float"sv;
        } else if (type->bits_ == 64) {
            out += "double"sv;
        } else {
            UNREACHABLE();
        }
    }

    static void output(GlobalMemory::String& out, const FunctionType* type, TypeMap& type_map) {
        out += "std::function<"sv;
        output(out, type->return_type_, type_map);
        out += "("sv;
        strview sep = ""sv;
        for (const Type* param_type : type->parameters_) {
            out += sep;
            output(out, param_type, type_map);
            sep = ", "sv;
        }
        out += ")>"sv;
    }

    static void output(GlobalMemory::String& out, const Value* value, TypeMap& type_map) {
        switch (value->kind_) {
        case Kind::Nullptr:
            out += "nullptr"sv;
            break;
        case Kind::Integer: {
            const IntegerValue* int_value = value->cast<IntegerValue>();
            if (int_value->type_->is_signed_) {
                switch (int_value->type_->bits_) {
                case 8:
                    std::format_to(
                        std::back_inserter(out), "std::int8_t{{{}}}", int_value->signed_value_
                    );
                    break;
                case 16:
                    std::format_to(
                        std::back_inserter(out), "std::int16_t{{{}}}", int_value->signed_value_
                    );
                    break;
                case 32:
                    std::format_to(
                        std::back_inserter(out), "std::int32_t{{{}}}", int_value->signed_value_
                    );
                    break;
                case 64:
                    std::format_to(
                        std::back_inserter(out), "std::int64_t{{{}}}", int_value->signed_value_
                    );
                    break;
                default:
                    UNREACHABLE();
                }
            } else {
                switch (int_value->type_->bits_) {
                case 8:
                    std::format_to(
                        std::back_inserter(out), "std::uint8_t{{{}}}", int_value->unsigned_value_
                    );
                    break;
                case 16:
                    std::format_to(
                        std::back_inserter(out), "std::uint16_t{{{}}}", int_value->unsigned_value_
                    );
                    break;
                case 32:
                    std::format_to(
                        std::back_inserter(out), "std::uint32_t{{{}u}}", int_value->unsigned_value_
                    );
                    break;
                case 64:
                    std::format_to(
                        std::back_inserter(out), "std::uint64_t{{{}u}}", int_value->unsigned_value_
                    );
                    break;
                default:
                    UNREACHABLE();
                }
            }
            break;
        }
        case Kind::Float: {
            // std::format does not output hexfloat with the "0x" prefix, so we use
            // std::ostringstream instead
            std::ostringstream oss;
            oss << std::hexfloat << value->cast<FloatValue>()->value_;
            out += oss.str();
            if (value->cast<FloatValue>()->type_->bits_ == 32) {
                out += "f"sv;
            }
            break;
        }
        case Kind::Boolean:
            out += value->cast<BooleanValue>()->value_ ? "true"sv : "false"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

private:
    TypeMap& type_map_;
    GlobalMemory::Vector<const Type*> types_;
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;

public:
    ObjectGen(TypeMap& type_map) noexcept : type_map_(type_map) {}

    void sort_types() {
        TypeSorter sorter;
        for (const StructType* type :
             std::get<TypeRegistry::TypeSet<StructType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        for (const InterfaceType* type : TypeRegistry::instance->interface_types_) {
            sorter.add(type);
        }
        for (const InstanceType* type : TypeRegistry::instance->instance_types_) {
            sorter.add(type);
        }
        for (const DynamicType* type :
             std::get<TypeRegistry::TypeSet<DynamicType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        for (const UnionType* type :
             std::get<TypeRegistry::TypeSet<UnionType>>(TypeRegistry::instance->types_)) {
            sorter.add(type);
        }
        std::ranges::copy(std::move(sorter).iterate(), std::back_inserter(types_));
        type_map_ =
            TypeMap(std::from_range, std::views::zip(types_, std::views::iota(std::size_t{0})));
    }

    void output_type_defs(std::ofstream& stream) {
        for (const Type* type : types_) {
            switch (type->kind_) {
            case Kind::Struct:
                generate_struct(type->cast<StructType>());
                break;
            case Kind::Interface:
                generate_interface(type->cast<InterfaceType>());
                break;
            case Kind::Instance:
                generate_class(type->cast<InstanceType>());
                break;
            case Kind::Union:
                generate_union(type->cast<UnionType>());
                break;
            case Kind::Dynamic:
                generate_dynamic(type->cast<DynamicType>());
                break;
            default:
                UNREACHABLE();
            }
        }
        stream << "// ----- Type Forward Declarations -----\n"sv;
        flush_without_sdl_prefix(forward_declarations_, stream);
        stream << "\n// ----- Type Definitions -----\n"sv;
        flush_without_sdl_prefix(definitions_, stream);
    }

private:
    void generate_struct(const StructType* type) {
        std::size_t type_index = type_map_.at(type);
        auto it_fwd = std::back_inserter(forward_declarations_);
        auto it_def = std::back_inserter(definitions_);
        std::format_to(it_fwd, "struct _t{};\n"sv, type_index);
        std::format_to(it_def, "struct _t{} {{\n"sv, type_index);
        for (const auto& [field_name, field_type] : type->fields_) {
            definitions_ += "    "sv;
            output(definitions_, field_type, type_map_);
            definitions_ += " "sv;
            definitions_ += field_name;
            definitions_ += ";\n"sv;
        }
        definitions_ += "};\n"sv;
    }

    void generate_interface(const InterfaceType* type) {
        std::size_t type_index = type_map_.at(type);
        std::format_to(std::back_inserter(forward_declarations_), "struct _t{};\n"sv, type_index);
        std::format_to(std::back_inserter(definitions_), "struct _t{} {{}};\n"sv, type_index);
    }

    void generate_class(const InstanceType* type) {
        std::size_t type_index = type_map_.at(type);
        if (type->scope_->is_extern_) {
            std::format_to(
                std::back_inserter(forward_declarations_), "#define _t{} "sv, type_index
            );
            GlobalMemory::String qualified_name;
            strview sep = ""sv;
            for (const Scope* current = type->scope_->parent_; current;
                 current = current->parent_) {
                if (!current->scope_id_.empty()) {
                    qualified_name.insert(0, sep);
                    qualified_name.insert(0, current->scope_id_);
                    sep = "::"sv;
                }
            }
            forward_declarations_ += qualified_name;
            if (!type->primary_template_) {
                forward_declarations_ += sep;
                forward_declarations_ += type->identifier_;
            }
            forward_declarations_ += "\n"sv;
        } else {
            std::format_to(
                std::back_inserter(forward_declarations_), "struct _t{};\n"sv, type_index
            );
            std::format_to(std::back_inserter(definitions_), "struct _t{} {{\n"sv, type_index);
            for (const auto& [attr_name, attr_type] : type->attrs_) {
                definitions_ += "    "sv;
                output(definitions_, attr_type, type_map_);
                definitions_ += " "sv;
                definitions_ += attr_name;
                definitions_ += ";\n"sv;
            }
            if (type->scope_->find(destructor_symbol)) {
                std::format_to(
                    std::back_inserter(definitions_), "    ~_t{}();\n"sv, type_map_.at(type)
                );
            }
            definitions_ += "};\n"sv;
        }
    }

    void generate_dynamic(const DynamicType* type) {
        std::size_t type_index = type_map_.at(type);
        std::format_to(std::back_inserter(forward_declarations_), "struct _t{};\n"sv, type_index);
        std::format_to(std::back_inserter(definitions_), "struct _t{} {{\n"sv, type_index);
        std::format_to(
            std::back_inserter(definitions_), "    void* _data;\n", type_map_.at(type->target_type_)
        );
        definitions_ += "    std::uint64_t _type_index;\n"sv;
        for (const InstanceType* implementor : type->target_type_->implementors_) {
            std::format_to(
                std::back_inserter(definitions_),
                "    _t{}(const _t{}* target) noexcept: _data(const_cast<void*>(static_cast<const void*>(target))), _type_index({}) {{}}\n"sv,
                type_map_.at(type),
                type_map_.at(implementor),
                type_map_.at(implementor)
            );
        }
        definitions_ += "};\n"sv;
    }

    void generate_union(const UnionType* type) {
        std::format_to(
            std::back_inserter(definitions_), "#define _t{} std::variant<"sv, type_map_.at(type)
        );
        strview sep = ""sv;
        for (const Type* variant_type : type->cast<UnionType>()->types_) {
            definitions_ += sep;
            output(definitions_, variant_type, type_map_);
            sep = ", "sv;
        }
        definitions_ += ">\n"sv;
    }
};

class NameMangler final {
private:
    TypeMap& type_map_;

public:
    NameMangler(TypeMap& type_map) noexcept : type_map_(type_map) {}

    auto mangle_all_instantiations(CodeGenEnvironment& env) const -> void {
        for (auto& [scope, args] : env.instantiations_) {
            GlobalMemory::String qualified_name;
            qualified_name += scope->scope_id_;
            if (scope->is_extern_) {
                qualified_name += "<"sv;
                strview sep = ""sv;
                for (const Object* arg : args) {
                    qualified_name += sep;
                    if (auto* type = arg->dyn_cast<Type>()) {
                        ObjectGen::output(qualified_name, type, type_map_);
                    } else {
                        ObjectGen::output(qualified_name, arg->cast<Value>(), type_map_);
                    }
                    sep = ", "sv;
                }
                qualified_name += ">"sv;
                scope->scope_id_ = GlobalMemory::persist(qualified_name);
            } else {
                qualified_name += "_0"sv;
                for (const Object* arg : args) {
                    std::size_t prev_size = qualified_name.size();
                    if (auto* type = arg->dyn_cast<Type>()) {
                        (*this)(qualified_name, type);
                    } else {
                        (*this)(qualified_name, arg->cast<Value>());
                    }
                    std::format_to(
                        std::inserter(
                            qualified_name,
                            std::next(
                                qualified_name.begin(), static_cast<std::ptrdiff_t>(prev_size)
                            )
                        ),
                        "{}"sv,
                        qualified_name.size() - prev_size
                    );
                }
                scope->scope_id_ = GlobalMemory::persist(qualified_name);
            }
            scope->children_.begin()->second->scope_id_ = {};
        }
    }

    auto operator()(GlobalMemory::String& out, const Scope* scope, strview identifier) const
        -> void {
        if (scope->is_extern_) {
            [&](this auto&& self, const Scope* current) -> void {
                if (current->parent_) self(current->parent_);
                if (!current->scope_id_.empty()) {
                    std::format_to(std::back_inserter(out), "{}::"sv, current->scope_id_);
                }
            }(scope);
            out += identifier;
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
            std::format_to(std::back_inserter(out), "_{}{}"sv, identifier.length(), identifier);
        }
    }

    auto operator()(GlobalMemory::String& out, const Type* type) const -> void {
        switch (type->kind_) {
        case Kind::Struct:
        case Kind::Interface:
        case Kind::Instance:
        case Kind::Dynamic:
        case Kind::Union:
            std::format_to(std::back_inserter(out), "t{}"sv, type_map_.at(type));
            break;
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
        case Kind::Reference:
            out += "r"sv;
            if (type->cast<ReferenceType>()->is_moved_) {
                out += "M"sv;
            } else if (type->cast<ReferenceType>()->is_mutable_) {
                out += "m"sv;
            }
            (*this)(out, type->cast<ReferenceType>()->target_type_);
            break;
        case Kind::Pointer:
            out += "p"sv;
            if (!type->cast<PointerType>()->is_mutable_) {
                out += "m"sv;
            }
            (*this)(out, type->cast<PointerType>()->target_type_);
            break;
        default:
            UNREACHABLE();
        }
    }

    auto operator()(GlobalMemory::String& out, const Value* value) const -> void {
        switch (value->kind_) {
        case Kind::Nullptr:
            out += "Vn"sv;
            break;
        case Kind::Integer: {
            const IntegerValue* int_value = value->cast<IntegerValue>();
            if (int_value->type_->is_signed_) {
                std::format_to(std::back_inserter(out), "Vi{}"sv, int_value->signed_value_);
            } else {
                std::format_to(std::back_inserter(out), "Vu{}"sv, int_value->unsigned_value_);
            }
            break;
        }
        case Kind::Float: {
            const FloatValue* float_value = value->cast<FloatValue>();
            std::format_to(std::back_inserter(out), "Vf{:a}"sv, float_value->value_);
            break;
        }
        case Kind::Boolean:
            out += value->cast<BooleanValue>()->value_ ? "Vtrue"sv : "Vfalse"sv;
            break;
        default:
            UNREACHABLE();
        }
    }

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
    GlobalMemory::String constants_;
    GlobalMemory::String forward_declarations_;
    GlobalMemory::String definitions_;
    CodeGenEnvironment& env_;
    NameMangler& mangler_;
    TypeMap& type_map_;
    const Scope* current_scope_;
    std::size_t indent_level_;

public:
    CodeGen(CodeGenEnvironment& env, NameMangler& mangler, TypeMap& type_map) noexcept
        : env_(env), mangler_(mangler), type_map_(type_map) {}

    auto operator()(std::ofstream& stream) -> void {
        for (const auto& [scope, identifier, value] : env_.constants_) {
            constants_ += "constexpr "sv;
            ObjectGen::output(constants_, value->get_type(), type_map_);
            constants_ += " "sv;
            mangler_(constants_, scope, identifier);
            constants_ += " = "sv;
            ObjectGen::output(constants_, value, type_map_);
            constants_ += ";\n"sv;
        }
        for (const auto& [scope, node, func_obj] : env_.functions_) {
            current_scope_ = scope;
            if (auto* func_def = std::get_if<const ASTFunctionDefinition*>(&node)) {
                (*this)(*func_def, func_obj);
            } else if (auto* ctor_def = std::get_if<const ASTCtorDtorDefinition*>(&node)) {
                if ((*ctor_def)->is_constructor) {
                    (*this)(*ctor_def, func_obj);
                } else {
                    (*this)(*ctor_def, func_obj, 0);
                }
            } else if (auto* op_def = std::get_if<const ASTOperatorDefinition*>(&node)) {
                (*this)(*op_def, func_obj);
            } else {
                UNREACHABLE();
            }
            newline(false);
        }
        for (const auto& [dynamic, identifier, func_type] : env_.virtuals_) {
            (*this)(dynamic, identifier, func_type);
        }
        stream << "\n// ----- Constants -----\n"sv;
        flush_without_sdl_prefix(constants_, stream);
        stream << "\n// ----- Function Forward Declarations -----\n"sv;
        flush_without_sdl_prefix(forward_declarations_, stream);
        stream << "\n// ----- Function Definitions -----\n"sv;
        flush_without_sdl_prefix(definitions_, stream);
    }

    auto operator()(ASTNodeVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(ASTExprVariant variant) -> void { return std::visit(*this, variant); }

    auto operator()(std::monostate) -> void { UNREACHABLE(); }

    auto operator()(const ASTClass auto*) -> void {}

    auto operator()(const ASTFunctionDefinition* node, const FunctionType* func_type) -> void {
        bool is_main = node->identifier == "main"sv && current_scope_->parent_->parent_ == nullptr;
        auto gen = [&](GlobalMemory::String& out) {
            if (is_main) {
                out += "auto main(int $argc, char** $argv) -> int"sv;
            } else {
                out += "auto "sv;
                mangler_(out, current_scope_, node->identifier);
                out += "_0"sv;
                for (const Type* param_type : func_type->parameters_) {
                    mangler_(out, param_type);
                }
                out += "("sv;
                strview sep = ""sv;
                for (std::size_t i = 0; i < node->parameters.size(); i++) {
                    out += sep;
                    const ASTFunctionParameter& param = node->parameters[i];
                    ObjectGen::output(out, func_type->parameters_[i], type_map_);
                    if (!param.is_mutable && func_type->parameters_[i]->kind_ != Kind::Reference) {
                        out += " const"sv;
                    }
                    out += " "sv;
                    out += param.identifier;
                    sep = ", "sv;
                }
                out += ") -> "sv;
                ObjectGen::output(out, func_type->return_type_, type_map_);
            }
        };
        gen(forward_declarations_);
        forward_declarations_ += ";\n"sv;
        gen(definitions_);
        definitions_ += " {"sv;
        indent_level_++;
        if (is_main) {
            newline();
            definitions_ += "std::vector<std::string_view> $args_vec{$argv, $argv + $argc};";
            newline();
            definitions_ += "const std::span<std::string_view> args{$args_vec};"sv;
        }
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline();
    }

    auto operator()(const ASTCtorDtorDefinition* node, const FunctionType* func_type) -> void {
        assert(node->is_constructor);
        auto gen = [&](GlobalMemory::String& out) {
            std::format_to(
                std::back_inserter(out), "auto _init_t{}_0"sv, type_map_.at(func_type->return_type_)
            );
            for (const Type* param_type : func_type->parameters_) {
                mangler_(out, param_type);
            }
            out += "("sv;
            strview sep = ""sv;
            for (std::size_t i = 0; i < node->parameters.size(); i++) {
                out += sep;
                const ASTFunctionParameter& param = node->parameters[i];
                ObjectGen::output(out, func_type->parameters_[i], type_map_);
                if (!param.is_mutable && func_type->parameters_[i]->kind_ != Kind::Reference) {
                    out += " const"sv;
                }
                out += " "sv;
                out += param.identifier;
                sep = ", "sv;
            }
            out += ") -> "sv;
            ObjectGen::output(out, func_type->return_type_, type_map_);
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
        newline();
    }

    auto operator()(const ASTCtorDtorDefinition* node, const FunctionType* func_type, int) -> void {
        assert(!node->is_constructor);
        std::format_to(
            std::back_inserter(definitions_),
            "_t{}::~_t{}"sv,
            type_map_.at(func_type->return_type_),
            type_map_.at(func_type->return_type_)
        );
        definitions_ += "() {"sv;
        indent_level_++;
        newline();
        std::format_to(
            std::back_inserter(definitions_),
            "_t{}& self = *this;"sv,
            type_map_.at(func_type->return_type_)
        );
        for (const ASTNodeVariant& child : node->body) {
            newline();
            (*this)(child);
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline();
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
            ObjectGen::output(out, func_type->parameters_[0], type_map_);
            if (!node->left.is_mutable && func_type->parameters_[0]->kind_ != Kind::Reference) {
                out += " const"sv;
            }
            out += " "sv;
            out += node->left.identifier;
            if (node->right) {
                out += ", "sv;
                ObjectGen::output(out, func_type->parameters_[1], type_map_);
                if (!node->right->is_mutable &&
                    func_type->parameters_[1]->kind_ != Kind::Reference) {
                    out += " const"sv;
                }
                out += " "sv;
                out += node->right->identifier;
            }
            out += ") -> "sv;
            ObjectGen::output(out, func_type->return_type_, type_map_);
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
        newline();
    }

    auto operator()(const DynamicType* dynamic, strview identifier, const FunctionType* func_type)
        -> void {
        const InterfaceType* interface = dynamic->target_type_;
        auto gen = [&](GlobalMemory::String& out) {
            out += "auto "sv;
            mangler_(out, interface->scope_, identifier);
            out += "_0"sv;
            for (const Type* param_type : func_type->parameters_) {
                mangler_(out, param_type);
            }
            out += "("sv;
            ObjectGen::output(out, dynamic, type_map_);
            out += " _arg0"sv;
            for (std::size_t i = 1; i < func_type->parameters_.size(); i++) {
                out += ", "sv;
                ObjectGen::output(out, func_type->parameters_[i], type_map_);
                out += " "sv;
                std::format_to(std::back_inserter(out), "_arg{}"sv, i);
            }
            out += ") -> "sv;
            ObjectGen::output(out, func_type->return_type_, type_map_);
        };
        gen(forward_declarations_);
        forward_declarations_ += ";\n"sv;
        gen(definitions_);

        const Type* self = func_type->parameters_[0];
        ValueCategory self_category = Type::category(self);
        auto gen_self = [&](const InstanceType* implementor) {
            switch (self_category) {
            case ValueCategory::Right:
            case ValueCategory::Expiring:
                std::format_to(
                    std::back_inserter(definitions_),
                    "std::move(*static_cast<_t{}*>(_arg0._data))"sv,
                    type_map_.at(implementor)
                );
                break;
            case ValueCategory::Left:
                std::format_to(
                    std::back_inserter(definitions_),
                    "*static_cast<_t{}*>(_arg0._data)"sv,
                    type_map_.at(implementor)
                );
            }
        };

        definitions_ += " {"sv;
        indent_level_++;
        newline();
        definitions_ += "switch (_arg0._type_index) {"sv;
        for (const InstanceType* implementor : interface->implementors_) {
            newline();
            std::format_to(
                std::back_inserter(definitions_),
                "case {}:"sv,
                type_map_.at(implementor),
                implementor->repr()
            );
            indent_level_++;
            newline();
            definitions_ += "return "sv;
            mangler_(definitions_, implementor->scope_, identifier);
            definitions_ += "_0"sv;
            mangler_(definitions_, Type::forward_like(self, implementor));
            for (std::size_t i = 1; i < func_type->parameters_.size(); i++) {
                mangler_(definitions_, func_type->parameters_[i]);
            }
            definitions_ += "("sv;
            gen_self(implementor);
            for (std::size_t i = 1; i < func_type->parameters_.size(); i++) {
                definitions_ += ", "sv;
                std::format_to(std::back_inserter(definitions_), "_arg{}"sv, i);
            }
            definitions_ += ");"sv;
            indent_level_--;
        }
        newline();
        definitions_ += "default:"sv;
        indent_level_++;
        newline();
        definitions_ += "__builtin_unreachable();"sv;
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        indent_level_--;
        newline();
        definitions_ += "}"sv;
        newline();
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
        ObjectGen::output(definitions_, node->value, type_map_);
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
                mangler_(definitions_, member_scope, node->member);
            }
        } else {
            (*this)(node->base);
            definitions_ += "."sv;
            definitions_ += node->member;
        }
    }

    auto operator()(const ASTPointerAccess* node) -> void {
        output_pointer_chain(*std::get<PointerChain*>(*env_.find(current_scope_, node)));
        definitions_ += "->"sv;
        definitions_ += node->member;
    }

    auto operator()(const ASTAddressOfExpr* node) -> void {
        definitions_ += "&"sv;
        (*this)(node->operand);
    }

    auto operator()(const ASTDereference* node) -> void {
        if (auto* replacement = env_.find(current_scope_, node)) {
            const FunctionType* func_type =
                std::get<CodeGenEnvironment::FunctionCall*>(*replacement)->func_type;
            definitions_ += "_op_"sv;
            mangler_(definitions_, OperatorCode::Deref);
            definitions_ += "_0"sv;
            mangler_(definitions_, func_type->parameters_[0]);
            definitions_ += "("sv;
            (*this)(node->operand);
            definitions_ += ")"sv;
        } else {
            definitions_ += "*"sv;
            (*this)(node->operand);
        }
    }

    auto operator()(const ASTIndexAccess* node) -> void {
        if (auto* replacement = env_.find(current_scope_, node)) {
            const FunctionType* func_type =
                std::get<CodeGenEnvironment::FunctionCall*>(*replacement)->func_type;
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
        } else {
            (*this)(node->base);
            definitions_ += "["sv;
            (*this)(node->index);
            definitions_ += "]"sv;
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
            const FunctionType* func_type =
                std::get<CodeGenEnvironment::FunctionCall*>(*replacement)->func_type;
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
        } else {
            (*this)(node->left);
            definitions_ += " "sv;
            definitions_ += GetOperatorString(node->opcode);
            definitions_ += " "sv;
            (*this)(node->right);
        }
    }

    auto operator()(const ASTStructInitialization* node) -> void {
        auto* variant = env_.find(current_scope_, node);
        const Type* type = std::get<const Type*>(*variant);
        ObjectGen::output(definitions_, type, type_map_);
        GlobalMemory::Vector<strview> order;
        if (auto* struct_type = type->dyn_cast<StructType>()) {
            for (const auto& [field_name, _] : struct_type->fields_) {
                order.push_back(field_name);
            }
        } else if (auto* instance_type = type->dyn_cast<InstanceType>()) {
            for (const auto& [attr_name, _] : instance_type->attrs_) {
                order.push_back(attr_name);
            }
        } else {
            UNREACHABLE();
        }
        definitions_ += "{"sv;
        indent_level_++;
        for (strview field : order) {
            auto it =
                std::ranges::find(node->field_inits, field, &ASTFieldInitialization::identifier);
            if (it != node->field_inits.end()) {
                newline();
                std::format_to(std::back_inserter(definitions_), ".{} = "sv, field);
                (*this)(it->value);
                definitions_ += ","sv;
            }
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
        PointerChain* self_arg = nullptr;
        if (auto* replacement = env_.find(current_scope_, node)) {
            const auto& [func_type, scope, identifier, self] =
                *std::get<CodeGenEnvironment::FunctionCall*>(*replacement);
            assert(scope && !scope->is_extern_);
            self_arg = self;
            if (identifier == constructor_symbol) {
                definitions_ += "_init_"sv;
                mangler_(definitions_, func_type->return_type_);
            } else if (identifier == GetOperatorString(OperatorCode::Call)) {
                definitions_ += "_op_call"sv;
            } else {
                mangler_(definitions_, scope, identifier);
            }
            definitions_ += "_0"sv;
            for (const Type* param_type : func_type->parameters_) {
                mangler_(definitions_, param_type);
            }
        } else {
            (*this)(node->function);
        }
        definitions_ += "("sv;
        strview sep = "";
        if (self_arg) {
            if (!self_arg->pointers.empty()) {
                definitions_ += "*"sv;
            }
            output_pointer_chain(*self_arg);
            sep = ", "sv;
        }
        for (ASTExprVariant arg : node->arguments) {
            definitions_ += sep;
            (*this)(arg);
            sep = ", "sv;
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
        ObjectGen::output(definitions_, std::get<const Type*>(*replacement), type_map_);
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
            ObjectGen::output(definitions_, lambda_type->parameters_[i], type_map_);
            definitions_ += " "sv;
            definitions_ += param.identifier;
            sep = ", "sv;
        }
        definitions_ += ")"sv;
        if (lambda_type->return_type_->kind_ != Kind::Void) {
            definitions_ += " -> "sv;
            ObjectGen::output(definitions_, lambda_type->return_type_, type_map_);
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
        ObjectGen::output(definitions_, type, type_map_);
        if (current_scope_->self_id_.empty() && !node->is_mutable &&
            type->kind_ != Kind::Reference) {
            definitions_ += " const"sv;
        }
        definitions_ += " "sv;
        definitions_ += node->identifier;
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
        definitions_ += "switch ("sv;
        (*this)(node->condition);
        definitions_ += ") {"sv;
        for (const ASTSwitchCase& switch_case : node->cases) {
            newline();
            if (holds_monostate(switch_case.value)) {
                definitions_ += "default: "sv;
            } else {
                definitions_ += "case "sv;
                (*this)(switch_case.value);
                definitions_ += ": "sv;
            }
            (*this)(switch_case.body);
        }
        newline();
        definitions_ += "}"sv;
    }

    auto operator()(const ASTMatchStatement* node) -> void {
        const Type* value_type = std::get<const Type*>(*env_.find(current_scope_, node));
        strview qualifier;
        switch (Type::category(value_type)) {
        case ValueCategory::Right:
            qualifier = ""sv;
            break;
        case ValueCategory::Left:
            if (Type::is_mutable(value_type)) {
                qualifier = "&"sv;
            } else {
                qualifier = " const&"sv;
            }
            break;
        case ValueCategory::Expiring:
            qualifier = "&&"sv;
            break;
        }
        value_type = Type::decay(value_type);
        definitions_ += "{  // match statement"sv;
        indent_level_++;
        newline();
        ObjectGen::output(definitions_, value_type, type_map_);
        definitions_ += qualifier;
        definitions_ += " $match_value = "sv;
        (*this)(node->value);
        definitions_ += ";"sv;
        if (value_type->kind_ == Kind::Union) {
            for (const ASTMatchCase& match_case : node->cases) {
                newline();
                if (!holds_monostate(match_case.type)) {
                    const Type* case_type =
                        std::get<const Type*>(*env_.find(current_scope_, &match_case));
                    definitions_ += "if (std::holds_alternative<"sv;
                    ObjectGen::output(definitions_, case_type, type_map_);
                    definitions_ += ">($match_value)) {"sv;
                    indent_level_++;
                    if (!match_case.identifier.empty()) {
                        newline();
                        ObjectGen::output(definitions_, case_type, type_map_);
                        definitions_ += qualifier;
                        definitions_ += " "sv;
                        definitions_ += match_case.identifier;
                        definitions_ += " = std::get<"sv;
                        ObjectGen::output(definitions_, case_type, type_map_);
                        definitions_ += ">($match_value);"sv;
                    }
                } else {
                    definitions_ += "else {"sv;
                    indent_level_++;
                }
                Guard guard{*this, &match_case};
                for (const ASTNodeVariant& child : match_case.body) {
                    newline();
                    (*this)(child);
                }
                indent_level_--;
                newline();
                definitions_ += "}"sv;
            }
        }
        indent_level_--;
        newline();
        definitions_ += "}"sv;
    }

    auto operator()(const ASTForStatement* node) -> void {
        Guard guard{*this, node};
        definitions_ += "for ("sv;
        if (auto* decl = std::get_if<const ASTDeclaration*>(&node->initializer)) {
            (*this)(*decl);
            definitions_ += " "sv;
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
            if (auto* type = arg->dyn_cast<Type>()) {
                ObjectGen::output(definitions_, type, type_map_);
            } else {
                ObjectGen::output(definitions_, arg->cast<Value>(), type_map_);
            }
            sep = ", "sv;
        }
        definitions_ += ">"sv;
    }

    auto operator()(const ASTBreakStatement* node) -> void { definitions_ += "break;"sv; }

    auto operator()(const ASTContinueStatement* node) -> void { definitions_ += "continue;"sv; }

    auto operator()(const ASTMoveExpr* node) -> void {
        definitions_ += "std::move("sv;
        (*this)(node->inner);
        definitions_ += ")"sv;
    }

    auto operator()(const ASTThrowStatement* node) -> void {
        definitions_ += "throw"sv;
        if (!holds_monostate(node->expr)) {
            definitions_ += " "sv;
            (*this)(node->expr);
        }
        definitions_ += ";"sv;
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

    auto output_pointer_chain(const PointerChain& pointer_chain) -> void {
        const auto& [base, pointers] = pointer_chain;
        GlobalMemory::String temp;
        (*this)(base);
        for (const auto& [inst_type, func_type] : pointers) {
            if (inst_type->scope_->is_extern_) {
                temp += ".operator->()"sv;
            } else {
                GlobalMemory::String op_name = "_op_";
                mangler_(op_name, OperatorCode::Pointer);
                op_name += "_0"sv;
                mangler_(op_name, func_type->parameters_[0]);
                op_name += "("sv;
                temp.insert(0, op_name);
                temp += ")"sv;
            }
        }
        definitions_ += temp;
    }
};

auto codegen(SourceManager& sources, Sema& sema, CodeGenEnvironment& codegen_env) -> int {
    std::filesystem::path out_path = sources.files[1].relative_path_;
    out_path.concat(".cpp");
    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << "\n";
        return EXIT_FAILURE;
    }
    out << output_header;
    for (strview cpp_block : codegen_env.cpp_blocks_) {
        out << cpp_block << "\n";
    }

    TypeMap type_map;
    ObjectGen type_codegen{type_map};
    type_codegen.sort_types();
    NameMangler mangler(type_map);
    mangler.mangle_all_instantiations(codegen_env);
    type_codegen.output_type_defs(out);
    CodeGen{codegen_env, mangler, type_map}(out);
    return EXIT_SUCCESS;
}
