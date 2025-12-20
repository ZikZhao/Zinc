#pragma once
#include "pch.hpp"

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

template <typename TargetType>
class Reference {
private:
    std::shared_ptr<TargetType> ptr_;
    Reference* source_;

public:
    Reference() = default;
    Reference(const Reference& other) = default;
    Reference(Reference&& other) = default;
    Reference(TargetType* ptr) : ptr_(ptr), source_(nullptr) {}
    Reference(std::shared_ptr<TargetType>&& other) : ptr_(std::move(other)), source_(nullptr) {}
    // Copy constructor that keeps track of source for assignment propagation
    Reference(Reference& other) : ptr_(other.ptr_), source_(&other) {}

public:
    // Assignment operator with adaptation
    Reference& operator=(const Reference& other) {
        if (ptr_ != nullptr and (ptr_->kind_ != other.ptr_->kind_)) {
            const auto result =
                ptr_->eval_operation(GetOperatorString<OperatorFunctors::Assign>(), *other.ptr_);
            ptr_ = std::move(result.ptr_);
        } else {
            ptr_ = std::move(other.ptr_);
        }
        if (source_) {
            *source_ = *this;
            source_ = nullptr;
        }
        return *this;
    }
    TargetType* operator->() const noexcept { return ptr_.get(); }
    TargetType& operator*() const { return *ptr_; }
    std::string repr() const { return ptr_->repr(); }
    template <typename Operator>
    Reference eval_operation() const {
        return ptr_->eval_operation(GetOperatorString<Operator>());
    }
    template <typename Operator>
    Reference eval_operation(Reference& other) const {
        return ptr_->eval_operation(GetOperatorString<Operator>(), *other);
    }
    bool is_truthy() const { return ptr_->is_truthy(); }
    bool contains(const Reference& other) const { return ptr_->contains(*other); }
    Reference operator()(auto&&... args) {
        return ptr_->operator()(std::forward<decltype(args)>(args)...);
    }
    bool null() const noexcept { return ptr_ == nullptr; }
};

class TypeOrValue;
using ObjRef = Reference<TypeOrValue>;

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

using TypeRef = ObjRef;           // Should always point to a Type
using ValueRef = ObjRef;          // Should always point to a Value
using InterfaceTypeRef = ObjRef;  // Should always point to an InterfaceType
using ClassTypeRef = ObjRef;      // Should always point to a ClassType
using DictValueRef = ObjRef;      // Should always point to a DictValue

using Arguments = std::vector<ValueRef>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;

class TypeOrValue {
public:
    const Kind kind_;
    TypeOrValue(Kind kind);
    virtual ~TypeOrValue() = default;
    virtual std::string repr() const = 0;
    bool is_truthy() const;
    bool contains(const TypeOrValue& other) const;
    // Evaluations are not const because data structures may need to be modified
    ObjRef eval_operation(std::string_view op);
    ObjRef eval_operation(std::string_view op, TypeOrValue& other);
};

class Type : public TypeOrValue {
public:
    static TypeRef FromTypeIndex(const std::type_index& type);

public:
    Type(Kind kind);
    ~Type() override = default;
    virtual bool contains(const Type& other) const = 0;
};

class AnyType final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_ANY;

public:
    AnyType();
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

template <Kind K>
class PrimitiveType final : public Type {
public:
    static constexpr Kind kind = K;

public:
    PrimitiveType() : Type(kind) {}
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
    PrimitiveType() : Type(Kind::KIND_INTEGER) {}
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
    PrimitiveType() : Type(Kind::KIND_FLOAT) {}
    std::string repr() const final { return "float"; }
    bool contains(const Type& other) const final {
        return other.kind_ == Kind::KIND_FLOAT or other.kind_ == Kind::KIND_INTEGER;
    }
};

template <>
class PrimitiveType<Kind::KIND_FUNCTION> final : public Type {
public:
    static constexpr Kind kind = Kind::KIND_FUNCTION;

public:
    std::vector<TypeRef> parameters_;
    TypeRef spread_;
    TypeRef return_type_;
    PrimitiveType(std::vector<TypeRef>&& parameters, TypeRef spread, TypeRef return_type)
        : Type(Kind::KIND_FUNCTION),
          parameters_(std::move(parameters)),
          spread_(spread),
          return_type_(return_type) {}
    std::string repr() const final { return "function"; }
    bool contains(const Type& other) const final { return other.kind_ == Kind::KIND_FUNCTION; }
};

using NullType = PrimitiveType<Kind::KIND_NULL>;
using IntegerType = PrimitiveType<Kind::KIND_INTEGER>;
using FloatType = PrimitiveType<Kind::KIND_FLOAT>;
using StringType = PrimitiveType<Kind::KIND_STRING>;
using BooleanType = PrimitiveType<Kind::KIND_BOOLEAN>;
using FunctionType = PrimitiveType<Kind::KIND_FUNCTION>;

class ListType final : public Type {
public:
    TypeRef element_type_;
    ListType(TypeRef element_type);
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

class RecordType : public Type {
public:
    const std::map<std::string, TypeRef> fields_;
    RecordType(std::map<std::string, TypeRef> fields);
    std::string repr() const final;
    bool contains(const Type& other) const final;
};

class ClassType : public Type {
public:
    const std::string_view name_;
    const std::vector<InterfaceTypeRef>& interfaces_;
    const ClassTypeRef extends_;
    const Context* properties_;
    ClassType(
        std::string_view name,
        const std::vector<InterfaceTypeRef>& interfaces,
        const ClassTypeRef extends,
        const Context* properties
    );
    std::string repr() const override;
    bool contains(const Type& other) const override;
};

class IntersectionType final : public Type {
public:
    std::vector<TypeRef> types;
    IntersectionType(std::vector<TypeRef>&& types);
    bool contains(const Type& other) const final;
};

class UnionType final : public Type {
public:
    std::vector<TypeRef> types;
    UnionType(std::vector<TypeRef>&& types);
    bool contains(const Type& other) const final;
};

class Value : public TypeOrValue {
public:
    static constexpr Kind kind = Kind::KIND_ANY;
    using Type = AnyType;
    template <ValueClass V>
    static ValueRef FromLiteral(std::string_view literal) {
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
    Value(Kind kind);
    ~Value() override = default;
    virtual bool is_truthy() const = 0;
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_NULL;
    using Type = NullType;

public:
    NullValue();
    std::string repr() const final;
    bool is_truthy() const final;
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::KIND_INTEGER;
    using Type = IntegerType;

public:
    const std::int64_t value_;
    IntegerValue(std::int64_t value);
    std::string repr() const final;
    bool is_truthy() const final;
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
    FloatValue(double value);
    std::string repr() const final;
    bool is_truthy() const final;
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
    StringValue(std::string&& value);
    std::string repr() const final;
    bool is_truthy() const final;
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
    BooleanValue(bool value);
    std::string repr() const final;
    bool is_truthy() const final;
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
    const std::function<ValueRef(const Arguments&)> callback_;
    TypeRef function_type_;
    FunctionValue(auto&& callback, TypeRef function_type)
        : Value(Kind::KIND_FUNCTION),
          callback_(std::forward<decltype(callback)>(callback)),
          function_type_(function_type) {}
    std::string repr() const final;
    bool is_truthy() const final;
    ValueRef operator()(const Arguments& args) const;
};

class ObjectValue : public Value {
public:
    static constexpr Kind kind = Kind::KIND_OBJECT;

public:
    ClassTypeRef class_type_;
    std::vector<ValueRef> attributes_;
    ObjectValue(ClassTypeRef cls);
    ValueRef get(const std::string_view property) const;
};

class ListValue : public ObjectValue {
private:
    static TypeRef ListClassInstance;
    static ValueRef Append(const std::vector<ValueRef>& args);

public:
    std::vector<ValueRef> values_;
    ListValue();
    ListValue(std::vector<ValueRef>&& values);
    std::string repr() const final;
    bool is_truthy() const final;
    ListValue* operator+(const ListValue& other) const;
    ListValue* operator*(const IntegerValue& other) const;
    ValueRef operator[](const Slice& indices) const;
};
