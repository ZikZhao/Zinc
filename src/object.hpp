#pragma once
#include "pch.hpp"
#include <compare>
#include <limits>

#include "diagnosis.hpp"

class Scope;

enum class Kind : std::uint16_t {
    NothingOrUnknown,  // for operation table it means no right operand, for types it means unknown
                       // type
    Any,
    Null,
    Integer,
    Float,
    String,
    Boolean,
    NonCompositeSize,
    Function,
    List,
    Record,
    Interface,
    Instance,
    Intersection,
    Union,
};

class Object;
class Type;   // interface
class Value;  // interface

template <Kind Kind>
class PrimitiveType;
using AnyType = PrimitiveType<Kind::Any>;
using NullType = PrimitiveType<Kind::Null>;
using IntegerType = PrimitiveType<Kind::Integer>;
using FloatType = PrimitiveType<Kind::Float>;
using StringType = PrimitiveType<Kind::String>;
using BooleanType = PrimitiveType<Kind::Boolean>;
class InterfaceType;
class StructType;
class ClassType;
class IntersectionType;
class UnionType;

class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;
class FunctionValue;
class InterfaceValue;
class ClassValue;
class InstanceValue;
class ListValue;
class DictValue;
class SetValue;

class UnknownType;

template <typename T>
concept TypeClass = std::derived_from<T, Type>;
template <typename V>
concept ValueClass = std::derived_from<V, Value> && !std::derived_from<V, Type>;

// using Arguments = std::vector<ValueRef>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;

template <typename U>
class Reference;
using ObjectRef = Reference<Object>;
using TypeRef = Reference<Type>;
using ValueRef = Reference<Value>;

class Object {
    template <typename>
    friend class Reference;

private:
    mutable std::int32_t ref_count_;

public:
    Kind kind_;

public:
    Object(Kind kind, bool is_value) noexcept
        : ref_count_(is_value ? 0 : std::numeric_limits<decltype(ref_count_)>::min()),
          kind_(kind) {}

    virtual ~Object() = default;

    virtual std::string repr() const = 0;

    constexpr std::strong_ordering operator<=>(const Object& other) const noexcept {
        return this <=> &other;
    }

    constexpr bool operator==(const Object& other) const noexcept { return this == &other; }

private:
    bool is_type() const noexcept { return ref_count_ < 0; }
    bool is_value() const noexcept { return ref_count_ >= 0; }
};

class Type : public Object {
public:
    Type(Kind kind) noexcept : Object(kind, false) {}

    constexpr std::strong_ordering operator<=>(const Type& other) const noexcept {
        return this <=> &other;
    }

    constexpr bool operator==(const Type& other) const noexcept { return this == &other; }

    bool assignable_from(const Type& source) const {
        assert(
            this == &source ? assignable_from_impl(source) : true
        );  // assignable_from_impl(source) must be true for identical types
        return this == &source || assignable_from_impl(source);
    }

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
        if constexpr (K == Kind::Any) {
            return "any";
        } else if constexpr (K == Kind::Null) {
            return "null";
        } else if constexpr (K == Kind::Integer) {
            return "integer";
        } else if constexpr (K == Kind::Float) {
            return "float";
        } else if constexpr (K == Kind::String) {
            return "string";
        } else if constexpr (K == Kind::Boolean) {
            return "boolean";
        } else if constexpr (K == Kind::Function) {
            return "function";
        } else {
            static_assert(false);
        }
    }

    bool assignable_from_impl(const Type& source) const final {
        if constexpr (K == Kind::Any) {
            return true;
        } else {
            return source.kind_ == kind;
        }
    }
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    Type* return_type_;
    ComparableSpan<Type*> parameters_;
    Type* spread_;

public:
    FunctionType(Type* return_type, ComparableSpan<Type*> parameters, Type* spread) noexcept
        : Type(kind), return_type_(return_type), parameters_(parameters), spread_(spread) {}

    std::string repr() const final {
        std::string result = "function("s;
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            result += parameters_[i]->repr();
            if (i + 1 < parameters_.size()) {
                result += ", "s;
            }
        }
        if (spread_) {
            if (!parameters_.empty()) {
                result += ", "s;
            }
            result += "..."s + spread_->repr();
        }
        result += ") => "s + return_type_->repr();
        return result;
    }

    bool assignable_from_impl(const Type& source) const final {
        // (Base) => Derived is assignable to (Derived) => Base
        // i.e., parameters are contravariant, return type is covariant
        if (source.kind_ != Kind::Function) {
            return false;
        }
        const FunctionType& func_other = static_cast<const FunctionType&>(source);
        if (parameters_.size() != func_other.parameters_.size()) {
            return false;
        }
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            if (!func_other.parameters_[i]->assignable_from(*parameters_[i])) {
                return false;
            }
        }
        if ((spread_ == nullptr) != (func_other.spread_ == nullptr)) {
            return false;
        } else if (spread_ && func_other.spread_) {
            if (!func_other.spread_->assignable_from(*spread_)) {
                return false;
            }
        }
        return return_type_->assignable_from(*func_other.return_type_);
    }
    std::strong_ordering operator<=>(const FunctionType& other) const noexcept = default;
};

class ListType final : public Type {
public:
    static constexpr Kind kind = Kind::List;

public:
    Type* element_type_;

public:
    ListType(Type* element_type) noexcept : Type(kind), element_type_(element_type) {}

    std::string repr() const final { return "List<"s + element_type_->repr() + ">"s; }

    bool assignable_from_impl(const Type& source) const final {
        if (source.kind_ != kind) {
            return false;
        }
        const ListType& other_list = static_cast<const ListType&>(source);
        return element_type_->assignable_from(*other_list.element_type_);
    }
};

class RecordType : public Type {
public:
    static constexpr Kind kind = Kind::Record;

private:
    FlatMap<std::string_view, Type*> fields_;

public:
    RecordType(FlatMap<std::string_view, Type*> fields) noexcept
        : Type(kind), fields_(std::move(fields)) {}

    std::string repr() const final {
        // TODO
        return {};
    }

    bool assignable_from_impl(const Type& source) const final {
        // (a,b,c) is assignable to (a,b)
        // i.e., source must have at least all fields of this
        if (source.kind_ != kind) {
            return false;
        }
        const RecordType& other_record = static_cast<const RecordType&>(source);
        for (const auto& [name, type] : fields_) {
            auto it = other_record.fields_.find(name);
            if (it == other_record.fields_.end() || !(*it).second->assignable_from(*type)) {
                return false;
            }
        }
        return true;
    }
    std::strong_ordering operator<=>(const RecordType& other) const noexcept = default;
};

class ClassType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

private:
    std::string_view name_;
    ComparableSpan<InterfaceType*> interfaces_;
    const ClassType* extends_;
    FlatMap<std::string_view, Type*> properties_;

public:
    ClassType(
        std::string_view name,
        ComparableSpan<InterfaceType*> interfaces,
        const ClassType* extends,
        FlatMap<std::string_view, Type*> properties
    ) noexcept
        : Type(kind),
          name_(name),
          interfaces_(interfaces),
          extends_(extends),
          properties_(std::move(properties)) {}

    std::string repr() const override { return "class "s + std::string(name_); }

    bool assignable_from_impl(const Type& other) const override { return false; }
};

class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::Intersection;

private:
    static ComparableSpan<Type*> combine(Type* left, Type* right) {
        std::size_t size = 0;
        if (right < left) std::swap(left, right);
        if (left->kind_ == Kind::Intersection) {
            const IntersectionType& left_intersection = static_cast<const IntersectionType&>(*left);
            size += left_intersection.types_.size();
        } else {
            assert(left->kind_ == Kind::Function);
            size++;
        }
        if (right->kind_ == Kind::Intersection) {
            const IntersectionType& right_intersection =
                static_cast<const IntersectionType&>(*right);
            size += right_intersection.types_.size();
        } else {
            assert(right->kind_ == Kind::Function);
            size++;
        }
        ComparableSpan<Type*> buffer = GlobalMemory::alloc_array<Type*>(size);
        std::size_t index = 0;
        if (left->kind_ == Kind::Intersection) {
            const IntersectionType& left_intersection = static_cast<const IntersectionType&>(*left);
            for (const auto& type : left_intersection.types_) {
                buffer[index++] = type;
            }
        } else {
            buffer[index++] = left;
        }
        if (right->kind_ == Kind::Intersection) {
            const IntersectionType& right_intersection =
                static_cast<const IntersectionType&>(*right);
            for (const auto& type : right_intersection.types_) {
                buffer[index++] = type;
            }
        } else {
            buffer[index++] = right;
        }
        return buffer;
    }

private:
    ComparableSpan<Type*> types_;

public:
    IntersectionType(Type* left, Type* right) noexcept : Type(kind), types_{combine(left, right)} {}

    std::string repr() const final {
        // TODO
        return {};
    }

    bool assignable_from_impl(const Type& source) const final {
        // (a & b & c) is assignable to (a & b)
        // i.e., source supports at least all the function overloads of this
        if (source.kind_ == Kind::Intersection) {
            const IntersectionType& other_intersection =
                static_cast<const IntersectionType&>(source);
            for (const auto& type : types_) {
                bool found = false;
                for (const auto& other_type : other_intersection.types_) {
                    if (type->assignable_from(*other_type)) {
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

    std::strong_ordering operator<=>(const IntersectionType& other) const noexcept = default;
};

class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::Union;

private:
    static ComparableSpan<Type*> combine(Type* left, Type* right) {
        std::size_t size = 0;
        if (right < left) std::swap(left, right);
        if (left->kind_ == Kind::Union) {
            const UnionType& left_union = static_cast<const UnionType&>(*left);
            size += left_union.types_.size();
        } else {
            size++;
        }
        if (right->kind_ == Kind::Union) {
            const UnionType& right_union = static_cast<const UnionType&>(*right);
            size += right_union.types_.size();
        } else {
            size++;
        }
        ComparableSpan<Type*> buffer = GlobalMemory::alloc_array<Type*>(size);
        std::size_t index = 0;
        if (left->kind_ == Kind::Union) {
            const UnionType& left_union = static_cast<const UnionType&>(*left);
            for (const auto& type : left_union.types_) {
                buffer[index++] = type;
            }
        } else {
            buffer[index++] = left;
        }
        if (right->kind_ == Kind::Union) {
            const UnionType& right_union = static_cast<const UnionType&>(*right);
            for (const auto& type : right_union.types_) {
                buffer[index++] = type;
            }
        } else {
            buffer[index++] = right;
        }
        return buffer;
    }

private:
    ComparableSpan<Type*> types_;

public:
    UnionType(Type* left, Type* right) noexcept : Type(kind), types_(combine(left, right)) {}

    std::string repr() const final {
        // TODO
        return {};
    }

    bool assignable_from_impl(const Type& source) const final {
        // (a | b) is assignable to (a | b | c)
        // i.e., source must be assignable to at least one of the types in this
        if (source.kind_ == Kind::Union) {
            const UnionType& other_union = static_cast<const UnionType&>(source);
            for (const auto& type : other_union.types_) {
                bool found = false;
                for (const auto& other_type : types_) {
                    if (other_type->assignable_from(*type)) {
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
            assert(source.kind_ == Kind::Function);
            for (const auto& type : types_) {
                if (type->assignable_from(source)) {
                    return true;
                }
            }
            return false;
        }
    }

    std::strong_ordering operator<=>(const UnionType& other) const noexcept = default;
};

class UnknownType final : public Type {
public:
    static constexpr Kind kind = Kind::NothingOrUnknown;
    UnknownType() noexcept : Type(kind) {}
    std::string repr() const final { return "unknown"; }
    bool assignable_from_impl(const Type& source) const final { return true; }
};

class Value : public Object {
    friend ValueRef;

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
    Value(Kind kind) noexcept : Object(kind, true) {}
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::Null;
    using Type = NullType;

public:
    NullValue() noexcept : Value(kind) {}

    std::string repr() const final { return "null"; }
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;
    using Type = IntegerType;

public:
    std::int64_t value_;

public:
    IntegerValue(std::int64_t value) noexcept : Value(kind), value_(value) {}
    std::string repr() const final { return std::to_string(value_); }
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
    static constexpr Kind kind = Kind::Float;
    using Type = FloatType;

public:
    double value_;

public:
    FloatValue(double value) noexcept : Value(kind), value_(value) {}
    std::string repr() const final { return std::to_string(value_); }

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
    static constexpr Kind kind = Kind::String;
    using Type = StringType;

public:
    std::string value_;

public:
    StringValue(std::string value) noexcept : Value(kind), value_(std::move(value)) {}
    std::string repr() const final { return "\"" + this->value_ + "\""; }

    StringValue operator+(const StringValue& other) const;
    StringValue operator*(const IntegerValue& other) const;
    BooleanValue operator==(const StringValue& other) const;
    BooleanValue operator!=(const StringValue& other) const;
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::Boolean;
    using Type = BooleanType;

public:
    bool value_;

public:
    BooleanValue(bool value) noexcept : Value(kind), value_(value) {}
    std::string repr() const final { return this->value_ ? "true" : "false"; }

    BooleanValue operator==(const BooleanValue& other) const;
    BooleanValue operator!=(const BooleanValue& other) const;
    BooleanValue operator&&(const BooleanValue& other) const;
    BooleanValue operator||(const BooleanValue& other) const;
    BooleanValue operator!() const;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::Function;
    using Type = FunctionType;

public:
    const std::function<Value*(const std::vector<Value*>&)> callback_;
    Type* type_;

public:
    FunctionValue(auto&& callback, Type* function_type) noexcept
        : Value(kind),
          callback_(std::forward<decltype(callback)>(callback)),
          type_(function_type) {}

    std::string repr() const final {
        return std::format("<function at {:p}>", static_cast<const void*>(this));
    }
    Value* operator()(const std::vector<Value*>& args) const;
};

class InstanceValue : public Value {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    ClassType* cls_;
    std::vector<Value*> attributes_;

public:
    InstanceValue(ClassType* cls) noexcept : Value(kind), cls_(cls) {}

    Value* get(const std::string_view property) const noexcept {
        // TODO: implement
        return nullptr;
    }
};

class ListValue : public InstanceValue {
private:
    static ListValue* Append(const std::vector<Value*>& args) noexcept {
        // TODO: implement
        return nullptr;
    }

public:
    std::vector<Value*> values_;
    ListValue() noexcept : InstanceValue(nullptr) {}
    ListValue(std::vector<Value*>&& values) noexcept
        : InstanceValue(nullptr), values_(std::move(values)) {
        kind_ = kind;
    }
    std::string repr() const final {
        // TODO: implement
        return {};
    }
    ListValue* operator+(const ListValue& other) const;
    ListValue* operator*(const IntegerValue& other) const;
    Value* operator[](const Slice& indices) const;
};

class UnknownValue final : public Value {
public:
    static constexpr Kind kind = Kind::NothingOrUnknown;
    UnknownValue() noexcept : Value(kind) {}
    std::string repr() const final { return "unknown"; }
};

template <typename T>
class Reference {
    template <typename>
    friend class Reference;
    friend class IntrinsicOpTable;
    friend class TypeRegistry;

public:
    template <typename V, typename = std::enable_if_t<std::is_base_of_v<Value, V>>>
    static Reference make_literal(std::string_view literal) {
        return Reference(Value::from_literal<V>(literal));
    }

private:
    template <
        TypeClass U,
        typename... Args,
        typename = std::enable_if_t<std::is_base_of_v<Type, U>>>
    static Reference make(Args&&... args) {
        return Reference(GlobalMemory::alloc<U>(std::forward<decltype(args)>(args)...));
    }

private:
    T* ptr_;

public:
    Reference() noexcept = default;
    Reference(const Reference& other) noexcept : ptr_(other.ptr_) { retain(); }
    template <typename U>
        requires std::is_base_of_v<T, U>
    Reference(const Reference<U>& other) noexcept : ptr_(other.ptr_) {
        retain();
    }
    Reference(Reference&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
    template <typename U>
        requires std::is_base_of_v<T, U>
    Reference(Reference<U>&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }
    ~Reference() noexcept { release(); }
    Reference& operator=(Reference other) noexcept {
        std::swap(ptr_, other.ptr_);
        return *this;
    }
    operator bool() const noexcept { return ptr_ != nullptr; }
    T& operator*() const noexcept { return *ptr_; }
    T* operator->() const noexcept { return ptr_; }

    Reference<Type> as_type() const noexcept {
        static_assert(std::is_same_v<T, Object>, "as_type can only be called on Reference<Object>");
        return Reference<Type>((ptr_ && ptr_->is_type()) ? static_cast<Type*>(ptr_) : nullptr);
    }

    Reference<Value> as_value() const noexcept {
        static_assert(
            std::is_same_v<T, Object>, "as_value can only be called on Reference<Object>"
        );
        if (ptr_ && ptr_->is_value()) {
            return Reference<Value>(static_cast<Value*>(ptr_));
        } else if (ptr_ && ptr_->kind_ == Kind::NothingOrUnknown) {
            return Reference<Value>(new UnknownValue());
        } else {
            return Reference<Value>(nullptr);
        }
    }

    template <typename U = T, typename = std::enable_if_t<std::is_same_v<U, Type>>>
    operator Type*() const noexcept {
        return ptr_;
    }

    template <typename U = T, typename = std::enable_if_t<std::is_same_v<U, Value>>>
    operator Value*() const noexcept {
        return ptr_;
    }

private:
    explicit Reference(T* ptr) noexcept : ptr_(ptr) { retain(); }

    void retain() noexcept {
        if (ptr_) {
            ptr_->ref_count_++;
        }
    }

    void release() noexcept {
        if (ptr_) {
            ptr_->ref_count_--;
            if (ptr_->ref_count_ == 0) {
                delete ptr_;
            }
        }
    }
};

class TypeRegistry {
private:
    using Primitives =
        std::tuple<AnyType, NullType, IntegerType, FloatType, StringType, BooleanType>;
    UnknownType unknown_type_instance_;
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
        assert(value);
        switch (value->kind_) {
        case Kind::NothingOrUnknown:
            return TypeRef(&unknown_type_instance_);
        case Kind::Null:
            return TypeRef(&any_type_instance_);
        case Kind::Integer:
            return TypeRef(&integer_type_instance_);
        case Kind::Float:
            return TypeRef(&float_type_instance_);
        case Kind::String:
            return TypeRef(&string_type_instance_);
        case Kind::Boolean:
            return TypeRef(&boolean_type_instance_);
        case Kind::Function:
            return TypeRef(static_cast<FunctionValue&>(*value).type_);
        case Kind::Instance:
            return TypeRef(static_cast<InstanceValue&>(*value).cls_);
        default:
            assert(false && "Unknown value kind for TypeRegistry::of");
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
            return TypeRef::make<T>(std::forward<decltype(args)>(args)...);
        } else {
            static_assert(false, "Unknown type for TypeRegistry::get");
            // TODO: handle other types
        }
    }

    TypeRef get_kind(Kind kind) {
        switch (kind) {
        case Kind::Any:
            return TypeRef(&any_type_instance_);
        case Kind::Integer:
            return TypeRef(&integer_type_instance_);
        case Kind::Float:
            return TypeRef(&float_type_instance_);
        case Kind::String:
            return TypeRef(&string_type_instance_);
        case Kind::Boolean:
            return TypeRef(&boolean_type_instance_);
        default:
            assert(false && "Unknown primitive kind");
            std::unreachable();
        }
    }

    TypeRef get_unknown() { return TypeRef(&unknown_type_instance_); }

private:
    template <typename T>
    TypeRef get_cached(FlatSet<T>& instances, auto&&... args) {
        T type(std::forward<decltype(args)>(args)...);
        T& result = instances.try_emplace(type);
        return TypeRef(&result);
    }
};

// IntegerValue operators

inline IntegerValue IntegerValue::operator+(const IntegerValue& other) const {
    return IntegerValue(this->value_ + other.value_);
}

inline IntegerValue IntegerValue::operator-(const IntegerValue& other) const {
    return IntegerValue(this->value_ - other.value_);
}

inline IntegerValue IntegerValue::operator-() const { return IntegerValue(-this->value_); }

inline IntegerValue IntegerValue::operator*(const IntegerValue& other) const {
    return IntegerValue(this->value_ * other.value_);
}

inline IntegerValue IntegerValue::operator/(const IntegerValue& other) const {
    if (other.value_ == 0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return IntegerValue(this->value_ / other.value_);
}

inline IntegerValue IntegerValue::operator%(const IntegerValue& other) const {
    if (other.value_ == 0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return IntegerValue(this->value_ % other.value_);
}

inline BooleanValue IntegerValue::operator==(const IntegerValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}

inline BooleanValue IntegerValue::operator!=(const IntegerValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}

inline BooleanValue IntegerValue::operator<(const IntegerValue& other) const {
    return BooleanValue(this->value_ < other.value_);
}

inline BooleanValue IntegerValue::operator<=(const IntegerValue& other) const {
    return BooleanValue(this->value_ <= other.value_);
}

inline BooleanValue IntegerValue::operator>(const IntegerValue& other) const {
    return BooleanValue(this->value_ > other.value_);
}

inline BooleanValue IntegerValue::operator>=(const IntegerValue& other) const {
    return BooleanValue(this->value_ >= other.value_);
}

inline IntegerValue IntegerValue::operator&(const IntegerValue& other) const {
    return IntegerValue(this->value_ & other.value_);
}

inline IntegerValue IntegerValue::operator|(const IntegerValue& other) const {
    return IntegerValue(this->value_ | other.value_);
}

inline IntegerValue IntegerValue::operator^(const IntegerValue& other) const {
    return IntegerValue(this->value_ ^ other.value_);
}

inline IntegerValue IntegerValue::operator~() const { return IntegerValue(~this->value_); }

inline IntegerValue IntegerValue::operator<<(const IntegerValue& other) const {
    if (other.value_ < 0) throw UnlocatedProblem::make<ShiftByNegativeError>();
    return IntegerValue(this->value_ << other.value_);
}

inline IntegerValue IntegerValue::operator>>(const IntegerValue& other) const {
    if (other.value_ < 0) throw UnlocatedProblem::make<ShiftByNegativeError>();
    return IntegerValue(this->value_ >> other.value_);
}

// FloatValue operators
inline FloatValue FloatValue::operator+(const FloatValue& other) const {
    return FloatValue(this->value_ + other.value_);
}

inline FloatValue FloatValue::operator-(const FloatValue& other) const {
    return FloatValue(this->value_ - other.value_);
}

inline FloatValue FloatValue::operator-() const { return FloatValue(-this->value_); }

inline FloatValue FloatValue::operator*(const FloatValue& other) const {
    return FloatValue(this->value_ * other.value_);
}

inline FloatValue FloatValue::operator/(const FloatValue& other) const {
    if (other.value_ == 0.0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return FloatValue(this->value_ / other.value_);
}

inline FloatValue FloatValue::operator%(const FloatValue& other) const {
    if (other.value_ == 0.0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return FloatValue(std::fmod(this->value_, other.value_));
}

inline BooleanValue FloatValue::operator==(const FloatValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}

inline BooleanValue FloatValue::operator!=(const FloatValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}

inline BooleanValue FloatValue::operator<(const FloatValue& other) const {
    return BooleanValue(this->value_ < other.value_);
}

inline BooleanValue FloatValue::operator<=(const FloatValue& other) const {
    return BooleanValue(this->value_ <= other.value_);
}

inline BooleanValue FloatValue::operator>(const FloatValue& other) const {
    return BooleanValue(this->value_ > other.value_);
}

inline BooleanValue FloatValue::operator>=(const FloatValue& other) const {
    return BooleanValue(this->value_ >= other.value_);
}

// StringValue operators
inline StringValue StringValue::operator+(const StringValue& other) const {
    return StringValue(this->value_ + other.value_);
}

inline StringValue StringValue::operator*(const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value_.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        result += this->value_;
    }
    return StringValue(std::move(result));
}

inline BooleanValue StringValue::operator==(const StringValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}

inline BooleanValue StringValue::operator!=(const StringValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}

// BooleanValue operators
inline BooleanValue BooleanValue::operator==(const BooleanValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}

inline BooleanValue BooleanValue::operator!=(const BooleanValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}

inline BooleanValue BooleanValue::operator&&(const BooleanValue& other) const {
    return BooleanValue(this->value_ && other.value_);
}

inline BooleanValue BooleanValue::operator||(const BooleanValue& other) const {
    return BooleanValue(this->value_ || other.value_);
}

inline BooleanValue BooleanValue::operator!() const { return BooleanValue(!this->value_); }

// FunctionValue operators
inline Value* FunctionValue::operator()(const std::vector<Value*>& args) const {
    return callback_(args);
}

// ListValue operators
inline ListValue* ListValue::operator+(const ListValue& other) const {
    // TODO: implement
    return nullptr;
}

inline ListValue* ListValue::operator*(const IntegerValue& other) const {
    // TODO: implement
    return nullptr;
}

inline Value* ListValue::operator[](const Slice& indices) const {
    // TODO: implement
    return nullptr;
}
