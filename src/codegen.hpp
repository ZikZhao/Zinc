#pragma once
#include "pch.hpp"

#include "object.hpp"

static auto format_ptr(const void* ptr) -> std::string_view {
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
            definitions_ += format_ptr(type);
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
        std::format_to(std::back_inserter(forward_declarations_), "struct {};\n", format_ptr(type));
        std::format_to(std::back_inserter(definitions_), "struct {} {{\n", format_ptr(type));
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
        std::format_to(std::back_inserter(forward_declarations_), "struct {};\n", format_ptr(type));
        std::format_to(std::back_inserter(definitions_), "struct {} {{\n", format_ptr(type));
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
