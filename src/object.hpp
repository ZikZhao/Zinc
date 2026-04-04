#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"

enum class Kind : std::uint8_t {
    None,
    Auto,
    Skolem,
    Void,
    Nullptr,
    Integer,
    Float,
    Boolean,
    Function,
    Struct,
    Interface,
    Instance,
    Reference,
    Pointer,
    Dynamic,
    Union,
};

enum class ValueCategory : std::uint8_t {
    Right,
    Left,
    Expiring,
};

class Scope;

class Object;

class Type;
class VoidType;
class NullptrType;
class IntegerType;
class FloatType;
class BooleanType;
class FunctionType;
class StructType;
class InterfaceType;
class InstanceType;
class ReferenceType;
class PointerType;
class DynamicType;
class UnionType;

class Value;
class NullptrValue;
class IntegerValue;
class FloatValue;
class BooleanValue;

class AutoObject;
class SkolemObject;

template <typename T>
concept TypeClass = std::derived_from<T, Type> && !std::is_abstract_v<T>;
template <typename V>
concept ValueClass = std::derived_from<V, Value> && !std::is_abstract_v<V>;

using AutoBindings = GlobalMemory::FlatMap<const AutoObject*, const Object*>;

class TypeDependencyGraph {
public:
    struct Edge {
        const Type* parent;
        const Type* child;
        const Type** action;
    };

private:
    GlobalMemory::Vector<Edge> edges_;

public:
    /// Checks if the child type is complete. If not, adds a dependency edge and returns false.
    auto check_dependency(const Type* parent, const Type*& child) noexcept -> bool {
        if (!is_type_complete(child)) {
            edges_.push_back({parent, child, &child});
            return false;
        }
        return true;
    }

    auto extract_cycles(const Type* target) noexcept -> GlobalMemory::Vector<Edge> {
        std::span<Edge> span = edges_;
        GlobalMemory::FlatSet<const Type*> visited{target};
        auto span_pivot = [&](this auto&& self,
                              const Type* child,
                              std::span<Edge> subspan) -> decltype(subspan)::iterator {
            auto pivot = std::partition(subspan.begin(), subspan.end(), [&](const auto& edge) {
                return edge.child != child;
            });
            for (const Edge& active_edge : std::ranges::subrange(pivot, subspan.end())) {
                if (visited.insert(active_edge.parent).second) {
                    pivot = self(active_edge.parent, std::span(subspan.begin(), pivot));
                }
            }
            return pivot;
        }(target, span);

        for (const Edge& edge : std::ranges::subrange(span.begin(), span_pivot)) {
            if (visited.contains(edge.parent)) {
                return {};
            }
        }

        std::ptrdiff_t remaining_size = std::distance(span.begin(), span_pivot);
        auto pivot = std::next(edges_.begin(), remaining_size);
        GlobalMemory::Vector<Edge> active_graph(pivot, edges_.end());
        edges_.erase(pivot, edges_.end());
        return active_graph;
    }

    void add_ref_dependency(const Type* parent, const Type* child) noexcept {
        assert(std::ranges::none_of(edges_, [&](const Edge& edge) {
            return edge.parent == parent && edge.child == child;
        }));
        edges_.push_back({parent, child, nullptr});
    }

    auto is_parent(const Type* parent) const noexcept -> bool {
        return std::ranges::any_of(edges_, [&](const Edge& edge) { return edge.parent == parent; });
    }

private:
    auto is_type_complete(const Type* type) const noexcept -> bool {
        for (const Edge& edge : edges_) {
            if (edge.parent == type) {
                return false;
            }
        }
        return true;
    }
};

class Object : public GlobalMemory::MonotonicAllocated {
public:
    static auto any_pattern(auto&& elements) noexcept -> bool {
        return std::ranges::contains(
            std::forward<decltype(elements)>(elements), true, &Object::is_pattern
        );
    }

public:
    Kind kind_;

private:
    bool is_type_;
    bool is_pattern_;

public:
    Object(Kind kind, bool is_type, bool is_pattern) noexcept
        : kind_(kind), is_type_(is_type), is_pattern_(is_pattern) {}
    virtual ~Object() = default;

    template <typename T>
        requires TypeClass<T> || std::same_as<T, Type>
    auto dyn_cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const T*, T*>;
        if constexpr (std::is_same_v<T, Type>) {
            return self.is_type_ ? static_cast<ResultType>(&self) : nullptr;
        } else {
            return self.is_type_ && self.kind_ == T::kind ? static_cast<ResultType>(&self)
                                                          : nullptr;
        }
    }

    template <typename V>
        requires ValueClass<V> || std::same_as<V, Value>
    auto dyn_cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const V*, V*>;
        if constexpr (std::is_same_v<V, Value>) {
            return !self.is_type_ ? static_cast<ResultType>(&self) : nullptr;
        } else {
            return !self.is_type_ && self.kind_ == V::kind ? static_cast<ResultType>(&self)
                                                           : nullptr;
        }
    }

    template <typename T>
    auto cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const T*, T*>;
        assert(self.template dyn_cast<T>() != nullptr);
        return static_cast<ResultType>(&self);
    }

    virtual GlobalMemory::String repr() const = 0;

    auto pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept -> bool {
        assert(is_type_ == target->is_type_);
        if (this == target) {
            return true;
        }
        if (kind_ != Kind::Auto && kind_ != target->kind_) {
            return false;
        }
        return do_pattern_match(target, auto_bindings);
    }

public:
    auto is_pattern() const noexcept -> bool { return is_pattern_; }

private:
    virtual auto do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept
        -> bool = 0;
};

class Type : public Object {
public:
    static auto decay(const Type* type) noexcept -> const Type*;
    static auto is_mutable(const Type* type) noexcept -> bool;
    static auto set_mutable(const Type* type, bool is_mutable) noexcept -> const Type*;
    static auto forward_like(const Type* source, const Type* target) noexcept -> const Type*;
    static auto category(const Type* type) noexcept -> ValueCategory;

protected:
    Type(Kind kind, bool is_pattern) noexcept : Object(kind, true, is_pattern) {}

public:
    virtual auto can_intern(TypeDependencyGraph& graph) noexcept -> bool = 0;

    auto compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering {
        if (this == other) {
            return std::strong_ordering::equal;
        }
        if (kind_ != other->kind_) {
            return kind_ <=> other->kind_;
        }
        if (assumed_equal.contains(std::minmax(this, other))) {
            return std::strong_ordering::equal;
        }
        assumed_equal.insert(std::minmax(this, other));
        return do_compare(other, assumed_equal);
    }

protected:
    virtual auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering = 0;
};

class PrimitiveType : public Type {
protected:
    PrimitiveType(Kind kind) noexcept : Type(kind, false) {}

    bool can_intern(TypeDependencyGraph& graph) noexcept final { return true; }

    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        return this == target;
    }
};

class VoidType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Void;
    static VoidType instance;

public:
    VoidType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "void"; }
};

class NullptrType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Nullptr;
    static NullptrType instance;

public:
    NullptrType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "nullptr"; }
};

class IntegerType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Integer;
    static IntegerType i8_instance;
    static IntegerType i16_instance;
    static IntegerType i32_instance;
    static IntegerType i64_instance;
    static IntegerType u8_instance;
    static IntegerType u16_instance;
    static IntegerType u32_instance;
    static IntegerType u64_instance;

public:
    bool is_signed_;
    std::uint8_t bits_;  // 8, 16, 32, 64 bits

public:
    IntegerType(bool is_signed, std::uint8_t bits) noexcept
        : PrimitiveType(kind), is_signed_(is_signed), bits_(bits) {
        assert(bits == 8 || bits == 16 || bits == 32 || bits == 64);
    }
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("{}{}", is_signed_ ? "i" : "u", bits_);
    }
    bool in_range(std::int64_t value) const noexcept {
        if (is_signed_) {
            switch (bits_) {
            case 8:
                return std::in_range<std::int8_t>(value);
            case 16:
                return std::in_range<std::int16_t>(value);
            case 32:
                return std::in_range<std::int32_t>(value);
            case 64:
                return true;
            default:
                UNREACHABLE();
            }
        } else {
            switch (bits_) {
            case 8:
                return std::in_range<std::uint8_t>(value);
            case 16:
                return std::in_range<std::uint16_t>(value);
            case 32:
                return std::in_range<std::uint32_t>(value);
            case 64:
                return value >= 0;
            default:
                UNREACHABLE();
            }
        }
    }
    bool in_range(std::uint64_t value) const noexcept {
        if (is_signed_) {
            switch (bits_) {
            case 8:
                return std::in_range<std::int8_t>(value);
            case 16:
                return std::in_range<std::int16_t>(value);
            case 32:
                return std::in_range<std::int32_t>(value);
            case 64:
                return std::in_range<std::int64_t>(value);
            default:
                UNREACHABLE();
            }
        } else {
            switch (bits_) {
            case 8:
                return std::in_range<std::uint8_t>(value);
            case 16:
                return std::in_range<std::uint16_t>(value);
            case 32:
                return std::in_range<std::uint32_t>(value);
            case 64:
                return true;
            default:
                UNREACHABLE();
            }
        }
    }
    bool in_range(double value) const noexcept {
        if (is_signed_) {
            switch (bits_) {
            case 8:
                return float_in_range<std::int8_t>(value);
            case 16:
                return float_in_range<std::int16_t>(value);
            case 32:
                return float_in_range<std::int32_t>(value);
            case 64:
                return float_in_range<std::int64_t>(value);
            default:
                UNREACHABLE();
            }
        } else {
            switch (bits_) {
            case 8:
                return float_in_range<std::uint8_t>(value);
            case 16:
                return float_in_range<std::uint16_t>(value);
            case 32:
                return float_in_range<std::uint32_t>(value);
            case 64:
                return float_in_range<std::uint64_t>(value);
            default:
                UNREACHABLE();
            }
        }
    }
};

class FloatType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Float;
    static FloatType f32_instance;
    static FloatType f64_instance;

public:
    std::uint8_t bits_;  // 32, 64 bits

public:
    FloatType(std::uint8_t bits) noexcept : PrimitiveType(kind), bits_(bits) {
        assert(bits == 32 || bits == 64);
    }
    GlobalMemory::String repr() const final { return GlobalMemory::format("f{}", bits_); }
    bool in_range(double value) const noexcept {
        if (bits_ == 32) {
            return std::abs(value) <= std::numeric_limits<float>::max();
        } else {
            return true;
        }
    }
};

class BooleanType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Boolean;
    static BooleanType instance;

public:
    BooleanType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "bool"; }
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    GlobalMemory::Vector<const Type*> parameters_;
    const Type* return_type_;

public:
    FunctionType() noexcept : Type(kind, false) {}

    FunctionType(GlobalMemory::Vector<const Type*> parameters, const Type* return_type) noexcept
        : Type(kind, any_pattern(parameters) || return_type->is_pattern()),
          parameters_(std::move(parameters)),
          return_type_(return_type) {}

    GlobalMemory::String repr() const final {
        GlobalMemory::String params_repr =
            parameters_ | std::views::transform([](const Type* type) { return type->repr(); }) |
            std::views::join_with(", "sv) | GlobalMemory::collect<GlobalMemory::String>();
        return GlobalMemory::format("({}) -> {}", params_repr, return_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        bool has_incomplete_child = false;
        for (const Type*& param_type : parameters_) {
            if (!graph.check_dependency(this, param_type)) {
                has_incomplete_child = true;
            }
        }
        if (!graph.check_dependency(this, return_type_)) {
            has_incomplete_child = true;
        }
        return !has_incomplete_child;
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const FunctionType* other_func = other->cast<FunctionType>();
        if (parameters_.size() != other_func->parameters_.size()) {
            return parameters_.size() <=> other_func->parameters_.size();
        }
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            auto cmp = parameters_[i]->compare(other_func->parameters_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return return_type_->compare(other_func->return_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const FunctionType* other_func = target->dyn_cast<FunctionType>();
        if (!other_func || parameters_.size() != other_func->parameters_.size()) {
            return false;
        }
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            if (!parameters_[i]->pattern_match(other_func->parameters_[i], auto_bindings)) {
                return false;
            }
        }
        return return_type_->pattern_match(other_func->return_type_, auto_bindings);
    }
};

class StructType : public Type {
public:
    static constexpr Kind kind = Kind::Struct;

public:
    GlobalMemory::Vector<std::pair<strview, const Type*>> fields_;

public:
    StructType() noexcept : Type(kind, false) {}

    StructType(GlobalMemory::Vector<std::pair<strview, const Type*>> fields) noexcept
        : Type(
              kind,
              any_pattern(fields | std::views::transform(&std::pair<strview, const Type*>::second))
          ),
          fields_(std::move(fields)) {}

    GlobalMemory::String repr() const final {
        GlobalMemory::String str;
        strview sep = "{"sv;
        for (const auto& [name, type] : fields_) {
            str += sep;
            str += GlobalMemory::format("{}: {}", name, type->repr());
            sep = ", "sv;
        }
        str += "}"sv;
        return str;
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        bool has_incomplete_child = false;
        for (auto& [_, type] : fields_) {
            if (!graph.check_dependency(this, type)) {
                has_incomplete_child = true;
            }
        }
        return !has_incomplete_child;
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const StructType* other_struct = other->cast<StructType>();
        if (fields_.size() != other_struct->fields_.size()) {
            return fields_.size() <=> other_struct->fields_.size();
        }
        auto this_it = fields_.begin();
        auto other_it = other_struct->fields_.begin();
        for (; this_it != fields_.end() && other_it != other_struct->fields_.end();
             ++this_it, ++other_it) {
            const auto& [this_name, this_type] = *this_it;
            const auto& [other_name, other_type] = *other_it;
            if (this_name != other_name) {
                return this_name <=> other_name;
            }
            auto cmp = this_type->compare(other_type, assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const StructType* other_struct = target->dyn_cast<StructType>();
        if (!other_struct) {
            return false;
        }
        if (fields_.size() != other_struct->fields_.size()) {
            return false;
        }
        auto this_it = fields_.begin();
        auto other_it = other_struct->fields_.begin();
        for (; this_it != fields_.end() && other_it != other_struct->fields_.end();
             ++this_it, ++other_it) {
            const auto& [this_name, this_type] = *this_it;
            const auto& [other_name, other_type] = *other_it;
            if (this_name != other_name) {
                return false;
            }
            if (!this_type->pattern_match(other_type, auto_bindings)) {
                return false;
            }
        }
        return true;
    }
};

class InterfaceType final : public Type {
public:
    static constexpr Kind kind = Kind::Interface;

public:
    Scope* scope_;
    strview identifier_;
    GlobalMemory::FlatSet<const InterfaceType*> extends_;
    mutable GlobalMemory::FlatSet<const InstanceType*> implementors_;

public:
    InterfaceType() noexcept : Type(kind, false) {}

    InterfaceType(
        Scope* scope, strview identifier, GlobalMemory::FlatSet<const InterfaceType*> extends
    ) noexcept
        : Type(kind, false), scope_(scope), identifier_(identifier), extends_(std::move(extends)) {}

    GlobalMemory::String repr() const override { return GlobalMemory::String{identifier_}; }

    bool can_intern(TypeDependencyGraph& graph) noexcept final { UNREACHABLE(); }

    void implemented_by(const InstanceType* instance) const noexcept {
        implementors_.insert(instance);
        for (const InterfaceType* parent : extends_) {
            parent->implemented_by(instance);
        }
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        return this == target;
    }
};

class InstanceType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    Scope* scope_;
    strview identifier_;
    GlobalMemory::FlatSet<const InterfaceType*> implements_;
    GlobalMemory::Vector<std::pair<strview, const Type*>> attrs_;
    mutable const void* primary_template_;
    mutable std::span<const Object*> template_args_;

public:
    InstanceType() noexcept : Type(kind, false) {}

    InstanceType(
        strview identifier, const void* primary_template, std::span<const Object*> template_args
    ) noexcept
        : Type(kind, true),
          identifier_(identifier),
          primary_template_(primary_template),
          template_args_(template_args) {}

    InstanceType(
        Scope* scope,
        strview identifier,
        GlobalMemory::FlatSet<const InterfaceType*> interfaces,
        GlobalMemory::Vector<std::pair<strview, const Type*>> attrs
    ) noexcept
        : Type(kind, false),
          scope_(scope),
          identifier_(identifier),
          implements_(std::move(interfaces)),
          attrs_(std::move(attrs)) {
        for (const InterfaceType* interface : implements_) {
            interface->implemented_by(this);
        }
    }

    GlobalMemory::String repr() const override {
        GlobalMemory::String str{identifier_};
        if (primary_template_) {
            std::string_view sep = "<"sv;
            for (const Object* arg : template_args_) {
                str += sep;
                str += arg->repr();
                sep = ", "sv;
            }
            str += ">";
        }
        return str;
    }

    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }

    auto implemented(const InterfaceType* interface) const noexcept -> bool {
        auto check = [&](const InterfaceType* current) -> bool {
            if (current == interface) {
                return true;
            }
            for (const InterfaceType* parent : current->extends_) {
                if (this->implemented(parent)) {
                    return true;
                }
            }
            return false;
        };
        for (const InterfaceType* impl : implements_) {
            if (check(impl)) {
                return true;
            }
        }
        return false;
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const InstanceType* other_instance = target->dyn_cast<InstanceType>();
        if (!other_instance) {
            return false;
        }
        if (this == other_instance) {
            return true;
        } else if (
            primary_template_ && primary_template_ == other_instance->primary_template_ &&
            template_args_.size() == other_instance->template_args_.size()
        ) {
            for (std::size_t i = 0; i < template_args_.size(); ++i) {
                if (!template_args_[i]->pattern_match(
                        other_instance->template_args_[i], auto_bindings
                    )) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    }
};

class ReferenceType final : public Type {
public:
    static constexpr Kind kind = Kind::Reference;

public:
    const Type* target_type_;
    bool is_mutable_;
    bool is_moved_;

public:
    ReferenceType() noexcept : Type(kind, false) {}

    ReferenceType(const Type* target_type, bool is_mutable, bool is_moved) noexcept
        : Type(kind, target_type->is_pattern()),
          target_type_(target_type),
          is_mutable_(is_mutable),
          is_moved_(is_moved) {
        assert(!is_moved_ || is_mutable_);
    }
    GlobalMemory::String repr() const final {
        return GlobalMemory::format(
            "{}&{}{}",
            is_moved_ ? "move " : "",
            (is_mutable_ && !is_moved_) ? "mut " : "",
            target_type_->repr()
        );
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ReferenceType* other_ref = other->cast<ReferenceType>();
        if (is_mutable_ != other_ref->is_mutable_) {
            return is_mutable_ <=> other_ref->is_mutable_;
        }
        if (is_moved_ != other_ref->is_moved_) {
            return is_moved_ <=> other_ref->is_moved_;
        }
        return target_type_->compare(other_ref->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const ReferenceType* other_ref = target->dyn_cast<ReferenceType>();
        return other_ref && (is_mutable_ == other_ref->is_mutable_) &&
               (is_moved_ == other_ref->is_moved_) &&
               target_type_->pattern_match(other_ref->target_type_, auto_bindings);
    }
};

class PointerType final : public Type {
public:
    static constexpr Kind kind = Kind::Pointer;

public:
    const Type* target_type_;
    bool is_mutable_;

public:
    PointerType() noexcept : Type(kind, false) {}

    PointerType(const Type* target_type, bool is_mutable) noexcept
        : Type(kind, target_type->is_pattern()),
          target_type_(target_type),
          is_mutable_(is_mutable) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("*{}{}", is_mutable_ ? "mut " : "", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const PointerType* other_ptr = other->cast<PointerType>();
        if (is_mutable_ != other_ptr->is_mutable_) {
            return is_mutable_ <=> other_ptr->is_mutable_;
        }
        return target_type_->compare(other_ptr->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const PointerType* other_ptr = target->dyn_cast<PointerType>();
        return other_ptr && (is_mutable_ == other_ptr->is_mutable_) &&
               target_type_->pattern_match(other_ptr->target_type_, auto_bindings);
    }
};

class DynamicType final : public Type {
public:
    static constexpr Kind kind = Kind::Dynamic;

public:
    const InterfaceType* target_type_;
    bool is_mutable_;

public:
    DynamicType() noexcept : Type(kind, false) {}

    DynamicType(const InterfaceType* target_type, bool is_mutable) noexcept
        : Type(kind, target_type->is_pattern()),
          target_type_(target_type),
          is_mutable_(is_mutable) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("dyn {}{}", is_mutable_ ? "mut " : "", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final { return true; }

private:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const DynamicType* other_dyn = other->cast<DynamicType>();
        return target_type_->compare(other_dyn->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const DynamicType* other_dyn = target->dyn_cast<DynamicType>();
        return other_dyn && target_type_->pattern_match(other_dyn->target_type_, auto_bindings);
    }
};

/// TODO: order types by address
class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::Union;

public:
    GlobalMemory::FlatSet<const Type*> types_;

public:
    UnionType() noexcept : Type(kind, false) {}

    UnionType(const Type* left, const Type* right) noexcept
        : Type(kind, any_pattern(std::array{left, right})) {
        if (auto* union_left = left->dyn_cast<UnionType>()) {
            types_.insert(union_left->types_.begin(), union_left->types_.end());
        } else {
            types_.insert(left);
        }
        if (auto* union_right = right->dyn_cast<UnionType>()) {
            types_.insert(union_right->types_.begin(), union_right->types_.end());
        } else {
            types_.insert(right);
        }
    }

    GlobalMemory::String repr() const final {
        GlobalMemory::String str = "(";
        std::string_view sep = ""sv;
        for (const Type* type : types_) {
            str += sep;
            str += type->repr();
            sep = " | "sv;
        }
        str += ")"sv;
        return str;
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        bool has_incomplete_child = false;
        for (const Type*& type : types_) {
            has_incomplete_child |= !graph.check_dependency(this, type);
        }
        return !has_incomplete_child;
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const UnionType* other_union = other->cast<UnionType>();
        if (types_.size() != other_union->types_.size()) {
            return types_.size() <=> other_union->types_.size();
        }
        auto this_it = types_.begin();
        auto other_it = other_union->types_.begin();
        for (; this_it != types_.end() && other_it != other_union->types_.end();
             ++this_it, ++other_it) {
            assumed_equal.insert({*this_it, *other_it});
            auto cmp = (*this_it)->compare(*other_it, assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        return false;
    }
};

class Value : public Object {
protected:
    Value(Kind kind, bool is_pattern = false) noexcept : Object(kind, false, is_pattern) {}
    virtual auto do_equal_compare(const Value* other) const noexcept -> bool = 0;

public:
    virtual const Type* get_type() const noexcept = 0;
    virtual auto hash_code() const noexcept -> std::size_t = 0;
    bool equal_to(const Value* other) const noexcept {
        if (kind_ != other->kind_) {
            return false;
        }
        return do_equal_compare(other);
    }
};

class NullptrValue final : public Value {
public:
    static constexpr Kind kind = Kind::Nullptr;
    static NullptrValue instance;

public:
    NullptrValue() noexcept : Value(kind) {}
    GlobalMemory::String repr() const final { return "nullptr"; }
    const NullptrType* get_type() const noexcept final { return &NullptrType::instance; }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const NullptrValue* other_nullptr = target->dyn_cast<NullptrValue>();
        return other_nullptr != nullptr;
    }
    bool do_equal_compare(const Value* other) const noexcept final { return true; }
    std::size_t hash_code() const noexcept final { return std::bit_cast<std::size_t>(this); }
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;

public:
    const IntegerType* type_;
    union {
        std::int64_t signed_value_;
        std::uint64_t unsigned_value_;
    };

public:
    IntegerValue(const IntegerType* type, std::int64_t signed_value) noexcept
        : Value(kind), type_(type) {
        if (type->is_signed_) {
            signed_value_ = signed_value;
        } else {
            unsigned_value_ = static_cast<std::uint64_t>(signed_value);
        }
    }
    IntegerValue(const IntegerType* type, std::uint64_t unsigned_value) noexcept
        : Value(kind), type_(type) {
        if (type->is_signed_) {
            signed_value_ = static_cast<std::int64_t>(unsigned_value);
        } else {
            unsigned_value_ = unsigned_value;
        }
    }
    auto repr() const -> GlobalMemory::String final {
        strview prefix;
        if (type_->is_signed_) {
            switch (type_->bits_) {
            case 8:
                prefix = "i8";
                break;
            case 16:
                prefix = "i16";
                break;
            case 32:
                prefix = "i32";
                break;
            case 64:
                prefix = "i64";
                break;
            default:
                UNREACHABLE();
            }
            return GlobalMemory::format("{}({})", prefix, signed_value_);
        } else {
            switch (type_->bits_) {
            case 8:
                prefix = "u8";
                break;
            case 16:
                prefix = "u16";
                break;
            case 32:
                prefix = "u32";
                break;
            case 64:
                prefix = "u64";
                break;
            default:
                UNREACHABLE();
            }
            return GlobalMemory::format("{}({})", prefix, unsigned_value_);
        }
    }
    auto get_type() const noexcept -> const IntegerType* final { return type_; }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const IntegerValue* other_int = target->dyn_cast<IntegerValue>();
        return other_int && type_ == other_int->type_ &&
               unsigned_value_ == other_int->unsigned_value_;
    }
    bool do_equal_compare(const Value* other) const noexcept final {
        const IntegerValue* other_int = other->cast<IntegerValue>();
        return type_ == other_int->type_ && unsigned_value_ == other_int->unsigned_value_;
    }
    std::size_t hash_code() const noexcept final {
        return std::hash<std::uint64_t>{}(unsigned_value_);
    }
    template <std::integral T>
    bool in_range() const noexcept {
        if (type_->is_signed_) {
            return std::in_range<std::int64_t>(unsigned_value_);
        } else {
            return std::in_range<std::uint64_t>(unsigned_value_);
        }
    }
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::Float;

public:
    const FloatType* type_;
    double value_;

public:
    explicit FloatValue(double value) noexcept
        : Value(kind), type_(&FloatType::f64_instance), value_(value) {}
    FloatValue(const FloatType* type, double value) noexcept
        : Value(kind), type_(type), value_(value) {}
    GlobalMemory::String repr() const final {
        strview prefix;
        switch (type_->bits_) {
        case 32:
            prefix = "f32";
            break;
        case 64:
            prefix = "f64";
            break;
        default:
            UNREACHABLE();
        }
        return GlobalMemory::format("{}({:#})", prefix, value_);
    }
    const FloatType* get_type() const noexcept final { return type_; }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        /// float-point number is not allowed as NTTP
        return false;
    }
    bool do_equal_compare(const Value* other) const noexcept final {
        return value_ == other->cast<FloatValue>()->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<double>{}(value_); }
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::Boolean;

public:
    bool value_;

public:
    BooleanValue(bool value) noexcept : Value(kind), value_(value) {}
    GlobalMemory::String repr() const noexcept final { return this->value_ ? "true" : "false"; }
    const BooleanType* get_type() const noexcept final { return &BooleanType::instance; }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const BooleanValue* other_bool = target->dyn_cast<BooleanValue>();
        return other_bool && value_ == other_bool->value_;
    }
    bool do_equal_compare(const Value* other) const noexcept final {
        return value_ == other->cast<BooleanValue>()->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<bool>{}(value_); }
};

class AutoObject final : public Type, public Value {
public:
    static constexpr Kind kind = Kind::Auto;

public:
    std::size_t index_;

public:
    AutoObject(std::size_t index) noexcept : Type(kind, true), Value(kind, true), index_(index) {}
    auto as_object(bool is_nttp) const noexcept -> const Object* {
        if (is_nttp) {
            return static_cast<const Value*>(this);
        } else {
            return static_cast<const Type*>(this);
        }
    }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        auto [it, inserted] = auto_bindings.insert({this, target});
        if (!inserted) {
            if (it->second != target) {
                Diagnostic::error_auto_binding_mismatch(repr(), it->second->repr(), target->repr());
                return false;
            }
        }
        return true;
    }

private:
    GlobalMemory::String repr() const final { return GlobalMemory::format("auto#{}", index_); }
    const Type* get_type() const noexcept final { UNREACHABLE(); }
    bool do_equal_compare(const Value* other) const noexcept final { UNREACHABLE(); }
    auto hash_code() const noexcept -> std::size_t final { UNREACHABLE(); }
    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }
    auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering final {
        UNREACHABLE();
    }
};

class SkolemObject final : public Type, public Value {
public:
    static constexpr Kind kind = Kind::Skolem;

public:
    SkolemObject() noexcept : Type(kind, true), Value(kind, true) {}
    auto as_object(bool is_nttp) const noexcept -> const Object* {
        if (is_nttp) {
            return static_cast<const Value*>(this);
        } else {
            return static_cast<const Type*>(this);
        }
    }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        if (target->kind_ != Kind::Skolem) {
            return false;
        }
        if (auto* target_type = target->dyn_cast<Type>()) {
            return this == static_cast<const SkolemObject*>(target_type);
        } else {
            return this == static_cast<const SkolemObject*>(target->cast<Value>());
        }
    }

private:
    GlobalMemory::String repr() const final { UNREACHABLE(); }
    const Type* get_type() const noexcept final { UNREACHABLE(); }
    bool do_equal_compare(const Value* other) const noexcept final { UNREACHABLE(); }
    auto hash_code() const noexcept -> std::size_t final { UNREACHABLE(); }
    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }
    auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering final {
        UNREACHABLE();
    }
};

class TypeResolution final {
    friend class TypeRegistry;

private:
    /// Indicates whether the type is complete
    static constexpr std::uintptr_t flag = 1;

private:
    std::uintptr_t ptr_;

public:
    TypeResolution() noexcept : ptr_(0) {};

    TypeResolution(const Type* type) noexcept : ptr_(std::bit_cast<std::uintptr_t>(type)) {}

    template <TypeClass T>
    TypeResolution(std::type_identity<T> identity) noexcept
        : ptr_(std::bit_cast<std::uintptr_t>(GlobalMemory::alloc_raw(identity))) {
        std::construct_at(std::bit_cast<T*>(ptr_));
        if constexpr (!(std::is_same_v<T, InterfaceType> || std::is_same_v<T, InstanceType>)) {
            ptr_ |= flag;
        }
    }

    operator const Type*() const noexcept { return std::bit_cast<const Type*>(ptr_ & ~flag); }

    auto get() const noexcept -> const Type* { return std::bit_cast<const Type*>(ptr_ & ~flag); }

    auto operator->() const noexcept -> const Type* { return get(); }

    auto is_sized() const noexcept -> bool { return (ptr_ & flag) == 0; }

private:
    template <TypeClass T>
    auto construct(auto&&... args) noexcept -> T* {
        assert(ptr_);
        std::destroy_at(std::bit_cast<T*>(ptr_ & ~flag));
        std::construct_at(std::bit_cast<T*>(ptr_ & ~flag), std::forward<decltype(args)>(args)...);
        ptr_ &= ~flag;
        return std::bit_cast<T*>(ptr_);
    }
};

class TypeRegistry {
    friend class ThreadGuard;
    friend class ObjectGen;

private:
    struct TypeComparator {
        bool operator()(const Type* lhs, const Type* rhs) const noexcept {
            GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
            return lhs->compare(rhs, assumed_equal) == std::strong_ordering::less;
        }
    };

    template <typename T>
    using TypeSet = GlobalMemory::FlatSet<const T*, TypeComparator>;

private:
    static thread_local std::optional<TypeRegistry> instance;

public:
    template <TypeClass T>
        requires(!TypeInTupleV<T, std::tuple<NullptrType, IntegerType, FloatType, BooleanType>>)
    static void get_at(TypeResolution& out, auto&&... args) noexcept {
        if constexpr (std::is_same_v<T, InterfaceType>) {
            // interfaces are not interned
            out.construct<T>(std::forward<decltype(args)>(args)...);
            instance->interface_types_.push_back(static_cast<const T*>(out.get()));
        } else if constexpr (std::is_same_v<T, InstanceType>) {
            // classes are not interned
            out.construct<T>(std::forward<decltype(args)>(args)...);
            instance->instance_types_.push_back(static_cast<const T*>(out.get()));
        } else {
            instance->get_interned<T>(out, std::forward<decltype(args)>(args)...);
        }
    }

    template <typename T>
        requires(!TypeInTupleV<T, std::tuple<NullptrType, IntegerType, FloatType, BooleanType>>)
    static auto get(auto&&... args) noexcept -> const T* {
        TypeResolution out = std::type_identity<T>();
        get_at<T>(out, std::forward<decltype(args)>(args)...);
        return static_cast<const T*>(out.get());
    }

    static void add_ref_dependency(const Type* parent, const Type* child) noexcept {
        instance->graph_.add_ref_dependency(parent, child);
    }

    static auto is_type_incomplete(const Type* type) noexcept -> bool {
        return instance->graph_.is_parent(type);
    }

    static auto get_auto_instances(std::size_t count) noexcept -> std::span<const AutoObject*> {
        while (instance->auto_objects_.size() < count) {
            instance->auto_objects_.push_back(new AutoObject(instance->auto_objects_.size()));
        }
        return {instance->auto_objects_.data(), count};
    }

    static auto get_skolem_objects(std::size_t count) noexcept -> std::span<const SkolemObject*> {
        while (instance->skolem_objects_.size() < count) {
            instance->skolem_objects_.push_back(new SkolemObject());
        }
        return {instance->skolem_objects_.data(), count};
    }

private:
    TypeDependencyGraph graph_;
    std::tuple<
        TypeSet<FunctionType>,
        TypeSet<StructType>,
        TypeSet<ReferenceType>,
        TypeSet<PointerType>,
        TypeSet<DynamicType>,
        TypeSet<UnionType>>
        types_;
    GlobalMemory::Vector<const InterfaceType*> interface_types_;
    GlobalMemory::Vector<const InstanceType*> instance_types_;
    GlobalMemory::Vector<const AutoObject*> auto_objects_;
    GlobalMemory::Vector<const SkolemObject*> skolem_objects_;

private:
    template <TypeClass T>
    void get_interned(TypeResolution& out, auto&&... args) noexcept {
        T* type = out.construct<T>(std::forward<decltype(args)>(args)...);
        if (type->is_pattern()) {
            return;
        }
        if (type->can_intern(graph_)) {
            auto [it, _] = std::get<TypeSet<T>>(types_).insert(type);
            out = *it;
        } else {
            auto active_edges = graph_.extract_cycles(type);
            if (!active_edges.empty()) {
                out = simplify_recursive_type(active_edges, type);
            }
        }
    }

    auto dispatch_pool(const Type* type) noexcept -> std::pair<const Type*, bool> {
        switch (type->kind_) {
        case Kind::Function: {
            auto [it, inserted] =
                std::get<TypeSet<FunctionType>>(types_).insert(type->cast<FunctionType>());
            return {*it, inserted};
        }
        case Kind::Struct: {
            auto [it, inserted] =
                std::get<TypeSet<StructType>>(types_).insert(type->cast<StructType>());
            return {*it, inserted};
        }
        case Kind::Interface:
            // std::get<TypeSet<InterfaceType>>(types_).insert(type->cast<InterfaceType>());
            return {nullptr, false};
        case Kind::Instance:
            // std::get<TypeSet<ClassType>>(types_).insert(type->cast<ClassType>());
            return {nullptr, false};
        case Kind::Reference: {
            auto [it, inserted] =
                std::get<TypeSet<ReferenceType>>(types_).insert(type->cast<ReferenceType>());
            return {*it, inserted};
        }
        case Kind::Pointer: {
            auto [it, inserted] =
                std::get<TypeSet<PointerType>>(types_).insert(type->cast<PointerType>());
            return {*it, inserted};
        }
        case Kind::Union: {
            auto [it, inserted] =
                std::get<TypeSet<UnionType>>(types_).insert(type->cast<UnionType>());
            return {*it, inserted};
        }
        default:
            UNREACHABLE();
        }
    }

    auto simplify_recursive_type(
        GlobalMemory::Vector<TypeDependencyGraph::Edge> active_edges, const Type* type
    ) noexcept -> const Type* {
        GlobalMemory::FlatSet<const Type*, TypeComparator> unique_types;
        GlobalMemory::FlatSet<const Type*> visited_types{type};
        std::ignore =
            [&](this auto&& self,
                const Type* child,
                std::span<TypeDependencyGraph::Edge> subspan) -> decltype(subspan)::iterator {
            auto pivot = std::partition(subspan.begin(), subspan.end(), [&](const auto& edge) {
                return edge.child != child;
            });
            auto [it, inserted] = unique_types.insert(child);
            for (const auto& edge : std::ranges::subrange(pivot, subspan.end())) {
                if (!inserted) {
                    *edge.action = *it;
                }
                if (visited_types.contains(edge.parent)) {
                    continue;
                }
                visited_types.insert(edge.parent);
                pivot = self(edge.parent, std::span(subspan.begin(), pivot));
            }
            return pivot;
        }(type, active_edges);

        // Intern the type and its unique descendants
        auto [interned_type, inserted] = dispatch_pool(type);
        if (inserted) {
            for (const Type* candidate : unique_types) {
                if (candidate == type) continue;
                auto [_, subtype_inserted] = dispatch_pool(candidate);
                assert(subtype_inserted);
            }
        }
        return interned_type;
    }

public:
    TypeRegistry() noexcept = default;
};

class Term : public GlobalMemory::MonotonicAllocated {
public:
    static auto of(const Type* type) noexcept -> Term { return Term(type); }
    static auto of(const Value* value) noexcept -> Term { return Term(value); }
    static auto lvalue(const Type* type, bool is_mutable) noexcept -> Term {
        if (auto* ref = type->dyn_cast<ReferenceType>()) {
            if (ref->is_moved_) {
                return Term(TypeRegistry::get<ReferenceType>(ref->target_type_, true, false));
            } else {
                return Term(
                    TypeRegistry::get<ReferenceType>(ref->target_type_, ref->is_mutable_, false)
                );
            }
        } else {
            return Term(TypeRegistry::get<ReferenceType>(type, is_mutable, false));
        }
    }
    static auto xvalue(const Type* type) noexcept -> Term {
        if (auto* ref = type->dyn_cast<ReferenceType>()) {
            return Term(TypeRegistry::get<ReferenceType>(ref->target_type_, true, true));
        } else {
            return Term(TypeRegistry::get<ReferenceType>(Type::decay(type), true, true));
        }
    }
    static auto forward_like(Term term, const Type* type) noexcept -> Term {
        const Type* decayed = term.decay();
        switch (Type::category(term.effective_type())) {
        case ValueCategory::Right:
            return of(decayed);
        case ValueCategory::Left:
            return lvalue(decayed, Type::is_mutable(term.effective_type()));
        case ValueCategory::Expiring:
            return xvalue(decayed);
        }
    }

private:
    const Object* ptr_;

private:
    Term(const Type* type) noexcept : ptr_(type) {}
    Term(Value* value) noexcept : ptr_(value) {}
    Term(const Object* ptr) noexcept : ptr_(ptr) {}

public:
    Term() noexcept = default;

    auto get() const noexcept -> const Object* { return ptr_; }
    operator bool() const noexcept { return ptr_ != nullptr; }
    auto operator->() const noexcept -> const Object* { return ptr_; }
    auto effective_type() const noexcept -> const Type* {
        if (ptr_ == nullptr) {
            return nullptr;
        } else if (auto* value = ptr_->dyn_cast<Value>()) {
            return value->get_type();
        } else {
            return ptr_->cast<Type>();
        }
    }
    auto decay() const noexcept -> const Type* { return Type::decay(effective_type()); }
    auto is_comptime() const noexcept -> bool { return ptr_ && ptr_->dyn_cast<Value>(); }
    auto get_comptime() const noexcept -> const Value* {
        if (ptr_ == nullptr) {
            return nullptr;
        }
        return ptr_->dyn_cast<Value>();
    }
};

// ===== Implementation =====

inline auto Type::decay(const Type* type) noexcept -> const Type* {
    if (type == nullptr) return nullptr;
    if (auto ref = type->dyn_cast<ReferenceType>()) {
        return decay(ref->target_type_);
    } else {
        return type;
    }
}

inline auto Type::is_mutable(const Type* type) noexcept -> bool {
    if (auto ref = type->dyn_cast<ReferenceType>()) {
        return ref->is_mutable_;
    } else {
        return false;
    }
}

inline auto Type::forward_like(const Type* source, const Type* target) noexcept -> const Type* {
    if (target->kind_ == Kind::Reference) {
        return target;
    }
    const Type* decayed_target = decay(target);
    if (auto ref = source->dyn_cast<ReferenceType>()) {
        return TypeRegistry::get<ReferenceType>(decayed_target, ref->is_mutable_, ref->is_moved_);
    } else {
        return TypeRegistry::get<ReferenceType>(decayed_target, true, true);
    }
}

inline auto Type::category(const Type* type) noexcept -> ValueCategory {
    if (type->kind_ == Kind::Reference) {
        return type->cast<ReferenceType>()->is_moved_ ? ValueCategory::Expiring
                                                      : ValueCategory::Left;
    } else {
        return ValueCategory::Right;
    }
}

inline VoidType VoidType::instance;

inline NullptrType NullptrType::instance;

inline IntegerType IntegerType::i8_instance = IntegerType(true, 8);
inline IntegerType IntegerType::i16_instance = IntegerType(true, 16);
inline IntegerType IntegerType::i32_instance = IntegerType(true, 32);
inline IntegerType IntegerType::i64_instance = IntegerType(true, 64);
inline IntegerType IntegerType::u8_instance = IntegerType(false, 8);
inline IntegerType IntegerType::u16_instance = IntegerType(false, 16);
inline IntegerType IntegerType::u32_instance = IntegerType(false, 32);
inline IntegerType IntegerType::u64_instance = IntegerType(false, 64);

inline FloatType FloatType::f32_instance = FloatType(32);
inline FloatType FloatType::f64_instance = FloatType(64);

inline BooleanType BooleanType::instance;

inline NullptrValue NullptrValue::instance;

inline thread_local std::optional<TypeRegistry> TypeRegistry::instance;
