#pragma once
#include "pch.hpp"
#include <string_view>
#include <utility>

class Context;

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
class AnyType;
template <Kind Kind>
class PrimitiveType;
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
public:
    const Kind kind_;
    mutable std::int64_t ref_count_;
    Entity(Kind kind, bool is_value) noexcept;
    virtual ~Entity() = default;
    virtual std::string repr() const = 0;
    constexpr bool is_type() const noexcept { return ref_count_ <= 0; }
};

class Type : public Entity {
public:
    static bool contains(const Type& a, const Type& b) { return &a == &b || a.contains(b); }

public:
    Type(Kind kind) noexcept;
    ~Type() override = default;

protected:
    virtual bool contains(const Type& other) const = 0;
};

class AnyType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_ANY;

public:
    AnyType() noexcept;
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

template <Kind K>
class PrimitiveType final : public Type {
public:
    static constexpr Kind kind = K;

public:
    PrimitiveType() noexcept : Type(kind) {}
    std::string repr() const final {
        if constexpr (K == Kind::KIND_STRING) {
            return "string";
        } else if constexpr (K == Kind::KIND_BOOLEAN) {
            return "boolean";
        } else if constexpr (K == Kind::KIND_FUNCTION) {
            return "function";
        } else {
            std::unreachable();
        }
    }
    bool contains(const Type& other) const final { return other.kind_ == kind; }
};

template <>
class PrimitiveType<Kind::KIND_INTEGER> final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_INTEGER;

public:
    PrimitiveType() noexcept : Type(kind) {}
    std::string repr() const final { return "integer"; }
    bool contains(const Type& other) const final {
        return other.kind_ == Kind::KIND_INTEGER or other.kind_ == Kind::KIND_FLOAT;
    }
};

template <>
class PrimitiveType<Kind::KIND_FLOAT> final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_FLOAT;

public:
    PrimitiveType() noexcept : Type(kind) {}
    std::string repr() const final { return "float"; }
    bool contains(const Type& other) const final {
        return other.kind_ == Kind::KIND_FLOAT or other.kind_ == Kind::KIND_INTEGER;
    }
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_FUNCTION;

public:
    std::vector<Type*> parameters_;
    Type* spread_;
    Type* return_type_;
    FunctionType(std::vector<Type*>&& parameters, Type* spread, Type* return_type) noexcept
        : Type(kind),
          parameters_(std::move(parameters)),
          spread_(spread),
          return_type_(return_type) {}
    std::string repr() const final { return "function"; }
    bool contains(const Type& other) const final { return other.kind_ == kind; }
};

using NullType = PrimitiveType<Kind::KIND_NULL>;
using IntegerType = PrimitiveType<Kind::KIND_INTEGER>;
using FloatType = PrimitiveType<Kind::KIND_FLOAT>;
using StringType = PrimitiveType<Kind::KIND_STRING>;
using BooleanType = PrimitiveType<Kind::KIND_BOOLEAN>;

class ListType final : public Type {
public:
    Type* element_type_;
    ListType(Type* element_type) noexcept;
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

class RecordType : public Type {
public:
    const std::map<std::string, Type*> fields_;
    RecordType(std::map<std::string, Type*> fields) noexcept;
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

class ClassType : public Type {
public:
    const std::string_view name_;
    const std::vector<InterfaceType*>& interfaces_;
    const ClassType* extends_;
    const std::map<std::string_view, Type*> properties_;
    ClassType(
        std::string_view name,
        std::vector<InterfaceType*> interfaces,
        const ClassType* extends,
        std::map<std::string_view, Type*> properties
    ) noexcept;
    std::string repr() const override;
    bool contains(const Type& other) const override;
};

class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_INTERSECTION;

public:
    std::vector<Type*> types_;
    IntersectionType(auto... args) noexcept : Type(kind) { (combine(args), ...); }
    std::string repr() const final;
    bool contains(const Type& other) const final;
    IntersectionType& combine(Type* other);
};

class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_UNION;

public:
    std::vector<Type*> types_;
    UnionType(auto... args) noexcept : Type(kind) { (combine(args), ...); }
    std::string repr() const final;
    bool contains(const Type& other) const final;
    UnionType& combine(Type* other);
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
    const std::int64_t value_;
    IntegerValue(std::int64_t value) noexcept;
    std::string repr() const final;
    IntegerValue* operator+(const IntegerValue& other) const;
    IntegerValue* operator-(const IntegerValue& other) const;
    IntegerValue* operator-() const;
    IntegerValue* operator*(const IntegerValue& other) const;
    IntegerValue* operator/(const IntegerValue& other) const;
    IntegerValue* operator%(const IntegerValue& other) const;
    BooleanValue* operator==(const IntegerValue& other) const;
    BooleanValue* operator!=(const IntegerValue& other) const;
    BooleanValue* operator<(const IntegerValue& other) const;
    BooleanValue* operator<=(const IntegerValue& other) const;
    BooleanValue* operator>(const IntegerValue& other) const;
    BooleanValue* operator>=(const IntegerValue& other) const;
    IntegerValue* operator&(const IntegerValue& other) const;
    IntegerValue* operator|(const IntegerValue& other) const;
    IntegerValue* operator^(const IntegerValue& other) const;
    IntegerValue* operator~() const;
    IntegerValue* operator<<(const IntegerValue& other) const;
    IntegerValue* operator>>(const IntegerValue& other) const;
    IntegerValue* operator=(const FloatValue& other) const;
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_FLOAT;
    using Type = FloatType;

public:
    const double value_;
    FloatValue(double value) noexcept;
    std::string repr() const final;
    FloatValue* operator+(const FloatValue& other) const;
    FloatValue* operator-(const FloatValue& other) const;
    FloatValue* operator-() const;
    FloatValue* operator*(const FloatValue& other) const;
    FloatValue* operator/(const FloatValue& other) const;
    FloatValue* operator%(const FloatValue& other) const;
    BooleanValue* operator==(const FloatValue& other) const;
    BooleanValue* operator!=(const FloatValue& other) const;
    BooleanValue* operator<(const FloatValue& other) const;
    BooleanValue* operator<=(const FloatValue& other) const;
    BooleanValue* operator>(const FloatValue& other) const;
    BooleanValue* operator>=(const FloatValue& other) const;
    FloatValue* operator=(const IntegerValue& other) const;
};

class StringValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_STRING;
    using Type = StringType;

public:
    const std::string value_;
    StringValue(std::string value) noexcept;
    std::string repr() const final;
    StringValue* operator+(const StringValue& other) const;
    StringValue* operator*(const IntegerValue& other) const;
    BooleanValue* operator==(const StringValue& other) const;
    BooleanValue* operator!=(const StringValue& other) const;
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_BOOLEAN;
    using Type = BooleanType;

public:
    const bool value_;
    BooleanValue(bool value) noexcept;
    std::string repr() const final;
    BooleanValue* operator==(const BooleanValue& other) const;
    BooleanValue* operator!=(const BooleanValue& other) const;
    BooleanValue* operator and(const BooleanValue& other) const;
    BooleanValue* operator or(const BooleanValue& other) const;
    BooleanValue* operator not() const;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_FUNCTION;
    using Type = FunctionType;

public:
    const std::function<Value*(const std::vector<Value*>&)> callback_;
    Type* function_type_;
    FunctionValue(auto&& callback, Type* function_type) noexcept
        : Value(Kind::KIND_FUNCTION),
          callback_(std::forward<decltype(callback)>(callback)),
          function_type_(function_type) {}
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
    friend class TypeFactory;

public:
    template <ValueClass V>
    static ValueRef from_literal(std::string_view literal) {
        return ValueRef(Value::from_literal<V>(literal));
    }

private:
    template <TypeClass T, typename... Args>
    static TypeRef create(Args&&... args) {
        return TypeRef(new T(std::forward<decltype(args)>(args)...));
    }

private:
    Entity* ptr_;

public:
    EntityRef() noexcept = default;
    EntityRef(const EntityRef& other) noexcept;
    EntityRef(EntityRef&& other) noexcept;
    ~EntityRef() noexcept;
    EntityRef& operator=(const EntityRef& other) noexcept;
    EntityRef& operator=(EntityRef&& other) noexcept;
    operator bool() const noexcept;
    Entity* get() const noexcept;
    Entity& operator*() const noexcept;
    Entity* operator->() const noexcept;
    Value* value() const noexcept;
    Type* type() const noexcept;

private:
    EntityRef(Entity* ptr) noexcept;
    void retain() noexcept;
    void release() noexcept;
};

class TypeFactory {
private:
    using Primitives = std::tuple<AnyType, IntegerType, FloatType, StringType, BooleanType>;
    AnyType any_type_instance_;
    IntegerType integer_type_instance_;
    FloatType float_type_instance_;
    StringType string_type_instance_;
    BooleanType boolean_type_instance_;

public:
    TypeRef of(ValueRef value) {
        assert(value && !value->is_type());
        switch (value->kind_) {
        case Kind::KIND_NULL:
            return &any_type_instance_;
        case Kind::KIND_INTEGER:
            return &integer_type_instance_;
        case Kind::KIND_FLOAT:
            return &float_type_instance_;
        case Kind::KIND_STRING:
            return &string_type_instance_;
        case Kind::KIND_BOOLEAN:
            return &boolean_type_instance_;
            // TODO: handle other kinds
        }
    }

    template <typename T, typename... Args>
    TypeRef make(Args&&... args) {
        if constexpr (TypeInTupleV<T, Primitives>) {
            static T instance;
            return &instance;
        } else {
            // return SharedRef::create<T>(std::forward<decltype(args)>(args)...);
            return {};
        }
    }

    TypeRef make_kind(Kind kind) {
        switch (kind) {
        case Kind::KIND_ANY:
            return &any_type_instance_;
        case Kind::KIND_INTEGER:
            return &integer_type_instance_;
        case Kind::KIND_FLOAT:
            return &float_type_instance_;
        case Kind::KIND_STRING:
            return &string_type_instance_;
        case Kind::KIND_BOOLEAN:
            return &boolean_type_instance_;
        default:
            assert(false && "Unknown primitive kind");
            std::unreachable();
        }
    }
};
