#pragma once
#include "pch.hpp"
#include <ranges>

#include "diagnosis.hpp"

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
class Type;
class Value;

using ObjectPtr = const Object*;
using TypePtr = const Type*;
using ValuePtr = const Value*;

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

// using Arguments = std::vector<Value*>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;

class Object : public MemoryManaged {
    template <typename>
    friend class Reference;

public:
    Kind kind_;

private:
    bool is_type_;

public:
    Object(Kind kind, bool is_type) noexcept : kind_(kind), is_type_(is_type) {}

    virtual ~Object() = default;

    virtual GlobalMemory::String repr() const = 0;

    constexpr std::strong_ordering operator<=>(const Object& other) const noexcept {
        return this <=> &other;
    }

    constexpr bool operator==(const Object& other) const noexcept { return this == &other; }

    /// IMPORTANT: an object may be both a type and a value (i.e. UnknownType and UnknownValue)
    /// this is because we cannot have diamond inheritance with multiple derived classes from Object
    /// but by semantics a unknown object is both a type and a value
    TypePtr as_type() const;
    ValuePtr as_value() const;
};

class Type : public Object {
protected:
    Type(Kind kind) noexcept : Object(kind, true) {}

public:
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

class UnknownType final : public Type {
    friend class Object;
    friend class TypeRegistry;

public:
    static constexpr Kind kind = Kind::NothingOrUnknown;
    static const UnknownType instance;

private:
    UnknownType() noexcept : Type(kind) {}

public:
    GlobalMemory::String repr() const final { return "unknown"; }
    bool assignable_from_impl(const Type& source) const final { return true; }
};

template <Kind K>
class PrimitiveType final : public Type {
public:
    static constexpr Kind kind = K;
    static const PrimitiveType instance;

public:
    PrimitiveType() noexcept : Type(kind) {}

    GlobalMemory::String repr() const final {
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
    TypePtr return_type_;
    ComparableSpan<TypePtr> parameters_;
    TypePtr spread_;

public:
    FunctionType(TypePtr return_type, ComparableSpan<TypePtr> parameters, TypePtr spread) noexcept
        : Type(kind), return_type_(return_type), parameters_(parameters), spread_(spread) {}

    GlobalMemory::String repr() const final {
        GlobalMemory::String params_repr =
            parameters_ | std::views::transform([](TypePtr type) { return type->repr(); }) |
            std::views::join_with(", "sv) | GlobalMemory::collect<GlobalMemory::String>();
        return GlobalMemory::format(
            "({}{}) -> {}",
            params_repr,
            spread_ ? GlobalMemory::format(", ...{}", spread_->repr()) : ""sv,
            return_type_->repr()
        );
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

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("List<{}>", element_type_->repr());
    }

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
    FlatMap<std::string_view, TypePtr> fields_;

public:
    RecordType(FlatMap<std::string_view, TypePtr> fields) noexcept
        : Type(kind), fields_(std::move(fields)) {}

    GlobalMemory::String repr() const final {
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

    GlobalMemory::String repr() const override {
        return GlobalMemory::String("class ") + GlobalMemory::String(name_);
    }

    bool assignable_from_impl(const Type& other) const override { return false; }
};

class IntersectionType final : public Type {
public:
    static constexpr Kind kind = Kind::Intersection;

private:
    static ComparableSpan<TypePtr> combine(TypePtr left, TypePtr right) {
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
        ComparableSpan<TypePtr> buffer = GlobalMemory::alloc_array<TypePtr>(size);
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
    ComparableSpan<TypePtr> types_;

public:
    IntersectionType(TypePtr left, TypePtr right) noexcept
        : Type(kind), types_{combine(left, right)} {}

    GlobalMemory::String repr() const final {
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
    static ComparableSpan<TypePtr> combine(TypePtr left, TypePtr right) {
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
        ComparableSpan<TypePtr> buffer = GlobalMemory::alloc_array<TypePtr>(size);
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
    ComparableSpan<TypePtr> types_;

public:
    UnionType(TypePtr left, TypePtr right) noexcept : Type(kind), types_(combine(left, right)) {}

    GlobalMemory::String repr() const final {
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
            if (ec != std::errc()) {
                throw UnlocatedProblem::make<InvalidLiteralError>(literal, "integer");
            }
            return new V(value);
        } else if constexpr (std::is_same_v<V, FloatValue>) {
            double value;
            auto [ptr, ec] =
                std::from_chars(literal.data(), literal.data() + literal.size(), value);
            if (ec != std::errc()) {
                throw UnlocatedProblem::make<InvalidLiteralError>(literal, "float");
            }
            return new V(value);
        } else if constexpr (std::is_same_v<V, StringValue>) {
            return new V(GlobalMemory::String(literal.substr(1, literal.size() - 2)));
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
    virtual TypePtr get_type() const = 0;
};

class UnknownValue final : public Value {
    friend class Object;

public:
    static constexpr Kind kind = Kind::NothingOrUnknown;

private:
    UnknownValue() noexcept : Value(kind) {}
    GlobalMemory::String repr() const final { return "unknown"; }
    TypePtr get_type() const final { return &UnknownType::instance; }
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::Null;
    using Type = NullType;

public:
    NullValue() noexcept : Value(kind) {}

    GlobalMemory::String repr() const final { return "null"; }
    TypePtr get_type() const final { return &Type::instance; }
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;
    using Type = IntegerType;

public:
    std::int64_t value_;

public:
    IntegerValue(std::int64_t value) noexcept : Value(kind), value_(value) {}
    GlobalMemory::String repr() const final { return GlobalMemory::format("{}", value_); }
    TypePtr get_type() const final { return &Type::instance; }
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
    GlobalMemory::String repr() const final { return GlobalMemory::format("{}", value_); }
    TypePtr get_type() const final { return &Type::instance; }
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
    GlobalMemory::String value_;

public:
    StringValue(GlobalMemory::String value) noexcept : Value(kind), value_(std::move(value)) {}
    GlobalMemory::String repr() const final { return "\"" + this->value_ + "\""; }
    TypePtr get_type() const final { return &Type::instance; }
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
    GlobalMemory::String repr() const final { return this->value_ ? "true" : "false"; }
    TypePtr get_type() const final { return &Type::instance; }
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
    TypePtr type_;

public:
    FunctionValue(auto&& callback, TypePtr function_type) noexcept
        : Value(kind),
          callback_(std::forward<decltype(callback)>(callback)),
          type_(function_type) {}

    GlobalMemory::String repr() const final {
        return GlobalMemory::format("<function at {:p}>", static_cast<const void*>(this));
    }
    TypePtr get_type() const final { return type_; }
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
    GlobalMemory::String repr() const final {
        // TODO: implement
        return {};
    }
    ListValue* operator+(const ListValue& other) const;
    ListValue* operator*(const IntegerValue& other) const;
    Value* operator[](const Slice& indices) const;
};

class TypeRegistry {
private:
    using Primitives =
        std::tuple<AnyType, NullType, IntegerType, FloatType, StringType, BooleanType>;

public:
    template <TypeClass T>
    TypePtr get(auto&&... args) {
        if constexpr (TypeInTupleV<T, Primitives>) {
            return &T::instance;
        } else if constexpr (std::is_same_v<T, ClassType>) {
            // classes with same definition are distinct types
            return new T(std::forward<decltype(args)>(args)...);
        } else {
            return get_cached<T>(std::forward<decltype(args)>(args)...);
        }
    }

    TypePtr get_kind(Kind kind) {
        switch (kind) {
        case Kind::Any:
            return &AnyType::instance;
        case Kind::Integer:
            return &IntegerType::instance;
        case Kind::Float:
            return &FloatType::instance;
        case Kind::String:
            return &StringType::instance;
        case Kind::Boolean:
            return &BooleanType::instance;
        default:
            assert(false && "Unknown primitive kind");
            std::unreachable();
        }
    }

    ObjectPtr get_unknown() { return &UnknownType::instance; }

private:
    template <typename T>
    TypePtr get_cached(auto&&... args) {
        constexpr auto comparator = [](const T* a, const T* b) { return *a < *b; };
        static FlatSet<const T*, decltype(comparator)> instances;
        T type(std::forward<decltype(args)>(args)...);
        const T*& result = instances.try_emplace(&type);
        if (result == &type) {
            result = new T(std::move(type));
        }
        return result;
    }
};

inline TypePtr Object::as_type() const {
    if (!is_type_ && kind_ == Kind::NothingOrUnknown) return &UnknownType::instance;
    return is_type_ ? static_cast<TypePtr>(this) : nullptr;
}

inline ValuePtr Object::as_value() const {
    if (is_type_ && kind_ == Kind::NothingOrUnknown) return new UnknownValue();
    return !is_type_ ? static_cast<ValuePtr>(this) : nullptr;
}

inline const UnknownType UnknownType::instance;

template <Kind K>
inline const PrimitiveType<K> PrimitiveType<K>::instance;

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
    GlobalMemory::String result;
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
