#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"

enum class Kind : std::uint16_t {
    Unknown,
    Any,
    Null,
    Integer,
    Float,
    Boolean,
    Function,
    Array,
    Struct,
    Interface,
    Instance,
    Mut,
    Reference,
    Pointer,
    Intersection,
    Union,
    Template,
};

class Cursor;

class TypeRegistry;

class Object;

class Type;
class UnknownType;
class AnyType;
class NullType;
class IntegerType;
class FloatType;
class BooleanType;
class FunctionType;
class ArrayType;
class StructType;
class InterfaceType;
class ClassType;
class MutType;
class ReferenceType;
class PointerType;
class IntersectionType;
class UnionType;

class Value;
class UnknownValue;
class NullValue;
class IntegerValue;
class FloatValue;
class BooleanValue;
class FunctionValue;
class ArrayValue;
class InterfaceValue;
class ClassValue;
class InstanceValue;
class OverloadedFunctionValue;
class ReferenceValue;

template <typename T>
concept TypeClass = std::derived_from<T, Type> && !std::is_abstract_v<T>;
template <typename V>
concept ValueClass = std::derived_from<V, Value> && !std::is_abstract_v<V>;

using FunctionOverloads = GlobalMemory::Vector<Object*>;

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
    bool check_dependency(const Type* parent, const Type*& child) noexcept {
        if (!is_type_complete(child)) {
            edges_.push_back({parent, child, &child});
            return false;
        }
        return true;
    }

    GlobalMemory::Vector<Edge> extract_cycles(const Type* target) noexcept {
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

    bool is_dependent(const Type* parent) const noexcept {
        return std::ranges::any_of(edges_, [&](const Edge& edge) { return edge.parent == parent; });
    }

private:
    bool is_type_complete(const Type* type) const noexcept {
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

    TypeResolution(const Type* type) noexcept : ptr_(reinterpret_cast<std::uintptr_t>(type)) {}

    template <TypeClass T>
    TypeResolution(std::type_identity<T> identity) noexcept
        : ptr_(reinterpret_cast<std::uintptr_t>(GlobalMemory::alloc_raw(identity)) | flag) {}

    template <TypeClass T>
    TypeResolution& operator=(std::type_identity<T> identity) noexcept {
        ptr_ = reinterpret_cast<std::uintptr_t>(GlobalMemory::alloc_raw(identity)) | flag;
        return *this;
    }

    template <std::derived_from<Type> T>
    operator const T*() const noexcept {
        return static_cast<const T*>(reinterpret_cast<const Type*>(ptr_ & ~flag));
    }

    const Type* get() const noexcept { return reinterpret_cast<const Type*>(ptr_ & ~flag); }

    const Type* operator->() const noexcept { return get(); }

    bool is_sized() const noexcept { return (ptr_ & flag) == 0; }

private:
    template <TypeClass T>
    T* construct(auto&&... args) noexcept {
        assert(ptr_ && !is_sized());
        std::construct_at(
            reinterpret_cast<T*>(ptr_ & ~flag), std::forward<decltype(args)>(args)...
        );
        ptr_ &= ~flag;
        return reinterpret_cast<T*>(ptr_);
    }
};

class TypeRegistry {
    friend class ThreadGuard;

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
            !TypeInTupleV<T, std::tuple<AnyType, NullType, IntegerType, FloatType, BooleanType>>
        )
    static void get_at(TypeResolution& out, auto&&... args) noexcept {
        using Composites = std::tuple<
            FunctionType,
            ArrayType,
            StructType,
            ClassType,
            MutType,
            ReferenceType,
            PointerType,
            IntersectionType,
            UnionType>;
        if constexpr (std::is_same_v<T, ClassType>) {
            // classes with same definition are distinct types
            out = new T(std::forward<decltype(args)>(args)...);
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
            !TypeInTupleV<T, std::tuple<AnyType, NullType, IntegerType, FloatType, BooleanType>>
        )
    static const T* get(auto&&... args) noexcept {
        TypeResolution out = std::type_identity<T>();
        get_at<T>(out, std::forward<decltype(args)>(args)...);
        return static_cast<const T*>(out);
    }

    static const Type* get_unknown() noexcept;

    static void add_ref_dependency(const Type* parent, const Type* child) noexcept {
        instance->graph_.add_ref_dependency(parent, child);
    }

    static bool is_type_incomplete(const Type* type) noexcept {
        return instance->graph_.is_dependent(type);
    }

private:
    TypeDependencyGraph graph_;
    std::tuple<
        TypeSet<FunctionType>,
        TypeSet<ArrayType>,
        TypeSet<StructType>,
        TypeSet<MutType>,
        TypeSet<ReferenceType>,
        TypeSet<PointerType>,
        TypeSet<IntersectionType>,
        TypeSet<UnionType>>
        types_;
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

    std::pair<const Type*, bool> dispatch_pool(const Type* type) noexcept;

    const Type* simplify_recursive_type(
        GlobalMemory::Vector<TypeDependencyGraph::Edge> active_edges, const Type* type
    ) noexcept;

public:
    TypeRegistry() noexcept = default;
};

class Term : public GlobalMemory::MemoryManaged {
public:
    enum class Category {
        Type,        // a type -> const Type*
        CompConst,   // a compile-time constant value -> Value*
        CompVar,     // a compile-time variable -> Value*
        CompRValue,  // a compile-time temporary value -> Value*
        Var,         // a runtime variable -> const Type*
        RValue,      // a runtime temporary value -> const Type*
    };

public:
    static Term unknown() noexcept;

private:
    union {
        const Object* ptr_;
        const Type* type_;
        Value* value_;
    };
    Category category_;

public:
    Term(const Type* type, Category category) noexcept : type_(type), category_(category) {
        assert(
            category == Category::Type || category == Category::Var || category == Category::RValue
        );
    }
    Term(Value* value, Category category) noexcept : value_(value), category_(category) {
        assert(
            category == Category::CompConst || category == Category::CompVar ||
            category == Category::CompRValue
        );
    }

public:
    Term() noexcept = default;
    operator bool() const noexcept { return type_ != nullptr; }
    const Object* operator->() const noexcept { return ptr_; }
    const Type* effective_type() const noexcept;

    bool is_comptime() const noexcept {
        return category_ == Category::CompConst || category_ == Category::CompVar ||
               category_ == Category::CompRValue;
    }
    bool is_lvalue() const noexcept {
        return category_ == Category::CompVar || category_ == Category::Var;
    }
    Value* get_comptime() const noexcept { return is_comptime() ? value_ : nullptr; }
};

class Object : public GlobalMemory::MemoryManaged {
public:
    Kind kind_;

private:
    bool is_type_;

public:
    Object(Kind kind, bool is_type) noexcept : kind_(kind), is_type_(is_type) {}

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

    virtual ~Object() = default;

    virtual std::string_view repr() const = 0;

    virtual void transpile(Cursor& cursor) const noexcept = 0;
};

class Type : public Object {
protected:
    Type(Kind kind) noexcept : Object(kind, true) {}

public:
    Type* dyn_type() = delete;
    Value* dyn_value() = delete;

    virtual bool can_intern(TypeDependencyGraph& graph) noexcept = 0;

    std::strong_ordering compare(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept {
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
        return compare_impl(other, assumed_equal);
    }

    bool assignable_from(const Type* source) const {
        assert(!(this == source) || assignable_from_impl(source));
        return this == source || assignable_from_impl(source);
    }

    virtual const Type* member(std::string_view name) const noexcept { return nullptr; }

protected:
    virtual std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept = 0;

    virtual bool assignable_from_impl(const Type* source) const = 0;
};

class PrimitiveType : public Type {
protected:
    PrimitiveType(Kind kind) noexcept : Type(kind) {}

    bool can_intern(TypeDependencyGraph& graph) noexcept final { return true; }

    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        return this <=> other;
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
    std::string_view repr() const final { return "unknown"; }
    bool assignable_from_impl(const Type* source) const final { return true; }
    void transpile(Cursor& cursor) const noexcept final;
};

class AnyType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Any;
    static AnyType instance;

public:
    AnyType() noexcept : PrimitiveType(kind) {}
    std::string_view repr() const final { return "any"; }
    bool assignable_from_impl(const Type* source) const final { return true; }
    void transpile(Cursor& cursor) const noexcept final;
};

class NullType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Null;
    static NullType instance;

public:
    NullType() noexcept : PrimitiveType(kind) {}
    std::string_view repr() const final { return "null"; }
    bool assignable_from_impl(const Type* source) const final {
        /// No variable can have null type except null literal
        UNREACHABLE();
    }
    void transpile(Cursor& cursor) const noexcept final;
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
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}{}", is_signed_ ? "i" : "u", bits_);
    }
    bool assignable_from_impl(const Type* source) const final {
        const IntegerType* other_int = source->dyn_cast<IntegerType>();
        return other_int && (other_int->bits_ == 0 || (this->is_signed_ == other_int->is_signed_ &&
                                                       this->bits_ >= other_int->bits_));
    }
    void transpile(Cursor& cursor) const noexcept final;
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
    std::string_view repr() const final { return GlobalMemory::format_view("f{}", bits_); }
    bool assignable_from_impl(const Type* source) const final {
        const FloatType* other_float = source->dyn_cast<FloatType>();
        return other_float && (other_float->bits_ == 0 || this->bits_ >= other_float->bits_);
    }
    void transpile(Cursor& cursor) const noexcept final;
};

class BooleanType final : public PrimitiveType {
public:
    static constexpr Kind kind = Kind::Boolean;
    static BooleanType instance;

public:
    BooleanType() noexcept : PrimitiveType(kind) {}
    std::string_view repr() const final { return "bool"; }
    bool assignable_from_impl(const Type* source) const final {
        return source->dyn_cast<BooleanType>() != nullptr;
    }
    void transpile(Cursor& cursor) const noexcept final;
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    ComparableSpan<const Type*> parameters_;
    const Type* return_type_;

public:
    FunctionType(ComparableSpan<const Type*> parameters, const Type* return_type) noexcept
        : Type(kind), parameters_(parameters), return_type_(return_type) {}

    std::string_view repr() const final {
        GlobalMemory::String params_repr =
            parameters_ | std::views::transform([](const Type* type) { return type->repr(); }) |
            std::views::join_with(", "sv) | GlobalMemory::collect<GlobalMemory::String>();
        return GlobalMemory::format_view("({}) => {}", params_repr, return_type_->repr());
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

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
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

    bool assignable_from_impl(const Type* source) const final;
};

class ArrayType final : public Type {
public:
    static constexpr Kind kind = Kind::Array;

public:
    const Type* element_type_;
    std::size_t size_ = 0;  // 0 means dynamic size

public:
    ArrayType(const Type* element_type) noexcept : Type(kind), element_type_(element_type) {}

    std::string_view repr() const final {
        return GlobalMemory::format_view("{}[]", element_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return graph.check_dependency(this, element_type_);
    }

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ArrayType* other_array = other->cast<ArrayType>();
        if (size_ != other_array->size_) {
            return size_ <=> other_array->size_;
        }
        return element_type_->compare(other_array->element_type_, assumed_equal);
    }

    bool assignable_from_impl(const Type* source) const final {
        const ArrayType* other_array = source->dyn_cast<ArrayType>();
        return other_array && element_type_->assignable_from(other_array->element_type_) &&
               (size_ == 0 || size_ == other_array->size_);
    }
};

class StructType : public Type {
public:
    static constexpr Kind kind = Kind::Struct;

public:
    GlobalMemory::FlatMap<std::string_view, const Type*> fields_;

public:
    StructType(GlobalMemory::FlatMap<std::string_view, const Type*> fields) noexcept
        : Type(kind), fields_(std::move(fields)) {}

    std::string_view repr() const final {
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

    void transpile(Cursor& cursor) const noexcept final;

    const Type* member(std::string_view name) const noexcept final {
        auto it = fields_.find(name);
        if (it == fields_.end()) {
            return nullptr;
        }
        return it->second;
    }

protected:
    std::strong_ordering compare_impl(
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

    bool assignable_from_impl(const Type* source) const final {
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
};

class InterfaceType final : public Type {
public:
    static constexpr Kind kind = Kind::Interface;

private:
    GlobalMemory::FlatMap<std::string_view, OverloadedFunctionValue*> methods_;

public:
    InterfaceType() noexcept : Type(kind) {}

    std::string_view repr() const override {
        /// TODO:
        return {};
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        /// TODO:
        return true;
    }

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        /// TODO:
        return std::strong_ordering::equal;
    }

    bool assignable_from_impl(const Type* source) const final {
        /// TODO:
        return false;
    }
};

class ClassType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

private:
    const void* scope_;
    std::string_view identifier_;
    const Type* extends_;
    ComparableSpan<const Type*> implements_;
    GlobalMemory::FlatMap<std::string_view, const Type*> attr_;
    GlobalMemory::FlatMap<std::string_view, FunctionOverloads> methods_;

public:
    ClassType(
        const void* scope,
        std::string_view identifier,
        const Type* extends,
        ComparableSpan<const Type*> interfaces,
        GlobalMemory::FlatMap<std::string_view, const Type*> attr,
        GlobalMemory::FlatMap<std::string_view, FunctionOverloads> methods
    ) noexcept
        : Type(kind),
          scope_(scope),
          identifier_(identifier),
          extends_(extends),
          implements_(interfaces),
          attr_(std::move(attr)),
          methods_(std::move(methods)) {}

    std::string_view repr() const override {
        return GlobalMemory::format_view("class {}", identifier_);
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final { UNREACHABLE(); }

    FunctionOverloads get_method(std::string_view name) const {
        auto it = methods_.find(name);
        if (it == methods_.end()) {
            throw UnlocatedProblem::make<AttributeError>(
                GlobalMemory::format_view("Class {} has no method named {}", identifier_, name)
            );
        }
        return it->second;
    }

    const Type* get_attr(std::string_view name) const {
        auto it = attr_.find(name);
        if (it == attr_.end()) {
            throw UnlocatedProblem::make<AttributeError>(
                GlobalMemory::format_view("Class {} has no attribute named {}", identifier_, name)
            );
        }
        return it->second;
    }

    void transpile(Cursor& cursor) const noexcept override;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        UNREACHABLE();
    }

    bool assignable_from_impl(const Type* other) const final { return false; }
};

class MutType final : public Type {
public:
    static constexpr Kind kind = Kind::Mut;

public:
    const Type* target_type_;

public:
    MutType(const Type* target_type) noexcept : Type(kind), target_type_(target_type) {}

    std::string_view repr() const final {
        return GlobalMemory::format_view("mut {}", target_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return graph.check_dependency(this, target_type_);
    }

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const MutType* other_mut = other->cast<MutType>();
        return target_type_->compare(other_mut->target_type_, assumed_equal);
    }

    bool assignable_from_impl(const Type* source) const final {
        const MutType* other_mut = source->dyn_cast<MutType>();
        return other_mut && target_type_->assignable_from(other_mut->target_type_);
    }
};

class ReferenceType final : public Type {
public:
    static constexpr Kind kind = Kind::Reference;

public:
    const Type* referenced_type_;
    bool is_mutable_;

public:
    ReferenceType(const Type* referenced_type, bool is_mutable) noexcept
        : Type(kind), referenced_type_(referenced_type), is_mutable_(is_mutable) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view(
            "&{}{}", is_mutable_ ? "mut " : "", referenced_type_->repr()
        );
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_dependent(this) && graph.check_dependency(this, referenced_type_);
    }

    const Type* member(std::string_view name) const noexcept final {
        return referenced_type_->member(name);
    }

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ReferenceType* other_ref = other->cast<ReferenceType>();
        if (is_mutable_ != other_ref->is_mutable_) {
            return is_mutable_ <=> other_ref->is_mutable_;
        }
        return referenced_type_->compare(other_ref->referenced_type_, assumed_equal);
    }

    bool assignable_from_impl(const Type* source) const final {
        const ReferenceType* other_ref = source->dyn_cast<ReferenceType>();
        return other_ref && referenced_type_->assignable_from(other_ref->referenced_type_);
    }
};

class PointerType final : public Type {
public:
    static constexpr Kind kind = Kind::Pointer;

public:
    const Type* pointed_type_;
    bool is_mutable_;

public:
    PointerType(const Type* pointed_type, bool is_mutable) noexcept
        : Type(kind), pointed_type_(pointed_type), is_mutable_(is_mutable) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("*{}{}", is_mutable_ ? "mut " : "", pointed_type_->repr());
    }

    bool can_intern(TypeDependencyGraph& graph) noexcept final {
        return !graph.is_dependent(this) && graph.check_dependency(this, pointed_type_);
    }

    const Type* member(std::string_view name) const noexcept final {
        return pointed_type_->member(name);
    }

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
        const Type* other, GlobalMemory::FlatSet<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const PointerType* other_ptr = other->cast<PointerType>();
        if (is_mutable_ != other_ptr->is_mutable_) {
            return is_mutable_ <=> other_ptr->is_mutable_;
        }
        return pointed_type_->compare(other_ptr->pointed_type_, assumed_equal);
    }

    bool assignable_from_impl(const Type* source) const final {
        const PointerType* other_ptr = source->dyn_cast<PointerType>();
        return other_ptr && pointed_type_->assignable_from(other_ptr->pointed_type_);
    }
};

class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::Intersection;

private:
    static ComparableSpan<const Type*> flatten(ComparableSpan<const Type*> unflattened_types) {
        std::size_t size = 0;
        for (const Type* type : unflattened_types) {
            if (const IntersectionType* intersection_type = type->dyn_cast<IntersectionType>()) {
                size += intersection_type->types_.size();
            } else {
                size++;
            }
        }
        ComparableSpan<const Type*> buffer = GlobalMemory::alloc_array<const Type*>(size);
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
    static ComparableSpan<const Type*> flatten(const Type* left, const Type* right) {
        const Type* types[] = {left, right};
        return flatten(ComparableSpan<const Type*>(types));
    }

public:
    ComparableSpan<const Type*> types_;

public:
    IntersectionType(auto&&... unflattened_types) noexcept
        : Type(kind), types_{flatten(unflattened_types...)} {}

    std::string_view repr() const final {
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

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
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

    bool assignable_from_impl(const Type* source) const final {
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
};

class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::Union;

private:
    static ComparableSpan<const Type*> flatten(ComparableSpan<const Type*> unflattened_types) {
        std::size_t size = 0;
        for (const Type* type : unflattened_types) {
            if (const UnionType* union_type = type->dyn_cast<UnionType>()) {
                size += union_type->types_.size();
            } else {
                size++;
            }
        }
        ComparableSpan<const Type*> buffer = GlobalMemory::alloc_array<const Type*>(size);
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
    static ComparableSpan<const Type*> flatten(const Type* left, const Type* right) {
        const Type* types[] = {left, right};
        return flatten(ComparableSpan<const Type*>(types));
    }

public:
    ComparableSpan<const Type*> types_;

public:
    UnionType(auto&&... unflattened_types) noexcept
        : Type(kind), types_(flatten(unflattened_types...)) {}

    std::string_view repr() const final {
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

    void transpile(Cursor& cursor) const noexcept final;

protected:
    std::strong_ordering compare_impl(
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

    bool assignable_from_impl(const Type* source) const final {
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
};

class Value : public Object {
public:
    template <ValueClass V>
    static Value* from_literal(std::string_view literal) {
        if constexpr (std::is_same_v<V, NullValue>) {
            assert(literal == "null");
            return new V();
        } else if constexpr (std::is_same_v<V, IntegerValue>) {
            int64_t value;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec == std::errc()) {
                return new V(literal);
            } else if (ec == std::errc::result_out_of_range && literal[0] != '-') {
                uint64_t uvalue;
                auto [uptr, uec] =
                    std::from_chars(literal.data(), literal.data() + literal.size(), uvalue);
                if (uec == std::errc()) {
                    return new V(literal);
                }
            }
            return nullptr;
        } else if constexpr (std::is_same_v<V, FloatValue>) {
            double value;
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
    virtual Value* resolve_to(const Type* target) const = 0;
    virtual void assign_from(Value* source) = 0;
    virtual Value* member(std::string_view name) const noexcept { return nullptr; }
};

class UnknownValue final : public Value {
    friend class Object;
    friend class Term;

public:
    static constexpr Kind kind = Kind::Unknown;

private:
    UnknownValue() noexcept : Value(kind) {}
    std::string_view repr() const final { return "unknown"; }
    UnknownType* get_type() const noexcept final { return &UnknownType::instance; }
    UnknownValue* clone() const noexcept final { return new UnknownValue(*this); }
    UnknownValue* resolve_to(const Type* target) const noexcept final { return new UnknownValue(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    void transpile(Cursor& cursor) const noexcept final;
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::Null;

public:
    NullValue() noexcept : Value(kind) {}
    std::string_view repr() const final { return "null"; }
    const NullType* get_type() const noexcept final { return &NullType::instance; }
    NullValue* clone() const noexcept final { return new NullValue(*this); }
    NullValue* resolve_to(const Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Null) {
            throw UnlocatedProblem::make<TypeMismatchError>("null", target->repr());
        }
        return new NullValue();
    }
    void assign_from(Value* source) final { UNREACHABLE(); }
    void transpile(Cursor& cursor) const noexcept final;
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
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}", value_.to_string());
    }
    const IntegerType* get_type() const noexcept final { return type_; }
    IntegerValue* clone() const noexcept final { return new IntegerValue(*this); }
    IntegerValue* resolve_to(const Type* target) const final {
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
    void transpile(Cursor& cursor) const noexcept final;
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
    std::string_view repr() const final { return GlobalMemory::format_view("{}", value_); }
    const FloatType* get_type() const noexcept final { return type_; }
    FloatValue* clone() const noexcept final { return new FloatValue(*this); }
    FloatValue* resolve_to(const Type* target) const final {
        if (target && !target->dyn_cast<FloatType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("float", target->repr());
        }
        if (target == nullptr) {
            if (type_) {
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
    void transpile(Cursor& cursor) const noexcept final;
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::Boolean;

public:
    bool value_;

public:
    BooleanValue(bool value) noexcept : Value(kind), value_(value) {}
    std::string_view repr() const noexcept final { return this->value_ ? "true" : "false"; }
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
    void transpile(Cursor& cursor) const noexcept final;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::Function;

public:
    const FunctionType* type_;
    std::function<Term(ComparableSpan<Term>)> callback_;

public:
    FunctionValue(const FunctionType* type, decltype(callback_) invoke) noexcept
        : Value(kind), type_(type), callback_(std::move(invoke)) {}

    std::string_view repr() const final {
        return GlobalMemory::format_view("<function at {:p}>", static_cast<const void*>(this));
    }
    const FunctionType* get_type() const noexcept final { return type_; }
    FunctionValue* clone() const noexcept final { UNREACHABLE(); }
    FunctionValue* resolve_to(const Type* target) const noexcept final { UNREACHABLE(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    Term invoke(ComparableSpan<Term> args) const { return callback_(args); }
    void transpile(Cursor& cursor) const noexcept final;
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
    ArrayValue(GlobalMemory::Vector<Value*>&& elements) noexcept
        : Value(kind), type_(nullptr), elements_(std::move(elements)) {}
    ArrayValue(std::string_view string) noexcept
        : Value(kind),
          type_(TypeRegistry::get<ArrayType>(&IntegerType::u8_instance)),
          string_(string) {}
    ~ArrayValue() noexcept {
        if (type_->element_type_ == &IntegerType::u8_instance) {
            // string literal, no need to delete elements
        } else {
            std::destroy_at(&elements_);
        }
    };
    std::string_view repr() const noexcept final {
        return GlobalMemory::format_view("[{}]", type_->element_type_->repr());
    }
    const ArrayType* get_type() const noexcept final { return type_; }
    ArrayValue* clone() const noexcept final {
        if (type_) {
            GlobalMemory::Vector<Value*> cloned_elements;
            for (Value* element : elements_) {
                cloned_elements.push_back(element->clone());
            }
            return new ArrayValue(std::move(cloned_elements));
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
    void transpile(Cursor& cursor) const noexcept final;
};

class InstanceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    const ClassType* cls_;
    GlobalMemory::FlatMap<std::string_view, Value*> attributes_;

public:
    InstanceValue(const ClassType* cls, decltype(attributes_) attributes) noexcept
        : Value(kind), cls_(cls), attributes_(std::move(attributes)) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("<instance of {}>", cls_->repr());
    }
    const ClassType* get_type() const noexcept final { return cls_; }
    InstanceValue* clone() const noexcept final {
        GlobalMemory::FlatMap<std::string_view, Value*> cloned_attributes;
        for (const auto& [name, value] : attributes_) {
            cloned_attributes.insert({name, value->clone()});
        }
        return new InstanceValue(cls_, std::move(cloned_attributes));
    }
    InstanceValue* resolve_to(const Type* target) const final {
        if (target && target != cls_) {
            throw UnlocatedProblem::make<TypeMismatchError>("instance", target->repr());
        }
        return new InstanceValue(*this);
    }
    void assign_from(Value* source) final {
        InstanceValue* instance_source = source->cast<InstanceValue>();
        this->attributes_ = instance_source->attributes_;
    }
    Value* get_attr(std::string_view attr) noexcept { return attributes_.at(attr); }
    void transpile(Cursor& cursor) const noexcept final;
};

class ReferenceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Reference;

private:
    const ReferenceType* type_;
    Value** referenced_value_;

public:
    ReferenceValue(const ReferenceType* type, Value** referenced_value) noexcept
        : Value(kind), type_(type), referenced_value_(referenced_value) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("&{}", (*referenced_value_)->repr());
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
    void transpile(Cursor& cursor) const noexcept final;
};

inline bool TypeRegistry::TypeComparator::operator()(
    const Type* lhs, const Type* rhs
) const noexcept {
    GlobalMemory::FlatSet<std::pair<const Type*, const Type*>> assumed_equal;
    return lhs->compare(rhs, assumed_equal) == std::strong_ordering::less;
}

inline thread_local std::optional<TypeRegistry> TypeRegistry::instance;

inline const Type* TypeRegistry::get_unknown() noexcept { return &UnknownType::instance; }

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
    case Kind::Intersection: {
        auto [it, inserted] =
            std::get<TypeSet<IntersectionType>>(types_).insert(type->cast<IntersectionType>());
        return {*it, inserted};
    }
    case Kind::Union: {
        auto [it, inserted] = std::get<TypeSet<UnionType>>(types_).insert(type->cast<UnionType>());
        return {*it, inserted};
    }
    case Kind::Reference: {
        auto [it, inserted] =
            std::get<TypeSet<ReferenceType>>(types_).insert(type->cast<ReferenceType>());
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

inline Term Term::unknown() noexcept { return Term(new UnknownValue(), Term::Category::RValue); }

inline const Type* Term::effective_type() const noexcept {
    if (auto type = ptr_->dyn_type()) {
        return type;
    } else {
        return ptr_->cast<Value>()->get_type();
    }
}

inline UnknownType UnknownType::instance;

inline AnyType AnyType::instance;

inline NullType NullType::instance;

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

inline bool FunctionType::assignable_from_impl(const Type* source) const {
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
