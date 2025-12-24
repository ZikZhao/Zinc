#pragma once
#include "pch.hpp"
#include <compare>
#include <string_view>
#include <type_traits>
#include <utility>

class Scope;

enum class Kind : std::uint16_t {
    KIND_NO_RIGHT_OPERAND,
    KIND_ANY,
    KIND_NULL,
    KIND_INTEGER,
    KIND_FLOAT,
    KIND_STRING,
    KIND_BOOLEAN,
    NON_COMPOSITE_SIZE,
    KIND_FUNCTION,
    KIND_LIST,
    KIND_RECORD,
    KIND_INTERFACE,
    KIND_CLASS,
    KIND_OBJECT,
    KIND_INTERSECTION,
    KIND_UNION,
};

class Entity;
class Value;
class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;
class FunctionValue;
class InterfaceValue;
class ClassValue;
class ObjectValue;
class ListValue;
class DictValue;
class SetValue;

class Type;
template <Kind Kind>
class PrimitiveType;
using AnyType = PrimitiveType<Kind::KIND_ANY>;
using NullType = PrimitiveType<Kind::KIND_NULL>;
using IntegerType = PrimitiveType<Kind::KIND_INTEGER>;
using FloatType = PrimitiveType<Kind::KIND_FLOAT>;
using StringType = PrimitiveType<Kind::KIND_STRING>;
using BooleanType = PrimitiveType<Kind::KIND_BOOLEAN>;
class InterfaceType;
class StructType;
class ClassType;
class IntersectionType;
class UnionType;

template <typename T>
concept TypeClass = std::derived_from<T, Type>;
template <typename T>
concept ValueClass = std::derived_from<T, Value>;

// using Arguments = std::vector<ValueRef>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;

class EntityRef;
using ValueRef = EntityRef;
using TypeRef = EntityRef;

class Entity {
    friend class EntityRef;

private:
    mutable std::int32_t ref_count_;

public:
    Kind kind_;
    Entity(Kind kind, bool is_value) noexcept;
    virtual ~Entity() = default;
    virtual std::string repr() const = 0;
    constexpr std::strong_ordering operator<=>(const Entity& other) const noexcept {
        if (kind_ != other.kind_) {
            return kind_ <=> other.kind_;
        }
        return is_type() <=> other.is_type();
    }
    constexpr bool operator==(const Entity& other) const noexcept {
        return kind_ == other.kind_ && is_type() == other.is_type();
    }

private:
    constexpr bool is_type() const noexcept { return ref_count_ <= 0; }
};

class Type : public Entity {
public:
    Type(Kind kind) noexcept;
    ~Type() override = default;
    bool assignable_from(const Type& source) const;

protected:
    virtual bool assignable_from_impl(const Type& source) const = 0;
};

template <Kind K>
class PrimitiveType final : public Type {
public:
    static constexpr Kind kind = K;

public:
    PrimitiveType() noexcept : Type(kind) {}
    std::string repr() const final {
        if constexpr (K == Kind::KIND_ANY) {
            return "any";
        } else if constexpr (K == Kind::KIND_NULL) {
            return "null";
        } else if constexpr (K == Kind::KIND_INTEGER) {
            return "integer";
        } else if constexpr (K == Kind::KIND_FLOAT) {
            return "float";
        } else if constexpr (K == Kind::KIND_STRING) {
            return "string";
        } else if constexpr (K == Kind::KIND_BOOLEAN) {
            return "boolean";
        } else if constexpr (K == Kind::KIND_FUNCTION) {
            return "function";
        } else {
            static_assert(false);
        }
    }
    bool assignable_from_impl(const Type& source) const final {
        if constexpr (K == Kind::KIND_ANY) {
            return true;
        } else {
            return source.kind_ == kind;
        }
    }
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_FUNCTION;

public:
    Type* return_type_;
    ComparableSpan<Type*> parameters_;
    Type* spread_;
    FunctionType(Type* return_type, ComparableSpan<Type*> parameters, Type* spread) noexcept
        : Type(kind), return_type_(return_type), parameters_(parameters), spread_(spread) {}
    std::string repr() const final;
    bool assignable_from_impl(const Type& source) const final;
    std::strong_ordering operator<=>(const FunctionType& other) const noexcept = default;
};

class ListType final : public Type {
public:
    Type* element_type_;
    ListType(Type* element_type) noexcept;
    std::string repr() const final;
    bool assignable_from_impl(const Type& source) const final;
};

class RecordType : public Type {
public:
    FlatMap<std::string_view, Type*> fields_;
    RecordType(FlatMap<std::string_view, Type*> fields) noexcept;
    std::string repr() const final;
    bool assignable_from_impl(const Type& source) const final;
    std::strong_ordering operator<=>(const RecordType& other) const noexcept = default;
};

class ClassType : public Type {
public:
    std::string_view name_;
    ComparableSpan<InterfaceType*> interfaces_;
    const ClassType* extends_;
    FlatMap<std::string_view, Type*> properties_;
    ClassType(
        std::string_view name,
        ComparableSpan<InterfaceType*> interfaces,
        const ClassType* extends,
        FlatMap<std::string_view, Type*> properties
    ) noexcept;
    std::string repr() const override;
    bool assignable_from_impl(const Type& source) const override;
};

class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_INTERSECTION;

private:
    static ComparableSpan<Type*> combine(Type* left, Type* right);

public:
    ComparableSpan<Type*> types_;
    IntersectionType(Type* left, Type* right) noexcept;
    std::string repr() const final;
    bool assignable_from_impl(const Type& source) const final;
    std::strong_ordering operator<=>(const IntersectionType& other) const noexcept = default;
};

class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_UNION;

private:
    static ComparableSpan<Type*> combine(Type* left, Type* right);

public:
    ComparableSpan<Type*> types_;
    UnionType(Type* left, Type* right) noexcept;
    std::string repr() const final;
    bool assignable_from_impl(const Type& source) const final;
    std::strong_ordering operator<=>(const UnionType& other) const noexcept = default;
};

class Value : public Entity {
    friend class EntityRef;

private:
    template <ValueClass V>
    static Value* from_literal(std::string_view literal) {
        if constexpr (std::is_same_v<V, NullValue>) {
            assert(literal == "null");
            return new V();
        } else if constexpr (std::is_same_v<V, IntegerValue>) {
            int64_t value;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec != std::errc()) {
                throw std::runtime_error("Invalid integer literal: "s + literal.data());
            }
            return new V(value);
        } else if constexpr (std::is_same_v<V, FloatValue>) {
            double value;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec != std::errc()) {
                throw std::runtime_error("Invalid float literal: "s + literal.data());
            }
            return new V(value);
        } else if constexpr (std::is_same_v<V, StringValue>) {
            return new V(std::string(literal));
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

public:
    Value(Kind kind) noexcept;
    ~Value() override = default;
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_NULL;
    using Type = NullType;

public:
    NullValue() noexcept;
    std::string repr() const final;
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_INTEGER;
    using Type = IntegerType;

public:
    std::int64_t value_;
    IntegerValue(std::int64_t value) noexcept;
    std::string repr() const final;
    IntegerValue operator+(const IntegerValue& other) const;
    IntegerValue operator-(const IntegerValue& other) const;
    IntegerValue operator-() const;
    IntegerValue operator*(const IntegerValue& other) const;
    IntegerValue operator/(const IntegerValue& other) const;
    IntegerValue operator%(const IntegerValue& other) const;
    BooleanValue operator==(const IntegerValue& other) const;
    BooleanValue operator!=(const IntegerValue& other) const;
    BooleanValue operator<(const IntegerValue& other) const;
    BooleanValue operator<=(const IntegerValue& other) const;
    BooleanValue operator>(const IntegerValue& other) const;
    BooleanValue operator>=(const IntegerValue& other) const;
    IntegerValue operator&(const IntegerValue& other) const;
    IntegerValue operator|(const IntegerValue& other) const;
    IntegerValue operator^(const IntegerValue& other) const;
    IntegerValue operator~() const;
    IntegerValue operator<<(const IntegerValue& other) const;
    IntegerValue operator>>(const IntegerValue& other) const;
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_FLOAT;
    using Type = FloatType;

public:
    double value_;
    FloatValue(double value) noexcept;
    std::string repr() const final;
    FloatValue operator+(const FloatValue& other) const;
    FloatValue operator-(const FloatValue& other) const;
    FloatValue operator-() const;
    FloatValue operator*(const FloatValue& other) const;
    FloatValue operator/(const FloatValue& other) const;
    FloatValue operator%(const FloatValue& other) const;
    BooleanValue operator==(const FloatValue& other) const;
    BooleanValue operator!=(const FloatValue& other) const;
    BooleanValue operator<(const FloatValue& other) const;
    BooleanValue operator<=(const FloatValue& other) const;
    BooleanValue operator>(const FloatValue& other) const;
    BooleanValue operator>=(const FloatValue& other) const;
};

class StringValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_STRING;
    using Type = StringType;

public:
    std::string value_;
    StringValue(std::string value) noexcept;
    std::string repr() const final;
    StringValue operator+(const StringValue& other) const;
    StringValue operator*(const IntegerValue& other) const;
    BooleanValue operator==(const StringValue& other) const;
    BooleanValue operator!=(const StringValue& other) const;
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_BOOLEAN;
    using Type = BooleanType;

public:
    bool value_;
    BooleanValue(bool value) noexcept;
    std::string repr() const final;
    BooleanValue operator==(const BooleanValue& other) const;
    BooleanValue operator!=(const BooleanValue& other) const;
    BooleanValue operator&&(const BooleanValue& other) const;
    BooleanValue operator||(const BooleanValue& other) const;
    BooleanValue operator!() const;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_FUNCTION;
    using Type = FunctionType;

public:
    const std::function<Value*(const std::vector<Value*>&)> callback_;
    Type* type_;
    FunctionValue(auto&& callback, Type* function_type) noexcept
        : Value(Kind::KIND_FUNCTION),
          callback_(std::forward<decltype(callback)>(callback)),
          type_(function_type) {}
    std::string repr() const final;
    Value* operator()(const std::vector<Value*>& args) const;
};

class ObjectValue : public Value {
public:
    static constexpr Kind kind = Kind::KIND_OBJECT;

public:
    ClassType* cls_;
    std::vector<Value*> attributes_;
    ObjectValue(ClassType* cls) noexcept;
    Value* get(const std::string_view property) const noexcept;
};

class ListValue : public ObjectValue {
private:
    static ListValue* Append(const std::vector<Value*>& args) noexcept;

public:
    std::vector<Value*> values_;
    ListValue() noexcept;
    ListValue(std::vector<Value*>&& values) noexcept;
    std::string repr() const final;
    ListValue* operator+(const ListValue& other) const;
    ListValue* operator*(const IntegerValue& other) const;
    Value* operator[](const Slice& indices) const;
};

class EntityRef {
    friend class IntrinsicOpTable;
    friend class TypeRegistry;

public:
    template <ValueClass V>
    static ValueRef alloc_literal(std::string_view literal) {
        return ValueRef(Value::from_literal<V>(literal));
    }

private:
    template <TypeClass T, typename... Args>
    static TypeRef alloc(Args&&... args) {
        return TypeRef(GlobalMemory::allocate<T>(std::forward<decltype(args)>(args)...));
    }

private:
    Entity* ptr_;

public:
    EntityRef() noexcept = default;
    EntityRef(const EntityRef& other) noexcept;
    EntityRef(EntityRef&& other) noexcept;
    ~EntityRef() noexcept;
    EntityRef& operator=(EntityRef other) noexcept;
    operator bool() const noexcept;
    Entity& operator*() const noexcept;
    Entity* operator->() const noexcept;
    bool is_type() const noexcept { return ptr_ ? ptr_->is_type() : true; }
    bool is_value() const noexcept { return ptr_ ? !ptr_->is_type() : true; }
    Value* value() const noexcept;
    Type* type() const noexcept;

private:
    explicit EntityRef(Entity* ptr) noexcept;
    void retain() noexcept;
    void release() noexcept;
};

class TypeRegistry {
private:
    using Primitives =
        std::tuple<AnyType, NullType, IntegerType, FloatType, StringType, BooleanType>;
    AnyType any_type_instance_;
    IntegerType integer_type_instance_;
    FloatType float_type_instance_;
    StringType string_type_instance_;
    BooleanType boolean_type_instance_;
    FlatSet<FunctionType> function_types_;
    FlatSet<ListType> list_types_;
    FlatSet<RecordType> record_types_;
    FlatSet<IntersectionType> intersection_types_;
    FlatSet<UnionType> union_types_;

public:
    TypeRef of(ValueRef value) {
        assert(value && value.is_value());
        switch (value->kind_) {
        case Kind::KIND_NULL:
            return TypeRef(&any_type_instance_);
        case Kind::KIND_INTEGER:
            return TypeRef(&integer_type_instance_);
        case Kind::KIND_FLOAT:
            return TypeRef(&float_type_instance_);
        case Kind::KIND_STRING:
            return TypeRef(&string_type_instance_);
        case Kind::KIND_BOOLEAN:
            return TypeRef(&boolean_type_instance_);
        case Kind::KIND_FUNCTION:
            return TypeRef(static_cast<FunctionValue*>(value.value())->type_);
        case Kind::KIND_OBJECT:
            return TypeRef(static_cast<ObjectValue*>(value.value())->cls_);
            // TODO: handle other kinds
        }
    }

    template <TypeClass T>
    TypeRef get(auto&&... args) {
        if constexpr (TypeInTupleV<T, Primitives>) {
            return get_kind(T::kind);
        } else if constexpr (std::is_same_v<T, FunctionType>) {
            return get_cached<FunctionType>(function_types_, std::forward<decltype(args)>(args)...);
        } else if constexpr (std::is_same_v<T, ListType>) {
            return get_cached<ListType>(list_types_, std::forward<decltype(args)>(args)...);
        } else if constexpr (std::is_same_v<T, RecordType>) {
            return get_cached<RecordType>(record_types_, std::forward<decltype(args)>(args)...);
        } else if constexpr (std::is_same_v<T, IntersectionType>) {
            return get_cached<IntersectionType>(
                intersection_types_, std::forward<decltype(args)>(args)...
            );
        } else if constexpr (std::is_same_v<T, UnionType>) {
            return get_cached<UnionType>(union_types_, std::forward<decltype(args)>(args)...);
        } else if constexpr (std::is_same_v<T, ClassType>) {
            // classes with same definition are distinct types
            return TypeRef::alloc<T>(std::forward<decltype(args)>(args)...);
        } else {
            static_assert(false, "Unknown type for TypeRegistry::get");
            // TODO: handle other types
        }
    }

    TypeRef get_kind(Kind kind) {
        switch (kind) {
        case Kind::KIND_ANY:
            return TypeRef(&any_type_instance_);
        case Kind::KIND_INTEGER:
            return TypeRef(&integer_type_instance_);
        case Kind::KIND_FLOAT:
            return TypeRef(&float_type_instance_);
        case Kind::KIND_STRING:
            return TypeRef(&string_type_instance_);
        case Kind::KIND_BOOLEAN:
            return TypeRef(&boolean_type_instance_);
        default:
            assert(false && "Unknown primitive kind");
            std::unreachable();
        }
    }

private:
    template <typename T>
    TypeRef get_cached(FlatSet<T>& instances, auto&&... args) {
        T type(std::forward<decltype(args)>(args)...);
        T& result = instances.try_emplace(type);
        return TypeRef(&result);
    }
};

// TODO: implement unique cast static caching
