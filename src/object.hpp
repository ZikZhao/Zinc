#pragma once
#include "pch.hpp"

#include <algorithm>

#include "diagnosis.hpp"

enum class Kind : std::uint8_t {
    Unknown,
    Auto,
    Any,
    Void,
    Nullptr,
    Integer,
    Float,
    Boolean,
    Function,
    Array,
    Struct,
    Interface,
    Instance,
    Mutable,
    Reference,
    Pointer,
    Intersection,
    Union,
    Overload,
    Template,
};

enum class ValueCategory : std::uint8_t {
    Right,
    Left,
    Expiring,
};

class Scope;
/// for reinterpret_cast to ScopeValue
struct OpaqueScopeValue;

class Object;

class Type;
class UnknownType;
class AutoType;
class AnyType;
class VoidType;
class NullptrType;
class IntegerType;
class FloatType;
class BooleanType;
class FunctionType;
class ArrayType;
class StructType;
class InterfaceType;
class InstanceType;
class MutableType;
class ReferenceType;
class PointerType;
class IntersectionType;
class UnionType;

class Value;
class UnknownValue;
class NullptrValue;
class IntegerValue;
class FloatValue;
class BooleanValue;
class FunctionValue;
class ArrayValue;
class StructValue;
class InterfaceValue;
class InstanceValue;
class MutableValue;
class ReferenceValue;
class PointerValue;
class FunctionOverloadSetValue;

template <typename T>
concept TypeClass = std::derived_from<T, Type> && !std::is_abstract_v<T>;
template <typename V>
concept ValueClass = std::derived_from<V, Value> && !std::is_abstract_v<V>;

using FunctionObject = const Object*;  // either FunctionType or FunctionValue
using FunctionOverloadVector = GlobalMemory::Vector<FunctionObject>;

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

    [[nodiscard]] auto get() const noexcept -> const Type* {
        return std::bit_cast<const Type*>(ptr_ & ~flag);
    }

    auto operator->() const noexcept -> const Type* { return get(); }

    [[nodiscard]] auto is_sized() const noexcept -> bool { return (ptr_ & flag) == 0; }

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
    friend class TypeCodeGen;

private:
    struct TypeComparator {
        bool operator()(const Type* a, const Type* b) const noexcept;
    };

    template <typename T>
    using TypeSet = GlobalMemory::FlatSet<const T*, TypeComparator>;

private:
    static thread_local std::optional<TypeRegistry> instance;

public:
    template <TypeClass T>
        requires(
            !TypeInTupleV<T, std::tuple<AnyType, NullptrType, IntegerType, FloatType, BooleanType>>
        )
    static void get_at(TypeResolution& out, auto&&... args) noexcept {
        using Composites = std::tuple<
            FunctionType,
            ArrayType,
            StructType,
            InstanceType,
            MutableType,
            ReferenceType,
            PointerType,
            IntersectionType,
            UnionType>;
        if constexpr (std::is_same_v<T, InstanceType>) {
            // classes with same definition are distinct types
            out.reconstruct<T>(std::forward<decltype(args)>(args)...);
            instance->instance_types_.push_back(static_cast<const T*>(out.get()));
        } else if constexpr (TypeInTupleV<T, Composites>) {
            instance->get_interned<T>(out, std::forward<decltype(args)>(args)...);
        } else {
            // builtin singleton types, e.g. StringType
            std::type_index type_index = std::type_index(typeid(T));
            auto it = instance->builtin_types_.find(type_index);
            if (it != instance->builtin_types_.end()) {
                out = static_cast<T*>(it->second);
            } else {
                T* type = new T(std::forward<decltype(args)>(args)...);
                instance->builtin_types_.insert({type_index, type});
                out = type;
            }
        }
    }

    template <typename T>
        requires(
            !TypeInTupleV<T, std::tuple<AnyType, NullptrType, IntegerType, FloatType, BooleanType>>
        )
    static auto get(auto&&... args) noexcept -> const T* {
        TypeResolution out = std::type_identity<T>();
        get_at<T>(out, std::forward<decltype(args)>(args)...);
        return static_cast<const T*>(out);
    }

    static auto get_unknown() noexcept -> const Type*;

    static void add_ref_dependency(const Type* parent, const Type* child) noexcept {
        instance->graph_.add_ref_dependency(parent, child);
    }

    static auto is_type_incomplete(const Type* type) noexcept -> bool {
        return instance->graph_.is_parent(type);
    }

    template <std::ranges::input_range R>
    static auto get_auto_instances(R&& is_nttp_array) noexcept -> GlobalMemory::Vector<Object*>
        requires std::same_as<std::ranges::range_value_t<R>, bool>;

private:
    TypeDependencyGraph graph_;
    std::tuple<
        TypeSet<FunctionType>,
        TypeSet<ArrayType>,
        TypeSet<StructType>,
        TypeSet<MutableType>,
        TypeSet<ReferenceType>,
        TypeSet<PointerType>,
        TypeSet<IntersectionType>,
        TypeSet<UnionType>>
        types_;
    GlobalMemory::Vector<const InstanceType*> instance_types_;
    GlobalMemory::Vector<AutoType*> auto_types_;
    // GlobalMemory::Vector<const Object*> auto_instances_;
    GlobalMemory::FlatMap<std::type_index, Type*> builtin_types_;

private:
    template <TypeClass T>
    void get_interned(TypeResolution& out, auto&&... args) noexcept {
        T* type = out.construct<T>(std::forward<decltype(args)>(args)...);
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

    auto dispatch_pool(const Type* type) noexcept -> std::pair<const Type*, bool>;

    auto simplify_recursive_type(
        GlobalMemory::Vector<TypeDependencyGraph::Edge> active_edges, const Type* type
    ) noexcept -> const Type*;

public:
    TypeRegistry() noexcept = default;
};

class Term : public GlobalMemory::MonotonicAllocated {
public:
    enum class Category : std::uint8_t {
        Runtime,   // a runtime value -> const Type*
        Comptime,  // a compile-time value -> Value*
    };

public:
    static auto unknown() noexcept -> Term;
    static auto prvalue(auto* ptr) noexcept -> Term { return Term(ptr, ValueCategory::Right); }
    static auto lvalue(auto* ptr) noexcept -> Term { return Term(ptr, ValueCategory::Left); }
    static auto lvalue(Term term) noexcept -> Term {
        return Term(term.get(), ValueCategory::Left, term.is_comptime_);
    }
    static auto xvalue(auto* ptr) noexcept -> Term { return Term(ptr, ValueCategory::Expiring); }
    static auto forward_like(const Term& source, auto* ptr) noexcept -> Term {
        return Term(ptr, source.value_category(), source.is_comptime_);
    }

private:
    union {
        const Object* ptr_;
        const Type* type_;
        Value* value_;
    };
    ValueCategory value_category_;
    bool is_comptime_;

private:
    Term(const Type* type, ValueCategory value_category) noexcept
        : type_(type), value_category_(value_category), is_comptime_(false) {}
    Term(Value* value, ValueCategory value_category) noexcept
        : value_(value), value_category_(value_category), is_comptime_(true) {}
    Term(const Object* ptr, ValueCategory value_category, bool is_comptime) noexcept
        : ptr_(ptr), value_category_(value_category), is_comptime_(is_comptime) {}

public:
    Term() noexcept = default;

    [[nodiscard]] auto get() const noexcept -> const Object* { return ptr_; }
    operator bool() const noexcept { return ptr_ != nullptr; }
    auto operator->() const noexcept -> const Object* { return ptr_; }
    [[nodiscard]] auto effective_type() const noexcept -> const Type*;
    [[nodiscard]] auto value_category() const noexcept -> ValueCategory { return value_category_; }

    [[nodiscard]] auto is_unknown() const noexcept -> bool;
    [[nodiscard]] auto is_comptime() const noexcept -> bool { return is_comptime_; }
    [[nodiscard]] auto get_comptime() const noexcept -> Value* {
        return is_comptime() ? value_ : nullptr;
    }
};

struct TermWithSelf {
    Term result;
    Term self;
};

class Object : public GlobalMemory::MonotonicAllocated {
public:
    Kind kind_;

private:
    bool is_type_;

public:
    Object(Kind kind, bool is_type) noexcept : kind_(kind), is_type_(is_type) {}
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

    auto pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept -> bool {
        assert(is_type_ == target->is_type_);
        if (this == target) {
            return true;
        }
        if (kind_ != Kind::Auto && kind_ != target->kind_) {
            return false;
        }
        return do_pattern_match(target, auto_bindings);
    }

private:
    virtual auto do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept -> bool = 0;
};

class Type : public Object {
protected:
    Type(Kind kind) noexcept : Object(kind, true) {}

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

    auto assignable_from(const Type* source) const noexcept -> bool;

    virtual auto default_construct() const noexcept -> Term = 0;

protected:
    virtual auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering = 0;

    virtual auto do_assignable_from(const Type* source) const noexcept -> bool = 0;
};

class PrimitiveType : public Type {
protected:
    PrimitiveType(Kind kind) noexcept : Type(kind) {}

    bool can_intern(TypeDependencyGraph& graph) noexcept final { return true; }

    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        return this == target;
    }
};

class UnknownType final : public PrimitiveType {
    friend class Object;
    friend class TypeRegistry;

public:
    static constexpr Kind kind = Kind::Unknown;
    static UnknownType instance;

private:
    UnknownType() noexcept : PrimitiveType(kind) {}

public:
    GlobalMemory::String repr() const final { return "unknown"; }
    bool do_assignable_from(const Type* source) const noexcept final { return true; }
    Term default_construct() const noexcept final;
};

class AutoType final : public Type {
public:
    static constexpr Kind kind = Kind::Auto;

public:
    std::size_t index_;

public:
    AutoType(std::size_t index) noexcept : Type(kind), index_(index) {}
    GlobalMemory::String repr() const final { UNREACHABLE(); }
    bool can_intern(TypeDependencyGraph& graph) noexcept final { return true; }
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }
    bool do_assignable_from(const Type* source) const noexcept final { UNREACHABLE(); }
    Term default_construct() const noexcept final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        if (auto_bindings.contains(this)) {
            return auto_bindings[this] == target;
        } else {
            auto_bindings[this] = target;
            return true;
        }
    }
};

class VoidType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Void;
    static VoidType instance;

public:
    VoidType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "void"; }
    bool do_assignable_from(const Type* source) const noexcept final {
        return source->kind_ == Kind::Void;
    }
    Term default_construct() const noexcept final { UNREACHABLE(); }
};

class AnyType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Any;
    static AnyType instance;

public:
    AnyType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "any"; }
    bool do_assignable_from(const Type* source) const noexcept final { return true; }
    Term default_construct() const noexcept final;
};

class NullptrType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Nullptr;
    static NullptrType instance;

public:
    NullptrType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "nullptr"; }
    bool do_assignable_from(const Type* source) const noexcept final {
        /// No variable can have null type except null literal
        UNREACHABLE();
    }
    Term default_construct() const noexcept final;
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
    bool do_assignable_from(const Type* source) const noexcept final {
        const IntegerType* other_int = source->dyn_cast<IntegerType>();
        return other_int && (other_int->bits_ == 0 || (this->is_signed_ == other_int->is_signed_ &&
                                                       this->bits_ >= other_int->bits_));
    }
    Term default_construct() const noexcept final;
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
    bool do_assignable_from(const Type* source) const noexcept final {
        const FloatType* other_float = source->dyn_cast<FloatType>();
        return other_float && (other_float->bits_ == 0 || this->bits_ >= other_float->bits_);
    }
    Term default_construct() const noexcept final;
};

class BooleanType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Boolean;
    static BooleanType instance;

public:
    BooleanType() noexcept : PrimitiveType(kind) {}
    GlobalMemory::String repr() const final { return "bool"; }
    bool do_assignable_from(const Type* source) const noexcept final {
        return source->dyn_cast<BooleanType>() != nullptr;
    }
    Term default_construct() const noexcept final;
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    std::span<const Type*> parameters_;
    const Type* return_type_;

public:
    FunctionType(std::span<const Type*> parameters, const Type* return_type) noexcept
        : Type(kind), parameters_(parameters), return_type_(return_type) {}

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

    Term default_construct() const noexcept final;

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
        assumed_equal.insert({return_type_, other_func->return_type_});
        return return_type_->compare(other_func->return_type_, assumed_equal);
    }

    bool do_assignable_from(const Type* source) const noexcept final;

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
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

class ArrayType final : public Type {
public:
    static constexpr Kind kind = Kind::Array;

public:
    const Type* element_type_;
    const IntegerValue* size_;

public:
    ArrayType(const Type* element_type, const IntegerValue* size) noexcept
        : Type(kind), element_type_(element_type) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("{}[]", element_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return graph.check_dependency(this, element_type_);
    }

    Term default_construct() const noexcept final;

protected:
    auto do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept -> std::strong_ordering final;

    auto do_assignable_from(const Type* source) const noexcept -> bool final;

    auto do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept -> bool final;
};

class StructType : public Type {
public:
    /// Constructs a struct type from the given field initializers. The field types will be
    /// validated against the initializers. This function may also be used to initialize a instance
    /// type, in which case the field types will be validated against the instance's attribute
    /// types.
    static auto construct(
        const Type* type, GlobalMemory::FlatMap<std::string_view, Term> inits
    ) noexcept -> Term;

public:
    static constexpr Kind kind = Kind::Struct;

public:
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_;

public:
    StructType(GlobalMemory::FlatMap<std::string_view, const Type*> fields) noexcept
        : Type(kind), fields_(std::move(fields)) {}

    GlobalMemory::String repr() const final {
        // TODO
        return {};
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

    Term default_construct() const noexcept final;

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

    bool do_assignable_from(const Type* source) const noexcept final {
        // (a,b,c) is assignable to (a,b)
        // i.e., source must have at least all fields of this
        const StructType* other_struct = source->dyn_cast<StructType>();
        if (other_struct == nullptr) {
            return false;
        }
        for (const auto& [name, type] : fields_) {
            auto it = other_struct->fields_.find(name);
            if (it == other_struct->fields_.end() || !(*it).second->assignable_from(type)) {
                return false;
            }
        }
        return true;
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
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
    GlobalMemory::FlatMap<std::string_view, FunctionOverloadVector> methods_;

public:
    InterfaceType() noexcept : Type(kind) {}

    GlobalMemory::String repr() const override {
        /// TODO:
        return {};
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        /// TODO:
        return true;
    }

    Term default_construct() const noexcept final { UNREACHABLE(); }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        /// TODO:
        return std::strong_ordering::equal;
    }

    bool do_assignable_from(const Type* source) const noexcept final {
        /// TODO:
        return false;
    }
};

class InstanceType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    Scope* scope_;
    std::string_view identifier_;
    const Type* extends_;
    std::span<const Type*> implements_;
    GlobalMemory::FlatMap<std::string_view, const Type*> attrs_;
    mutable const void* primary_template_;
    mutable std::span<const Object*> template_args_;

public:
    InstanceType(std::string_view identifier) : Type(kind), identifier_(identifier) {}

    InstanceType(
        Scope* scope,
        std::string_view identifier,
        const Type* extends,
        std::span<const Type*> interfaces,
        GlobalMemory::FlatMap<std::string_view, const Type*> attrs
    ) noexcept
        : Type(kind),
          scope_(scope),
          identifier_(identifier),
          extends_(extends),
          implements_(interfaces),
          attrs_(std::move(attrs)) {}

    InstanceType(
        Scope* scope,
        std::string_view identifier,
        const void* primary_template,
        std::span<const Object*> template_args
    ) noexcept
        : Type(kind),
          scope_(scope),
          identifier_(identifier),
          primary_template_(primary_template),
          template_args_(template_args) {}

    GlobalMemory::String repr() const override { return GlobalMemory::format("{}", identifier_); }

    auto can_intern(TypeDependencyGraph& graph) noexcept -> bool final { UNREACHABLE(); }

    auto default_construct() const noexcept -> Term final {
        /// TODO:
        assert(false);
    }

    auto get_attr(std::string_view name) const -> const Type* {
        auto it = attrs_.find(name);
        if (it == attrs_.end()) {
            throw UnlocatedProblem::make<AttributeError>(
                GlobalMemory::format_view("Class {} has no attribute named {}", identifier_, name)
            );
        }
        return it->second;
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
    }

    bool do_assignable_from(const Type* other) const noexcept final { return this == other; }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
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
    MutableType(const Type* target_type) noexcept : Type(kind), target_type_(target_type) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("mut {}", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, target_type_);
    }

    auto default_construct() const noexcept -> Term final;

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const MutableType* other_mut = other->cast<MutableType>();
        return target_type_->compare(other_mut->target_type_, assumed_equal);
    }

    bool do_assignable_from(const Type* source) const noexcept final {
        const MutableType* other_mut = source->dyn_cast<MutableType>();
        return other_mut && target_type_->assignable_from(other_mut->target_type_);
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const MutableType* other_mut = target->dyn_cast<MutableType>();
        return other_mut && target_type_->pattern_match(other_mut->target_type_, auto_bindings);
    }
};

class ReferenceType final : public Type {
public:
    static constexpr Kind kind = Kind::Reference;

public:
    const Type* referenced_type_;
    bool is_moved_;

public:
    ReferenceType(const Type* referenced_type, bool is_moved) noexcept
        : Type(kind), referenced_type_(referenced_type), is_moved_(is_moved) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("{}&{}", is_moved_ ? "move " : "", referenced_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, referenced_type_);
    }

    auto default_construct() const noexcept -> Term final {
        return {};  // references cannot be default-constructed
    }

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ReferenceType* other_ref = other->cast<ReferenceType>();
        if (is_moved_ != other_ref->is_moved_) {
            return is_moved_ <=> other_ref->is_moved_;
        }
        return referenced_type_->compare(other_ref->referenced_type_, assumed_equal);
    }

    bool do_assignable_from(const Type* source) const noexcept final {
        const ReferenceType* other_ref = source->dyn_cast<ReferenceType>();
        return other_ref && (is_moved_ == other_ref->is_moved_) &&
               referenced_type_->assignable_from(other_ref->referenced_type_);
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const ReferenceType* other_ref = target->dyn_cast<ReferenceType>();
        return other_ref && (is_moved_ == other_ref->is_moved_) &&
               referenced_type_->pattern_match(other_ref->referenced_type_, auto_bindings);
    }
};

class PointerType final : public Type {
public:
    static constexpr Kind kind = Kind::Pointer;

public:
    const Type* pointed_type_;

public:
    PointerType(const Type* pointed_type) noexcept : Type(kind), pointed_type_(pointed_type) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("*{}", pointed_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_parent(this) && graph.check_dependency(this, pointed_type_);
    }

    auto default_construct() const noexcept -> Term final;

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const PointerType* other_ptr = other->cast<PointerType>();
        return pointed_type_->compare(other_ptr->pointed_type_, assumed_equal);
    }

    bool do_assignable_from(const Type* source) const noexcept final {
        const PointerType* other_ptr = source->dyn_cast<PointerType>();
        return (other_ptr && pointed_type_->assignable_from(other_ptr->pointed_type_)) ||
               source->kind_ == Kind::Nullptr;
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const PointerType* other_ptr = target->dyn_cast<PointerType>();
        return other_ptr && pointed_type_->pattern_match(other_ptr->pointed_type_, auto_bindings);
    }
};

/// TODO: order types by address
class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::Intersection;

private:
    static std::span<const Type*> flatten(std::span<const Type*> unflattened_types) {
        std::size_t size = 0;
        for (const Type* type : unflattened_types) {
            if (const IntersectionType* intersection_type = type->dyn_cast<IntersectionType>()) {
                size += intersection_type->types_.size();
            } else {
                size++;
            }
        }
        std::span<const Type*> buffer = GlobalMemory::alloc_array<const Type*>(size);
        std::size_t index = 0;
        for (const Type* type : unflattened_types) {
            if (const IntersectionType* intersection_type = type->dyn_cast<IntersectionType>()) {
                for (const Type* inner_type : intersection_type->types_) {
                    buffer[index++] = inner_type;
                }
            } else {
                buffer[index++] = type;
            }
        }
        return buffer;
    }
    static auto flatten(const Type* left, const Type* right) -> std::span<const Type*> {
        std::array types = {left, right};
        return flatten(std::span<const Type*>(types));
    }

public:
    std::span<const Type*> types_;

public:
    IntersectionType(auto&&... unflattened_types) noexcept
        : Type(kind), types_{flatten(unflattened_types...)} {}

    GlobalMemory::String repr() const final {
        /// TODO:
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

    auto default_construct() const noexcept -> Term final;

protected:
    std::strong_ordering do_compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const IntersectionType* other_intersection = other->cast<IntersectionType>();
        if (types_.size() != other_intersection->types_.size()) {
            return types_.size() <=> other_intersection->types_.size();
        }
        for (std::size_t i = 0; i < types_.size(); ++i) {
            assumed_equal.insert({types_[i], other_intersection->types_[i]});
            auto cmp = types_[i]->compare(other_intersection->types_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    bool do_assignable_from(const Type* source) const noexcept final {
        // (a & b & c) is assignable to (a & b)
        // i.e., source supports at least all the function overloads of this
        if (const IntersectionType* other_intersection = source->dyn_cast<IntersectionType>()) {
            for (const auto& type : types_) {
                bool found = false;
                for (const auto& other_type : other_intersection->types_) {
                    if (type->assignable_from(other_type)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        } else {
            return false;
        }
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        throw;
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
        : Type(kind), types_(flatten(unflattened_types...)) {}

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

    auto default_construct() const noexcept -> Term final;

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

    bool do_assignable_from(const Type* source) const noexcept final {
        // (a | b) is assignable to (a | b | c)
        // i.e., source must be assignable to at least one of the types in this
        if (const UnionType* other_union = source->dyn_cast<UnionType>()) {
            for (const auto& type : other_union->types_) {
                bool found = false;
                for (const auto& other_type : types_) {
                    if (other_type->assignable_from(type)) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    return false;
                }
            }
            return true;
        }
        return false;
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        throw;
    }
};

class Value : public Object {
public:
    template <ValueClass V>
    static auto from_literal(std::string_view literal) -> Value* {
        if constexpr (std::is_same_v<V, NullptrValue>) {
            assert(literal == "nullptr");
            return new V();
        } else if constexpr (std::is_same_v<V, IntegerValue>) {
            int64_t value = 0;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec == std::errc()) {
                return new V(literal);
            } else if (ec == std::errc::result_out_of_range && literal[0] != '-') {
                uint64_t uvalue = 0;
                auto [uptr, uec] =
                    std::from_chars(literal.data(), literal.data() + literal.size(), uvalue);
                if (uec == std::errc()) {
                    return new V(literal);
                }
            }
            return nullptr;
        } else if constexpr (std::is_same_v<V, FloatValue>) {
            double value = 0.0;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec == std::errc()) {
                return new V(value);
            }
            return nullptr;
        } else if constexpr (std::is_same_v<V, ArrayValue>) {
            // String literal is treated as array of characters
            assert(literal.size() >= 2 && literal.front() == '"' && literal.back() == '"');
            std::string_view content = literal.substr(1, literal.size() - 2);
            return new V(content);
        } else if constexpr (std::is_same_v<V, BooleanValue>) {
            assert(literal == "true" || literal == "false");
            if (literal == "true") {
                return new V(true);
            } else {
                return new V(false);
            }
        } else {
            static_assert(false, "Unknown literal type");
        }
    };

protected:
    Value(Kind kind) noexcept : Object(kind, false) {}

public:
    Type* dyn_type() = delete;
    Value* dyn_value() = delete;
    virtual const Type* get_type() const noexcept = 0;
    virtual Value* clone() const noexcept = 0;
    virtual auto resolve_to(const Type* target) const -> Value* = 0;
    virtual void assign_from(Value* source) = 0;
    virtual auto hash_code() const noexcept -> std::size_t = 0;
};

class UnknownValue final : public Value {
    friend class Object;
    friend class Term;

public:
    static constexpr Kind kind = Kind::Unknown;
    static UnknownValue instance;

private:
    UnknownValue() noexcept : Value(kind) {}
    GlobalMemory::String repr() const final { return "unknown"; }
    UnknownType* get_type() const noexcept final { return &UnknownType::instance; }
    UnknownValue* clone() const noexcept final { return new UnknownValue(*this); }
    UnknownValue* resolve_to(const Type* target) const noexcept final { return new UnknownValue(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        UNREACHABLE();
    }
    std::size_t hash_code() const noexcept final { return std::bit_cast<std::size_t>(this); }
};

class AutoValue final : public Value {
public:
    static constexpr Kind kind = Kind::Auto;

public:
    std::size_t index_;

public:
    AutoValue(std::size_t index) noexcept : Value(kind), index_(index) {}
    GlobalMemory::String repr() const final { UNREACHABLE(); }
    AutoType* get_type() const noexcept final { UNREACHABLE(); }
    AutoValue* clone() const noexcept final { UNREACHABLE(); }
    AutoValue* resolve_to(const Type* target) const noexcept final { UNREACHABLE(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        if (auto_bindings.contains(this)) {
            return target == auto_bindings[this];
        } else {
            auto_bindings[this] = target;
            return true;
        }
    }
    std::size_t hash_code() const noexcept final {
        return hash_combine(std::bit_cast<std::size_t>(this), index_);
    }
};

class NullptrValue final : public Value {
public:
    static constexpr Kind kind = Kind::Nullptr;
    static NullptrValue instance;

public:
    NullptrValue() noexcept : Value(kind) {}
    GlobalMemory::String repr() const final { return "null"; }
    const NullptrType* get_type() const noexcept final { return &NullptrType::instance; }
    NullptrValue* clone() const noexcept final { return new NullptrValue(*this); }
    NullptrValue* resolve_to(const Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Nullptr) {
            throw UnlocatedProblem::make<TypeMismatchError>("null", target->repr());
        }
        return new NullptrValue();
    }
    void assign_from(Value* source) final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const NullptrValue* other_nullptr = target->dyn_cast<NullptrValue>();
        return other_nullptr != nullptr;
    }
    std::size_t hash_code() const noexcept final { return std::bit_cast<std::size_t>(this); }
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;

public:
    const IntegerType* const type_;  // nullptr for integer literals without a specific type
    BigInt value_;

public:
    explicit IntegerValue(std::string_view value) noexcept
        : Value(kind), type_(&IntegerType::untyped_instance), value_(value) {}
    IntegerValue(const IntegerType* type, BigInt value) noexcept
        : Value(kind), type_(type), value_(std::move(value)) {
        assert(type && type != &IntegerType::untyped_instance);
    }
    auto repr() const -> GlobalMemory::String final {
        return GlobalMemory::format("{}", value_.to_string());
    }
    auto get_type() const noexcept -> const IntegerType* final { return type_; }
    auto clone() const noexcept -> IntegerValue* final { return new IntegerValue(*this); }
    auto resolve_to(const Type* target) const -> IntegerValue* final {
        if (target && !target->dyn_cast<IntegerType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("integer", target->repr());
        }
        if (target == nullptr) {
            // most suitable type inference
            if (type_ != &IntegerType::untyped_instance) {
                return new IntegerValue(*this);
            } else if (value_.fits_in<std::int32_t>()) {
                return new IntegerValue(&IntegerType::i32_instance, value_);
            } else if (value_.fits_in<std::int64_t>()) {
                return new IntegerValue(&IntegerType::i64_instance, value_);
            } else {
                throw UnlocatedProblem::make<OverflowError>(
                    value_.to_string(), "cannot fit into i64"
                );
            }
        } else if (type_ != &IntegerType::untyped_instance) {
            // implicit convert to the specified target type (must be wider type)
            const IntegerType* int_target = target->cast<IntegerType>();
            if (type_->is_signed_ != int_target->is_signed_) {
                throw UnlocatedProblem::make<TypeMismatchError>(target->repr(), type_->repr());
            }
            if (type_->bits_ > int_target->bits_) {
                throw UnlocatedProblem::make<OverflowError>(
                    value_.to_string(),
                    GlobalMemory::format_view("cannot fit into {}", target->repr())
                );
            }
            return new IntegerValue(int_target, value_);
        } else {
            // convert to the specified target type
            const IntegerType* int_target = target->cast<IntegerType>();
            std::string_view error_type;
            if (int_target->is_signed_) {
                switch (int_target->bits_) {
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
                switch (int_target->bits_) {
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
                throw UnlocatedProblem::make<OverflowError>(value_.to_string(), error_type);
            }
            return new IntegerValue(int_target, value_);
        }
    }
    void assign_from(Value* source) final {
        IntegerValue* int_source = source->cast<IntegerValue>();
        this->value_ = int_source->value_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const IntegerValue* other_int = target->dyn_cast<IntegerValue>();
        return other_int && value_ == other_int->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<BigInt>{}(value_); }
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
    GlobalMemory::String repr() const final { return GlobalMemory::format("{}", value_); }
    const FloatType* get_type() const noexcept final { return type_; }
    FloatValue* clone() const noexcept final { return new FloatValue(*this); }
    auto resolve_to(const Type* target) const -> FloatValue* final {
        if (target && !target->dyn_cast<FloatType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("float", target->repr());
        }
        if (target == nullptr) {
            if (type_ != &FloatType::untyped_instance) {
                return new FloatValue(*this);
            }
            // default to double
            return new FloatValue(&FloatType::f64_instance, value_);
        } else if (type_) {
            const FloatType* float_target = target->cast<FloatType>();
            if (type_ == float_target) {
                return new FloatValue(*this);
            } else if (type_) {
                throw UnlocatedProblem::make<TypeMismatchError>(type_->repr(), target->repr());
            }
            return new FloatValue(float_target, value_);
        } else {
            const FloatType* float_target = target->cast<FloatType>();
            return new FloatValue(float_target, value_);
        }
    }
    void assign_from(Value* source) final {
        FloatValue* float_source = source->cast<FloatValue>();
        this->value_ = float_source->value_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        /// float-point number is not allowed as NTTP
        return false;
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
    BooleanValue* clone() const noexcept final { return new BooleanValue(*this); }
    BooleanValue* resolve_to(const Type* target) const final {
        if (target && target->kind_ != Kind::Boolean) {
            throw UnlocatedProblem::make<TypeMismatchError>("boolean", target->repr());
        }
        return new BooleanValue(*this);
    }
    void assign_from(Value* source) final {
        BooleanValue* bool_source = source->cast<BooleanValue>();
        this->value_ = bool_source->value_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const BooleanValue* other_bool = target->dyn_cast<BooleanValue>();
        return other_bool && value_ == other_bool->value_;
    }
    std::size_t hash_code() const noexcept final { return std::hash<bool>{}(value_); }
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::Function;

public:
    const FunctionType* type_;
    std::function<Term(std::span<Term>)> callback_;

public:
    FunctionValue(const FunctionType* type, decltype(callback_) invoke) noexcept
        : Value(kind), type_(type), callback_(std::move(invoke)) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("<function at {:p}>", static_cast<const void*>(this));
    }
    const FunctionType* get_type() const noexcept final { return type_; }
    FunctionValue* clone() const noexcept final { UNREACHABLE(); }
    FunctionValue* resolve_to(const Type* target) const noexcept final { UNREACHABLE(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        throw;
    }

    auto hash_code() const noexcept -> std::size_t final { throw; }

    auto invoke(std::span<Term> args) const -> Term { return callback_(args); }
};

class ArrayValue final : public Value {
public:
    static constexpr Kind kind = Kind::Array;

public:
    const ArrayType* type_;
    union {
        GlobalMemory::Vector<Value*> elements_;
        std::string_view string_;
    };

public:
    ArrayValue(GlobalMemory::Vector<Value*> elements) noexcept
        : Value(kind), type_(nullptr), elements_(std::move(elements)) {}
    ArrayValue(const ArrayType* type, GlobalMemory::Vector<Value*> elements) noexcept
        : Value(kind), type_(type), elements_(std::move(elements)) {}
    ArrayValue(std::string_view string) noexcept
        : Value(kind),
          type_(TypeRegistry::get<ArrayType>(&IntegerType::u8_instance, nullptr)),
          string_(string) {}
    ~ArrayValue() noexcept {
        if (type_->element_type_ == &IntegerType::u8_instance) {
            // string literal, no need to delete elements
        } else {
            std::destroy_at(&elements_);
        }
    };
    GlobalMemory::String repr() const noexcept final {
        return GlobalMemory::format("[{}]", type_->element_type_->repr());
    }
    const ArrayType* get_type() const noexcept final { return type_; }
    ArrayValue* clone() const noexcept final {
        if (type_) {
            GlobalMemory::Vector<Value*> cloned_elements;
            for (Value* element : elements_) {
                cloned_elements.push_back(element->clone());
            }
            return new ArrayValue(type_, std::move(cloned_elements));
        } else {
            return new ArrayValue(string_);
        }
    }
    ArrayValue* resolve_to(const Type* target) const noexcept final {
        /// TODO: implement
        return nullptr;
    }
    void assign_from(Value* source) final {
        /// TODO: implement
        UNREACHABLE();
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        throw;
    }

    auto hash_code() const noexcept -> std::size_t final { throw; }
};

class StructValue final : public Value {
public:
    static constexpr Kind kind = Kind::Struct;

public:
    const StructType* type_;
    GlobalMemory::FlatMap<std::string_view, Value*> fields_;

public:
    StructValue(const StructType* type, decltype(fields_) fields) noexcept
        : Value(kind), type_(type), fields_(std::move(fields)) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("<struct {}>", type_->repr());
    }
    const StructType* get_type() const noexcept final { return type_; }
    StructValue* clone() const noexcept final {
        GlobalMemory::FlatMap<std::string_view, Value*> cloned_fields;
        for (const auto& [name, value] : fields_) {
            cloned_fields.insert({name, value->clone()});
        }
        return new StructValue(type_, std::move(cloned_fields));
    }
    StructValue* resolve_to(const Type* target) const final {
        if (target && target != type_) {
            throw UnlocatedProblem::make<TypeMismatchError>("struct", target->repr());
        }
        return new StructValue(*this);
    }
    void assign_from(Value* source) final {
        StructValue* struct_source = source->cast<StructValue>();
        this->fields_ = struct_source->fields_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const StructValue* other_struct = target->dyn_cast<StructValue>();
        if (!other_struct || type_ != other_struct->type_) {
            return false;
        }
        for (const auto& [name, value] : fields_) {
            if (!value->pattern_match(other_struct->fields_.at(name), auto_bindings)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto hash_code() const noexcept -> std::size_t final {
        auto hash = std::bit_cast<std::size_t>(type_);
        for (const auto& [_, value] : fields_) {
            hash = hash_combine(hash, value->hash_code());
        }
        return hash;
    }
};

class InstanceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    const InstanceType* type_;
    GlobalMemory::FlatMap<std::string_view, Value*> attrs_;

public:
    InstanceValue(const InstanceType* type, decltype(attrs_) attributes) noexcept
        : Value(kind), type_(type), attrs_(std::move(attributes)) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("<instance of {}>", type_->repr());
    }
    const InstanceType* get_type() const noexcept final { return type_; }
    InstanceValue* clone() const noexcept final {
        GlobalMemory::FlatMap<std::string_view, Value*> cloned_attributes;
        for (const auto& [name, value] : attrs_) {
            cloned_attributes.insert({name, value->clone()});
        }
        return new InstanceValue(type_, std::move(cloned_attributes));
    }
    InstanceValue* resolve_to(const Type* target) const final {
        if (target && target != type_) {
            throw UnlocatedProblem::make<TypeMismatchError>("instance", target->repr());
        }
        return new InstanceValue(*this);
    }
    void assign_from(Value* source) final {
        InstanceValue* instance_source = source->cast<InstanceValue>();
        this->attrs_ = instance_source->attrs_;
    }
    auto do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept -> bool final {
        const InstanceValue* other_instance = target->dyn_cast<InstanceValue>();
        if (!other_instance || type_ != other_instance->type_) {
            return false;
        }
        for (const auto& [name, value] : attrs_) {
            if (!value->pattern_match(other_instance->attrs_.at(name), auto_bindings)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] auto hash_code() const noexcept -> std::size_t final {
        auto hash = std::bit_cast<std::size_t>(type_);
        for (const auto& [_, value] : attrs_) {
            hash = hash_combine(hash, value->hash_code());
        }
        return hash;
    }

    auto get_attr(std::string_view attr) noexcept -> Value* { return attrs_.at(attr); }
};

class MutableValue final : public Value {
public:
    static constexpr Kind kind = Kind::Mutable;

public:
    const MutableType* type_;
    Value* value_;

public:
    MutableValue(const MutableType* type, Value* value) noexcept
        : Value(kind), type_(type), value_(value) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("mut {}", value_->repr());
    }
    const MutableType* get_type() const noexcept final { return type_; }
    MutableValue* clone() const noexcept final { return new MutableValue(*this); }
    MutableValue* resolve_to(const Type* target) const final {
        if (target && !target->dyn_cast<MutableType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("mutable", target->repr());
        }
        return new MutableValue(*this);
    }
    void assign_from(Value* source) final {
        MutableValue* mut_source = source->cast<MutableValue>();
        this->value_ = mut_source->value_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        UNREACHABLE();
    }

    [[nodiscard]] auto hash_code() const noexcept -> std::size_t final {
        return hash_combine(std::bit_cast<std::size_t>(type_), value_->hash_code());
    }
};

class ReferenceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Reference;

public:
    const ReferenceType* type_;
    Value* referenced_value_;

public:
    ReferenceValue(const ReferenceType* type, Value* referenced_value) noexcept
        : Value(kind), type_(type), referenced_value_(referenced_value) {}
    GlobalMemory::String repr() const final {
        return GlobalMemory::format("&{}", referenced_value_->repr());
    }
    const ReferenceType* get_type() const noexcept final { return type_; }
    ReferenceValue* clone() const noexcept final { return new ReferenceValue(*this); }
    ReferenceValue* resolve_to(const Type* target) const final {
        if (target && !target->dyn_cast<ReferenceType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("reference", target->repr());
        }
        return new ReferenceValue(*this);
    }
    void assign_from(Value* source) final {
        ReferenceValue* ref_source = source->cast<ReferenceValue>();
        referenced_value_ = ref_source->referenced_value_;
    }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const ReferenceValue* other_ref = target->dyn_cast<ReferenceValue>();
        return other_ref && referenced_value_ == other_ref->referenced_value_;
    }

    [[nodiscard]] auto hash_code() const noexcept -> std::size_t final {
        return hash_combine(std::bit_cast<std::size_t>(type_), referenced_value_->hash_code());
    }
};

class PointerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Pointer;

public:
    const PointerType* type_;
    Value* pointed_value_;

public:
    PointerValue(const PointerType* type, Value* pointed_value) noexcept
        : Value(kind), type_(type), pointed_value_(pointed_value) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("*{}", pointed_value_->repr());
    }

    const PointerType* get_type() const noexcept final { return type_; }

    PointerValue* clone() const noexcept final { return new PointerValue(*this); }

    PointerValue* resolve_to(const Type* target) const final {
        if (target && !target->dyn_cast<PointerType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("pointer", target->repr());
        }
        return new PointerValue(*this);
    }

    void assign_from(Value* source) final {
        PointerValue* ptr_source = source->cast<PointerValue>();
        this->pointed_value_ = ptr_source->pointed_value_;
    }

    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        const PointerValue* other_ptr = target->dyn_cast<PointerValue>();
        return other_ptr && pointed_value_ == other_ptr->pointed_value_;
    }

    [[nodiscard]] auto hash_code() const noexcept -> std::size_t final {
        return hash_combine(std::bit_cast<std::size_t>(type_), pointed_value_->hash_code());
    }
};

class FunctionOverloadSetValue final : public Value {
public:
    static constexpr Kind kind = Kind::Overload;

public:
    Scope* scope_;                         // scope defining the overload set
    const OpaqueScopeValue* scope_value_;  // points to scope record defining the overload set

public:
    FunctionOverloadSetValue(Scope* scope, const OpaqueScopeValue* scope_value) noexcept
        : Value(kind), scope_(scope), scope_value_(scope_value) {}
    GlobalMemory::String repr() const final { return "<function overload set>"; }
    const Type* get_type() const noexcept final {
        /// TODO:
        return nullptr;
    }
    FunctionOverloadSetValue* clone() const noexcept final {
        return new FunctionOverloadSetValue(scope_, scope_value_);
    }
    FunctionOverloadSetValue* resolve_to(const Type* target) const final {
        /// TODO:
        return nullptr;
    }
    void assign_from(Value* source) final { UNREACHABLE(); }
    bool do_pattern_match(
        const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
    ) const noexcept final {
        UNREACHABLE();
    }

    auto hash_code() const noexcept -> std::size_t final { UNREACHABLE(); }
};

inline bool TypeRegistry::TypeComparator::operator()(
    const Type* lhs, const Type* rhs
) const noexcept {
    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
    return lhs->compare(rhs, assumed_equal) == std::strong_ordering::less;
}

inline thread_local std::optional<TypeRegistry> TypeRegistry::instance;

inline const Type* TypeRegistry::get_unknown() noexcept { return &UnknownType::instance; }

template <std::ranges::input_range R>
inline auto TypeRegistry::get_auto_instances(R&& is_nttp_array) noexcept
    -> GlobalMemory::Vector<Object*>
    requires std::same_as<std::ranges::range_value_t<R>, bool>
{
    GlobalMemory::Vector<Object*> result;
    result.reserve(is_nttp_array.size());
    std::size_t type_index = 0;
    for (bool is_nttp : std::forward<R>(is_nttp_array)) {
        if (is_nttp) {
            throw;
        } else {
            if (instance->auto_types_.size() == type_index) {
                instance->auto_types_.push_back(new AutoType(type_index));
            }
            result.push_back(instance->auto_types_[type_index++]);
        }
    }
    return result;
}

inline std::pair<const Type*, bool> TypeRegistry::dispatch_pool(const Type* type) noexcept {
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
    case Kind::Intersection: {
        auto [it, inserted] =
            std::get<TypeSet<IntersectionType>>(types_).insert(type->cast<IntersectionType>());
        return {*it, inserted};
    }
    case Kind::Union: {
        auto [it, inserted] = std::get<TypeSet<UnionType>>(types_).insert(type->cast<UnionType>());
        return {*it, inserted};
    }
    default:
        UNREACHABLE();
    }
}

inline const Type* TypeRegistry::simplify_recursive_type(
    GlobalMemory::Vector<TypeDependencyGraph::Edge> active_edges, const Type* type
) noexcept {
    GlobalMemory::FlatSet<const Type*, TypeComparator> unique_types;
    GlobalMemory::FlatSet<const Type*> visited_types{type};
    std::ignore = [&](this auto&& self,
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

inline auto Term::unknown() noexcept -> Term { return Term::prvalue(&UnknownType::instance); }

inline auto Term::is_unknown() const noexcept -> bool { return ptr_->kind_ == Kind::Unknown; }

inline auto Term::effective_type() const noexcept -> const Type* {
    return is_comptime_ ? value_->get_type() : type_;
}

inline auto Type::assignable_from(const Type* source) const noexcept -> bool {
    assert(!(this == source) || do_assignable_from(source));
    if (this == source) {
        return true;
    }
    if (kind_ != source->kind_) {
        if (auto mut = source->dyn_cast<MutableType>()) {
            return do_assignable_from(mut->target_type_);
        } else if (auto ref = source->dyn_cast<ReferenceType>()) {
            return do_assignable_from(ref->referenced_type_);
        }
    }
    return do_assignable_from(source);
}

inline UnknownType UnknownType::instance;

inline auto UnknownType::default_construct() const noexcept -> Term {
    return Term::prvalue(&UnknownValue::instance);
}

inline VoidType VoidType::instance;

inline AnyType AnyType::instance;

inline auto AnyType::default_construct() const noexcept -> Term {
    return Term::prvalue(&UnknownValue::instance);
}

inline NullptrType NullptrType::instance;

inline auto NullptrType::default_construct() const noexcept -> Term {
    return Term::prvalue(&NullptrValue::instance);
}

inline IntegerType IntegerType::untyped_instance = IntegerType(false, 0);
inline IntegerType IntegerType::i8_instance = IntegerType(true, 8);
inline IntegerType IntegerType::i16_instance = IntegerType(true, 16);
inline IntegerType IntegerType::i32_instance = IntegerType(true, 32);
inline IntegerType IntegerType::i64_instance = IntegerType(true, 64);
inline IntegerType IntegerType::u8_instance = IntegerType(false, 8);
inline IntegerType IntegerType::u16_instance = IntegerType(false, 16);
inline IntegerType IntegerType::u32_instance = IntegerType(false, 32);
inline IntegerType IntegerType::u64_instance = IntegerType(false, 64);

inline auto IntegerType::default_construct() const noexcept -> Term {
    return Term::prvalue(new IntegerValue(this, BigInt{}));
}

inline FloatType FloatType::untyped_instance = FloatType(0);
inline FloatType FloatType::f32_instance = FloatType(32);
inline FloatType FloatType::f64_instance = FloatType(64);

inline auto FloatType::default_construct() const noexcept -> Term {
    return Term::prvalue(new FloatValue(this, 0.0));
}

inline BooleanType BooleanType::instance;

inline auto BooleanType::default_construct() const noexcept -> Term {
    return Term::prvalue(new BooleanValue(false));
}

inline auto FunctionType::do_assignable_from(const Type* source) const noexcept -> bool {
    // (Base) => Derived is assignable to (Derived) => Base
    // i.e., parameters are contravariant, return type is covariant
    if (const FunctionType* func_other = source->dyn_cast<FunctionType>()) {
        if (parameters_.size() != func_other->parameters_.size()) {
            return false;
        }
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            if (!func_other->parameters_[i]->assignable_from(parameters_[i])) {
                return false;
            }
        }
        return return_type_->assignable_from(func_other->return_type_);
    } else if (const IntersectionType* intersection_other = source->dyn_cast<IntersectionType>()) {
        for (const Type* member_type : intersection_other->types_) {
            if (this->assignable_from(member_type)) {
                return true;
            }
        }
        return false;
    } else {
        return false;
    }
}

inline auto FunctionType::default_construct() const noexcept -> Term {
    /// TODO:
    assert(false);
}

inline auto ArrayType::do_compare(
    const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
) const noexcept -> std::strong_ordering {
    const ArrayType* other_array = other->cast<ArrayType>();
    auto cmp = element_type_->compare(other_array->element_type_, assumed_equal);
    if (cmp != std::strong_ordering::equal) {
        return cmp;
    }
    if (size_ == nullptr && other_array->size_ == nullptr) {
        return std::strong_ordering::equal;
    } else if (size_ == nullptr) {
        return std::strong_ordering::less;
    } else if (other_array->size_ == nullptr) {
        return std::strong_ordering::greater;
    }
    return size_->value_ <=> other_array->size_->value_;
}

inline auto ArrayType::do_assignable_from(const Type* source) const noexcept -> bool {
    if (const ArrayType* array_other = source->dyn_cast<ArrayType>()) {
        if (!element_type_->assignable_from(array_other->element_type_)) {
            return false;
        }
        if (size_ && array_other->size_) {
            return size_->value_ == array_other->size_->value_;
        } else {
            return (!size_ && !array_other->size_);
        }
    }
    return false;
}

inline auto ArrayType::do_pattern_match(
    const Object* target, GlobalMemory::FlatMap<const Object*, const Object*>& auto_bindings
) const noexcept -> bool {
    const ArrayType* other_array = target->dyn_cast<ArrayType>();
    return other_array && element_type_->pattern_match(other_array->element_type_, auto_bindings) &&
           (size_ ? size_->pattern_match(other_array->size_, auto_bindings) : true);
}

inline auto ArrayType::default_construct() const noexcept -> Term {
    GlobalMemory::Vector<Term> elements;
    bool is_comptime = true;
    for (std::size_t i = 0; i < size_->value_; ++i) {
        Term default_value = element_type_->default_construct();
        if (!default_value) {
            return {};
        }
        if (!default_value.is_comptime()) {
            is_comptime = false;
        }
        elements.push_back(default_value);
    }
    if (is_comptime) {
        return Term::prvalue(
            new ArrayValue(this, elements | std::views::transform([](Term& term) {
                                     return term.get_comptime();
                                 }) | GlobalMemory::collect<GlobalMemory::Vector>())
        );
    } else {
        return Term::prvalue(this);
    }
}

inline auto StructType::construct(
    const Type* type, GlobalMemory::FlatMap<std::string_view, Term> inits
) noexcept -> Term {
    const GlobalMemory::FlatMap<std::string_view, const Type*>* field_types = nullptr;
    if (auto struct_type = type->dyn_cast<StructType>()) {
        field_types = &struct_type->fields_;
    } else if (auto instance_type = type->dyn_cast<InstanceType>()) {
        field_types = &instance_type->attrs_;
    } else {
        throw;
    }
    GlobalMemory::FlatMap<std::string_view, const Type*> init_types;
    bool is_comptime = true;
    for (const auto& [id, term] : inits) {
        if (!term.is_comptime()) {
            is_comptime = false;
        }
        init_types.insert({id, term.effective_type()});
    }
    for (const auto& [field_name, field_type] : *field_types) {
        auto it = init_types.find(field_name);
        if (it == init_types.end()) {
            if (auto default_value = field_type->default_construct()) {
                inits.insert({field_name, default_value});
            } else {
                throw UnlocatedProblem::make<UninitializedAttributeError>(field_name);
                return Term::unknown();
            }
        } else if (!field_type->assignable_from(it->second)) {
            throw UnlocatedProblem::make<TypeMismatchError>(
                GlobalMemory::format_view("field '{}'", field_name), it->second->repr()
            );
        } else {
            init_types.erase(it);
        }
    }
    if (!init_types.empty()) {
        // throw UnlocatedProblem::make<UnrecognizedAttributeError>(init_types.begin()->first);
        throw;
    }
    if (is_comptime) {
        auto values = inits |
                      std::views::transform(
                          [](
                              const std::pair<std::string_view, Term>& pair
                          ) -> std::pair<std::string_view, Value*> {
                              return {pair.first, pair.second.get_comptime()};
                          }
                      ) |
                      GlobalMemory::collect<GlobalMemory::FlatMap<std::string_view, Value*>>();
        if (type->dyn_cast<StructType>()) {
            return Term::prvalue(new StructValue(type->cast<StructType>(), std::move(values)));
        } else {
            return Term::prvalue(new InstanceValue(type->cast<InstanceType>(), std::move(values)));
        }
    } else {
        return Term::prvalue(type);
    }
}

inline auto StructType::default_construct() const noexcept -> Term {
    GlobalMemory::FlatMap<std::string_view, Term> values;
    bool is_comptime = true;
    for (const auto& [name, field_type] : fields_) {
        Term default_value = field_type->default_construct();
        if (!default_value) {
            return {};
            break;
        }
        values.insert({name, default_value});
    }
    if (is_comptime) {
        return Term::prvalue(new StructValue(
            this,
            values |
                std::views::transform(
                    [](const std::pair<std::string_view, Term>& pair)
                        -> std::pair<std::string_view, Value*> {
                        return {pair.first, pair.second.get_comptime()};
                    }
                ) |
                GlobalMemory::collect<GlobalMemory::FlatMap>()
        ));
    } else {
        return Term::prvalue(this);
    }
}

inline auto MutableType::default_construct() const noexcept -> Term {
    Term default_value = target_type_->default_construct();
    return default_value ? Term::prvalue(new MutableValue(this, default_value.get_comptime()))
                         : Term{};
}

inline auto PointerType::default_construct() const noexcept -> Term {
    return Term::prvalue(new PointerValue(this, nullptr));
}

inline auto IntersectionType::default_construct() const noexcept -> Term {
    /// TODO:
    assert(false);
    return Term{};
}

inline auto UnionType::default_construct() const noexcept -> Term {
    /// TODO:
    assert(false);
    return Term{};
}

inline UnknownValue UnknownValue::instance;

inline NullptrValue NullptrValue::instance;
