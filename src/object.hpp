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
    Mutable,
    Reference,
    Pointer,
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
class MutableType;
class ReferenceType;
class PointerType;
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
        return std::ranges::any_of(
            std::forward<decltype(elements)>(elements), std::identity{}, &Object::is_pattern
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

    auto dyn_type(this auto& self)
        requires std::same_as<std::remove_cvref_t<decltype(self)>, Object>
    {
        using ResultType = std::conditional_t<
            std::is_const_v<std::remove_reference_t<decltype(self)>>,
            const Type*,
            Type*>;
        return self.is_type_ ? static_cast<ResultType>(&self) : nullptr;
    };

    auto dyn_value(this auto& self)
        requires std::same_as<std::remove_cvref_t<decltype(self)>, Object>
    {
        using ResultType = std::conditional_t<
            std::is_const_v<std::remove_reference_t<decltype(self)>>,
            const Value*,
            Value*>;
        return !self.is_type_ ? static_cast<ResultType>(&self) : nullptr;
    };

    template <TypeClass T>
    auto dyn_cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const T*, T*>;
        return static_cast<ResultType>(self.is_type_ && self.kind_ == T::kind ? &self : nullptr);
    }

    template <ValueClass V>
    auto dyn_cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const V*, V*>;
        return !self.is_type_ && self.kind_ == V::kind ? static_cast<ResultType>(&self) : nullptr;
    }

    template <typename T>
    auto cast(this auto& self) {
        using ResultType = std::
            conditional_t<std::is_const_v<std::remove_reference_t<decltype(self)>>, const T*, T*>;
        using SelfType = std::remove_cvref_t<decltype(self)>;
        if constexpr (std::is_same_v<T, Value>) {
            assert(!self.is_type_);
        } else if constexpr (std::is_same_v<T, Type>) {
            assert(self.is_type_);
        } else {
            if constexpr (std::is_same_v<SelfType, Object>) {
                assert(self.kind_ == T::kind && ((self.dyn_type() != nullptr) == TypeClass<T>));
            } else {
                static_assert(std::is_same_v<SelfType, Type> || std::is_same_v<SelfType, Value>);
                assert(
                    self.kind_ == T::kind &&
                    (std::is_same_v<SelfType, Type> || TypeClass<SelfType>) == TypeClass<T>
                );
            }
        }
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
    auto dyn_type() -> Type* = delete;
    auto dyn_value() -> Value* = delete;

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

    virtual auto default_construct() const noexcept -> bool = 0;

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
    bool default_construct() const noexcept final { UNREACHABLE(); }
};

class NullptrType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Nullptr;
    static NullptrType instance;

public:
    NullptrType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "nullptr"; }
    bool default_construct() const noexcept final { return true; }
};

class IntegerType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Integer;
    static IntegerType untyped_instance;
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
    std::uint8_t bits_;  // 8, 16, 32, 64 bits (or 0 for untyped integer)

public:
    IntegerType(bool is_signed, std::uint8_t bits) noexcept
        : PrimitiveType(kind), is_signed_(is_signed), bits_(bits) {
        assert(bits == 0 || bits == 8 || bits == 16 || bits == 32 || bits == 64);
    }
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("{}{}", is_signed_ ? "i" : "u", bits_);
    }
    bool default_construct() const noexcept final { return true; }
};

class FloatType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Float;
    static FloatType untyped_instance;
    static FloatType f32_instance;
    static FloatType f64_instance;

public:
    std::uint8_t bits_;  // 32, 64 bits (or 0 for untyped float)

public:
    FloatType(std::uint8_t bits) noexcept : PrimitiveType(kind), bits_(bits) {
        assert(bits == 0 || bits == 32 || bits == 64);
    }
    GlobalMemory::String repr() const final { return GlobalMemory::format("f{}", bits_); }
    bool default_construct() const noexcept final { return true; }
};

class BooleanType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Boolean;
    static BooleanType instance;

public:
    BooleanType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "bool"; }
    bool default_construct() const noexcept final { return true; }
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    std::span<const Type*> parameters_;
    const Type* return_type_;

public:
    FunctionType(std::span<const Type*> parameters, const Type* return_type) noexcept
        : Type(kind, any_pattern(parameters) || return_type->is_pattern()),
          parameters_(parameters),
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

    bool default_construct() const noexcept final { return false; }

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
    GlobalMemory::FlatMap<strview, const Type*> fields_;

public:
    StructType(GlobalMemory::FlatMap<strview, const Type*> fields) noexcept
        : Type(kind, any_pattern(fields | std::views::values)), fields_(std::move(fields)) {}

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
        for (auto field : fields_) {
            if (!graph.check_dependency(this, field.second)) {
                has_incomplete_child = true;
            }
        }
        return !has_incomplete_child;
    }

    bool default_construct() const noexcept final;

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const StructType* other_struct = other->cast<StructType>();
        if (fields_.size() != other_struct->fields_.size()) {
            return fields_.size() <=> other_struct->fields_.size();
        }
        auto it1 = fields_.begin();
        auto it2 = other_struct->fields_.begin();
        for (; it1 != fields_.end() && it2 != other_struct->fields_.end(); ++it1, ++it2) {
            if (it1->first != it2->first) {
                return it1->first <=> it2->first;
            }
            auto cmp = it1->second->compare(it2->second, assumed_equal);
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
        for (const auto& [name, type] : fields_) {
            auto it = other_struct->fields_.find(name);
            if (it == other_struct->fields_.end() ||
                !type->pattern_match((*it).second, auto_bindings)) {
                return false;
            }
        }
        return true;
    }
};

class InterfaceType final : public Type {
public:
    static constexpr Kind kind = Kind::Interface;

private:
    GlobalMemory::FlatMap<strview, std::span<const FunctionType*>> methods_;

public:
    InterfaceType() noexcept : Type(kind, false) {}

    GlobalMemory::String repr() const override {
        /// TODO:
        return {};
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        /// TODO:
        return true;
    }

    bool default_construct() const noexcept final { return false; }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        /// TODO:
        return std::strong_ordering::equal;
    }
};

class InstanceType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    Scope* scope_;
    strview identifier_;
    const Type* extends_;
    std::span<const Type*> implements_;
    GlobalMemory::FlatMap<strview, const Type*> attrs_;
    mutable const void* primary_template_;
    mutable std::span<const Object*> template_args_;

public:
    InstanceType(strview identifier) : Type(kind, false), identifier_(identifier) {}

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
        const Type* extends,
        std::span<const Type*> interfaces,
        GlobalMemory::FlatMap<strview, const Type*> attrs
    ) noexcept
        : Type(kind, false),
          scope_(scope),
          identifier_(identifier),
          extends_(extends),
          implements_(interfaces),
          attrs_(std::move(attrs)) {}

    GlobalMemory::String repr() const override {
        GlobalMemory::String str = GlobalMemory::format("{}", identifier_);
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

    auto default_construct() const noexcept -> bool final {
        /// TODO:
        return true;
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

class MutableType final : public Type {
public:
    static constexpr Kind kind = Kind::Mutable;

public:
    const Type* target_type_;

public:
    MutableType(const Type* target_type) noexcept
        : Type(kind, target_type->is_pattern()), target_type_(target_type) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("mut {}", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

    auto default_construct() const noexcept -> bool final { return false; }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const MutableType* other_mut = other->cast<MutableType>();
        return target_type_->compare(other_mut->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const MutableType* other_mut = target->dyn_cast<MutableType>();
        return other_mut && target_type_->pattern_match(other_mut->target_type_, auto_bindings);
    }
};

class ReferenceType final : public Type {
public:
    static constexpr Kind kind = Kind::Reference;

public:
    const Type* target_type_;
    bool is_moved_;

public:
    ReferenceType(const Type* target_type, bool is_moved) noexcept
        : Type(kind, target_type->is_pattern()), target_type_(target_type), is_moved_(is_moved) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("{}&{}", is_moved_ ? "move " : "", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

    auto default_construct() const noexcept -> bool final { return false; }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ReferenceType* other_ref = other->cast<ReferenceType>();
        if (is_moved_ != other_ref->is_moved_) {
            return is_moved_ <=> other_ref->is_moved_;
        }
        return target_type_->compare(other_ref->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const ReferenceType* other_ref = target->dyn_cast<ReferenceType>();
        return other_ref && (is_moved_ == other_ref->is_moved_) &&
               target_type_->pattern_match(other_ref->target_type_, auto_bindings);
    }
};

class PointerType final : public Type {
public:
    static constexpr Kind kind = Kind::Pointer;

public:
    const Type* target_type_;

public:
    PointerType(const Type* target_type) noexcept
        : Type(kind, target_type->is_pattern()), target_type_(target_type) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("*{}", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

    auto default_construct() const noexcept -> bool final { return false; }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const PointerType* other_ptr = other->cast<PointerType>();
        return target_type_->compare(other_ptr->target_type_, assumed_equal);
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const PointerType* other_ptr = target->dyn_cast<PointerType>();
        return other_ptr && target_type_->pattern_match(other_ptr->target_type_, auto_bindings);
    }
};

/// TODO: order types by address
class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::Union;

private:
    static std::span<const Type*> flatten(std::span<const Type*> unflattened_types) {
        std::size_t size = 0;
        for (const Type* type : unflattened_types) {
            if (const UnionType* union_type = type->dyn_cast<UnionType>()) {
                size += union_type->types_.size();
            } else {
                size++;
            }
        }
        std::span<const Type*> buffer = GlobalMemory::alloc_array<const Type*>(size);
        std::size_t index = 0;
        for (const Type* type : unflattened_types) {
            if (const UnionType* union_type = type->dyn_cast<UnionType>()) {
                for (const Type* inner_type : union_type->types_) {
                    buffer[index++] = inner_type;
                }
            } else {
                buffer[index++] = type;
            }
        }
        return buffer;
    }
    static std::span<const Type*> flatten(const Type* left, const Type* right) {
        std::array types = {left, right};
        return flatten(std::span<const Type*>(types));
    }

public:
    std::span<const Type*> types_;

public:
    UnionType(auto&&... unflattened_types) noexcept
        : Type(kind, any_pattern(std::tuple{unflattened_types...})),
          types_(flatten(unflattened_types...)) {}

    GlobalMemory::String repr() const final {
        // TODO
        return {};
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        for (const Type*& type : types_) {
            if (!graph.check_dependency(this, type)) {
                return false;
            }
        }
        return true;
    }

    auto default_construct() const noexcept -> bool final {
        /// TODO:
        assert(false);
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const UnionType* other_union = other->cast<UnionType>();
        if (types_.size() != other_union->types_.size()) {
            return types_.size() <=> other_union->types_.size();
        }
        for (std::size_t i = 0; i < types_.size(); ++i) {
            assumed_equal.insert({types_[i], other_union->types_[i]});
            auto cmp = types_[i]->compare(other_union->types_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        throw;
    }
};

class Value : public Object {
protected:
    Value(Kind kind, bool is_pattern = false) noexcept : Object(kind, false, is_pattern) {}
    virtual auto do_equal_compare(const Value* other) const noexcept -> bool = 0;

public:
    Type* dyn_type() = delete;
    Value* dyn_value() = delete;
    virtual const Type* get_type() const noexcept = 0;
    virtual Value* clone() const noexcept = 0;
    virtual void assign_from(Value* source) noexcept = 0;
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
    NullptrValue* clone() const noexcept final { return new NullptrValue(*this); }
    void assign_from(Value* source) noexcept final { UNREACHABLE(); }
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
    const IntegerType* const type_;  // nullptr for integer literals without a specific type
    BigInt value_;

public:
    explicit IntegerValue(strview value) noexcept
        : Value(kind), type_(&IntegerType::untyped_instance), value_(value) {}
    IntegerValue(const IntegerType* type, BigInt value) noexcept
        : Value(kind), type_(type), value_(std::move(value)) {}
    auto repr() const -> GlobalMemory::String final {
        strview prefix;
        if (type_ == &IntegerType::untyped_instance) {
            return value_.to_string();
        } else if (type_->is_signed_) {
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
        }
        return GlobalMemory::format("{}({})", prefix, value_.to_string());
    }
    auto get_type() const noexcept -> const IntegerType* final { return type_; }
    auto clone() const noexcept -> IntegerValue* final { return new IntegerValue(*this); }
    void assign_from(Value* source) noexcept final {
        IntegerValue* int_source = source->cast<IntegerValue>();
        this->value_ = int_source->value_;
    }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        const IntegerValue* other_int = target->dyn_cast<IntegerValue>();
        return other_int && value_ == other_int->value_;
    }
    bool do_equal_compare(const Value* other) const noexcept final {
        const IntegerValue* other_int = other->cast<IntegerValue>();
        return value_ == other_int->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<BigInt>{}(value_); }
    IntegerValue* resolve_to(const IntegerType* target) const noexcept {
        assert(type_ == &IntegerType::untyped_instance);
        assert(target ? target->dyn_cast<IntegerType>() != nullptr : true);
        if (target == nullptr) {
            // most suitable type inference
            if (value_.fits_in<std::int32_t>()) {
                return new IntegerValue(&IntegerType::i32_instance, value_);
            } else {
                Diagnostic::error_overflow(value_.to_string(), target->repr());
                return nullptr;
            }
        } else {
            assert(target != &IntegerType::untyped_instance);
            // convert to the specified target type
            strview error_type;
            if (target->is_signed_) {
                switch (target->bits_) {
                case 8:
                    error_type = value_.fits_in<std::int8_t>() ? "" : "i8";
                    break;
                case 16:
                    error_type = value_.fits_in<std::int16_t>() ? "" : "i16";
                    break;
                case 32:
                    error_type = value_.fits_in<std::int32_t>() ? "" : "i32";
                    break;
                case 64:
                    error_type = value_.fits_in<std::int64_t>() ? "" : "i64";
                    break;
                default:
                    UNREACHABLE();
                }
            } else {
                switch (target->bits_) {
                case 8:
                    error_type = value_.fits_in<std::uint8_t>() ? "" : "u8";
                    break;
                case 16:
                    error_type = value_.fits_in<std::uint16_t>() ? "" : "u16";
                    break;
                case 32:
                    error_type = value_.fits_in<std::uint32_t>() ? "" : "u32";
                    break;
                case 64:
                    error_type = value_.fits_in<std::uint64_t>() ? "" : "u64";
                    break;
                default:
                    UNREACHABLE();
                }
            }
            if (!error_type.empty()) {
                Diagnostic::error_overflow(value_.to_string(), error_type);
                return nullptr;
            }
            return new IntegerValue(target, value_);
        }
    }
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::Float;

public:
    const FloatType* const type_;
    double value_;

public:
    explicit FloatValue(double value) noexcept
        : Value(kind), type_(&FloatType::untyped_instance), value_(value) {}
    FloatValue(const FloatType* type, double value) noexcept
        : Value(kind), type_(type), value_(value) {
        assert(type && type != &FloatType::untyped_instance);
    }
    GlobalMemory::String repr() const final {
        strview prefix;
        switch (type_->bits_) {
        case 0:
            return GlobalMemory::format("{:#}", value_);
            break;
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
    FloatValue* clone() const noexcept final { return new FloatValue(*this); }
    void assign_from(Value* source) noexcept final {
        FloatValue* float_source = source->cast<FloatValue>();
        this->value_ = float_source->value_;
    }
    bool do_pattern_match(const Object* target, AutoBindings& auto_bindings) const noexcept final {
        /// float-point number is not allowed as NTTP
        return false;
    }
    bool do_equal_compare(const Value* other) const noexcept final {
        return value_ == other->cast<FloatValue>()->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<double>{}(value_); }
    FloatValue* resolve_to(const FloatType* target) const noexcept {
        assert(type_ == &FloatType::untyped_instance);
        if (target == nullptr) {
            return new FloatValue(&FloatType::f64_instance, value_);
        } else {
            assert(target != &FloatType::untyped_instance);
            if (std::abs(value_) <= std::numeric_limits<float>::max()) {
                return new FloatValue(&FloatType::f32_instance, static_cast<float>(value_));
            } else {
                Diagnostic::error_overflow(GlobalMemory::format("{:#}", value_), "f32");
                return nullptr;
            }
        }
    }
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
    BooleanValue* clone() const noexcept final { return new BooleanValue(*this); }
    void assign_from(Value* source) noexcept final {
        BooleanValue* bool_source = source->cast<BooleanValue>();
        this->value_ = bool_source->value_;
    }
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
    Value* clone() const noexcept final { UNREACHABLE(); }
    void assign_from(Value* source) noexcept final { UNREACHABLE(); }
    bool do_equal_compare(const Value* other) const noexcept final { UNREACHABLE(); }
    auto hash_code() const noexcept -> std::size_t final { UNREACHABLE(); }
    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }
    auto default_construct() const noexcept -> bool final { UNREACHABLE(); }
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
        if (auto* target_type = target->dyn_type()) {
            return this == target_type->cast<SkolemObject>();
        } else {
            return this == target->cast<Value>()->cast<SkolemObject>();
        }
    }

private:
    GlobalMemory::String repr() const final { UNREACHABLE(); }
    const Type* get_type() const noexcept final { UNREACHABLE(); }
    Value* clone() const noexcept final { UNREACHABLE(); }
    void assign_from(Value* source) noexcept final { UNREACHABLE(); }
    bool do_equal_compare(const Value* other) const noexcept final { UNREACHABLE(); }
    auto hash_code() const noexcept -> std::size_t final { UNREACHABLE(); }
    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }
    auto default_construct() const noexcept -> bool final { UNREACHABLE(); }
    auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering final {
        UNREACHABLE();
    }
};

class ClassHierarchyGraph {
private:
    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> edges_;

public:
    auto add_edge(const Type* parent, const Type* child) noexcept -> void {
        edges_.insert({parent, child});
    }

    auto is_base(const Type* from, const Type* to) const noexcept -> bool {
        GlobalMemory::FlatSet<const Type*> visited;
        std::vector<const Type*> stack{from};
        while (!stack.empty()) {
            const Type* current = stack.back();
            stack.pop_back();
            if (current == to) {
                return true;
            }
            if (visited.insert(current).second) {
                if (auto* instance_type = current->dyn_cast<InstanceType>()) {
                    stack.push_back(instance_type->extends_);
                    stack.insert(
                        stack.end(),
                        instance_type->implements_.begin(),
                        instance_type->implements_.end()
                    );
                }
            }
        }
        return false;
    }

    auto is_derived(const Type* from, const Type* to) const noexcept -> bool {
        GlobalMemory::FlatSet<const Type*> visited;
        std::vector<const Type*> stack{from};
        while (!stack.empty()) {
            const Type* current = stack.back();
            stack.pop_back();
            if (current == to) {
                return true;
            }
            if (visited.insert(current).second) {
                auto range = std::ranges::equal_range(
                    edges_, current, {}, &std::pair<const Type*, const Type*>::first
                );
                for (const auto& edge : range) {
                    stack.push_back(edge.second);
                }
            }
        }
        return false;
    }

    auto is_reachable(const Type* from, const Type* to) const noexcept -> bool {
        return is_base(from, to) || is_derived(from, to);
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
        : ptr_(std::bit_cast<std::uintptr_t>(GlobalMemory::alloc_raw(identity)) | flag) {}

    template <std::derived_from<Type> T>
    operator const T*() const noexcept {
        return static_cast<const T*>(std::bit_cast<const Type*>(ptr_ & ~flag));
    }

    auto get() const noexcept -> const Type* { return std::bit_cast<const Type*>(ptr_ & ~flag); }

    auto operator->() const noexcept -> const Type* { return get(); }

    auto is_sized() const noexcept -> bool { return (ptr_ & flag) == 0; }

private:
    template <TypeClass T>
        requires(!std::is_same_v<T, InstanceType>)
    auto construct(auto&&... args) noexcept -> T* {
        assert(ptr_ && !is_sized());
        std::construct_at(std::bit_cast<T*>(ptr_ & ~flag), std::forward<decltype(args)>(args)...);
        ptr_ &= ~flag;
        return std::bit_cast<T*>(ptr_);
    }

    template <std::same_as<InstanceType> T>
    auto reconstruct(auto&&... args) noexcept -> T* {
        assert(ptr_ && is_sized());
        std::destroy_at(std::bit_cast<T*>(ptr_ & ~flag));
        std::construct_at(std::bit_cast<T*>(ptr_ & ~flag), std::forward<decltype(args)>(args)...);
        return std::bit_cast<T*>(ptr_);
    }
};

class TypeRegistry {
    friend class ThreadGuard;
    friend class ObjectCodeGen;

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
        if constexpr (std::is_same_v<T, InstanceType>) {
            // classes with same definition are distinct types
            out.reconstruct<T>(std::forward<decltype(args)>(args)...);
            instance->instance_types_.push_back(static_cast<const T*>(out.get()));
            instance->add_class_hierarchy(static_cast<const T*>(out.get()));
        } else {
            instance->get_interned<T>(out, std::forward<decltype(args)>(args)...);
        }
    }

    template <typename T>
        requires(!TypeInTupleV<T, std::tuple<NullptrType, IntegerType, FloatType, BooleanType>>)
    static auto get(auto&&... args) noexcept -> const T* {
        TypeResolution out = std::type_identity<T>();
        get_at<T>(out, std::forward<decltype(args)>(args)...);
        return static_cast<const T*>(out);
    }

    static void add_ref_dependency(const Type* parent, const Type* child) noexcept {
        instance->graph_.add_ref_dependency(parent, child);
    }

    static auto is_type_incomplete(const Type* type) noexcept -> bool {
        return instance->graph_.is_parent(type);
    }

    static auto is_base(const Type* from, const Type* to) noexcept -> bool {
        return instance->class_graph_.is_base(from, to);
    }

    static auto is_reachable(const Type* from, const Type* to) noexcept -> bool {
        return instance->class_graph_.is_reachable(from, to);
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
    ClassHierarchyGraph class_graph_;
    std::tuple<
        TypeSet<FunctionType>,
        TypeSet<StructType>,
        TypeSet<MutableType>,
        TypeSet<ReferenceType>,
        TypeSet<PointerType>,
        TypeSet<UnionType>>
        types_;
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
        case Kind::Mutable: {
            auto [it, inserted] =
                std::get<TypeSet<MutableType>>(types_).insert(type->cast<MutableType>());
            return {*it, inserted};
        }
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

    auto add_class_hierarchy(const InstanceType* type) noexcept -> void {
        if (type->extends_) {
            class_graph_.add_edge(type, type->extends_);
        }
        for (const Type* impl : type->implements_) {
            class_graph_.add_edge(type, impl);
        }
    }

public:
    TypeRegistry() noexcept = default;
};

class Term : public GlobalMemory::MonotonicAllocated {
public:
    static auto of(const Type* type) noexcept -> Term { return Term(type); }
    static auto of(const Value* value) noexcept -> Term { return Term(value); }
    static auto lvalue(const Type* type) noexcept -> Term {
        if (auto* ref = type->dyn_cast<ReferenceType>()) {
            return Term(TypeRegistry::get<ReferenceType>(ref->target_type_, false));
        } else {
            return Term(TypeRegistry::get<ReferenceType>(type, false));
        }
    }
    static auto xvalue(const Type* type) noexcept -> Term {
        if (auto* ref = type->dyn_cast<ReferenceType>()) {
            return Term(TypeRegistry::get<ReferenceType>(ref->target_type_, true));
        } else {
            return Term(TypeRegistry::get<ReferenceType>(Type::decay(type), true));
        }
    }
    static auto forward_like(Term term, const Type* type) noexcept -> Term {
        const Type* decayed = term.decay();
        switch (Type::category(term.effective_type())) {
        case ValueCategory::Right:
            return of(decayed);
        case ValueCategory::Left:
            return lvalue(decayed);
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
        } else if (auto* value = ptr_->dyn_value()) {
            return value->get_type();
        } else {
            return ptr_->cast<Type>();
        }
    }
    auto decay() const noexcept -> const Type* { return Type::decay(effective_type()); }
    auto is_comptime() const noexcept -> bool { return ptr_ && ptr_->dyn_value(); }
    auto get_comptime() const noexcept -> const Value* {
        if (ptr_ == nullptr) {
            return nullptr;
        }
        return ptr_->dyn_value();
    }
    auto resolve_to_default() const noexcept -> Term {
        if (ptr_ == nullptr) {
            return *this;
        }
        if (auto* value = ptr_->dyn_value()) {
            if (auto* integer_value = value->dyn_cast<IntegerValue>()) {
                if (integer_value->type_ == &IntegerType::untyped_instance) {
                    return of(integer_value->resolve_to(nullptr));
                }
            } else if (auto* float_value = value->dyn_cast<FloatValue>()) {
                if (float_value->type_ == &FloatType::untyped_instance) {
                    return of(float_value->resolve_to(nullptr));
                }
            }
        }
        return *this;
    }
};

// ===== Implementation =====

inline auto Type::decay(const Type* type) noexcept -> const Type* {
    if (type == nullptr) return nullptr;
    if (auto mut = type->dyn_cast<MutableType>()) {
        assert(mut->target_type_ == Type::decay(mut->target_type_));
        return mut->target_type_;
    } else if (auto ref = type->dyn_cast<ReferenceType>()) {
        return decay(ref->target_type_);
    } else {
        return type;
    }
}

inline auto Type::is_mutable(const Type* type) noexcept -> bool {
    if (type->kind_ == Kind::Mutable) {
        return true;
    } else if (auto ref = type->dyn_cast<ReferenceType>()) {
        return is_mutable(ref->target_type_);
    } else {
        return false;
    }
}

inline auto Type::forward_like(const Type* source, const Type* target) noexcept -> const Type* {
    const Type* decayed_target = decay(target);
    if (auto ref = source->dyn_cast<ReferenceType>()) {
        if (ref->target_type_->dyn_cast<MutableType>()) {
            return TypeRegistry::get<ReferenceType>(
                TypeRegistry::get<MutableType>(decayed_target), ref->is_moved_
            );
        } else {
            return TypeRegistry::get<ReferenceType>(decayed_target, ref->is_moved_);
        }
    } else if (source->dyn_cast<MutableType>()) {
        return TypeRegistry::get<MutableType>(decayed_target);
    } else {
        return TypeRegistry::get<ReferenceType>(decayed_target, true);
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

inline IntegerType IntegerType::untyped_instance = IntegerType(false, 0);
inline IntegerType IntegerType::i8_instance = IntegerType(true, 8);
inline IntegerType IntegerType::i16_instance = IntegerType(true, 16);
inline IntegerType IntegerType::i32_instance = IntegerType(true, 32);
inline IntegerType IntegerType::i64_instance = IntegerType(true, 64);
inline IntegerType IntegerType::u8_instance = IntegerType(false, 8);
inline IntegerType IntegerType::u16_instance = IntegerType(false, 16);
inline IntegerType IntegerType::u32_instance = IntegerType(false, 32);
inline IntegerType IntegerType::u64_instance = IntegerType(false, 64);

inline FloatType FloatType::untyped_instance = FloatType(0);
inline FloatType FloatType::f32_instance = FloatType(32);
inline FloatType FloatType::f64_instance = FloatType(64);

inline BooleanType BooleanType::instance;

inline auto StructType::default_construct() const noexcept -> bool {
    for (const auto& [_, field_type] : fields_) {
        if (!field_type->default_construct()) {
            return false;
        }
    }
    return true;
}

inline NullptrValue NullptrValue::instance;

inline thread_local std::optional<TypeRegistry> TypeRegistry::instance;
