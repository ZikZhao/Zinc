#pragma once
#include "pch.hpp"

#include "diagnosis.hpp"

enum class Kind : std::uint16_t {
    NothingOrUnknown,  // for operation table it means no right operand, for types it means unknown
                       // type
    Any,
    Null,
    Integer,
    Float,
    Boolean,
    Function,
    Array,
    Record,
    Interface,
    Instance,
    Intersection,
    Union,
};

class Transpiler;

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
class RecordType;
class InterfaceType;
class ClassType;
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

template <typename T>
concept TypeClass = std::derived_from<T, Type>;
template <typename V>
concept ValueClass = std::derived_from<V, Value>;

class TypeRegistry {
    friend class ThreadGuard;

private:
    struct TypeComparator {
        template <TypeClass T>
        constexpr bool operator()(T* a, T* b) const noexcept {
            return *a < *b;
        }
    };

private:
    static thread_local TypeRegistry instance;

public:
    template <TypeClass T>
    static T* get(auto&&... args) {
        using Primitives = std::tuple<AnyType, NullType, IntegerType, FloatType, BooleanType>;
        using OtherInternals =
            std::tuple<FunctionType, RecordType, IntersectionType, UnionType, ClassType>;
        if constexpr (TypeInTupleV<T, Primitives>) {
            static_assert(false);
        } else if constexpr (std::is_same_v<T, ClassType>) {
            // classes with same definition are distinct types
            return new T(std::forward<decltype(args)>(args)...);
        } else if constexpr (TypeInTupleV<T, OtherInternals>) {
            return instance.get_cached<T>(std::forward<decltype(args)>(args)...);
        } else {
            // builtin singleton types, e.g. StringType
            std::type_index type_index = std::type_index(typeid(T));
            auto it = instance.builtin_types_.find(type_index);
            if (it != instance.builtin_types_.end()) {
                return static_cast<T*>(it->second);
            } else {
                T* new_type = new T(std::forward<decltype(args)>(args)...);
                instance.builtin_types_.insert({type_index, new_type});
                return new_type;
            }
        }
    }

    static Object* get_unknown();

private:
    std::tuple<
        GlobalMemory::Set<FunctionType*, TypeComparator>,
        GlobalMemory::Set<RecordType*, TypeComparator>,
        GlobalMemory::Set<IntersectionType*, TypeComparator>,
        GlobalMemory::Set<UnionType*, TypeComparator>>
        cache_;
    GlobalMemory::Map<std::type_index, Type*> builtin_types_;

private:
    TypeRegistry() noexcept = default;

    template <TypeClass T>
    T* get_cached(auto&&... args) {
        GlobalMemory::Set<T*, TypeComparator>& type_set =
            std::get<GlobalMemory::Set<T*, TypeComparator>>(cache_);
        T new_type(std::forward<decltype(args)>(args)...);
        if (auto it = type_set.find(&new_type); it != type_set.end()) {
            return *it;
        } else {
            auto [it2, _] = type_set.emplace(new T(std::forward<decltype(args)>(args)...));
            return *it2;
        }
    }
};

class Object : public MemoryManaged {
public:
    Kind kind_;

private:
    bool is_type_;

public:
    Object(Kind kind, bool is_type) noexcept : kind_(kind), is_type_(is_type) {}
    virtual ~Object() = default;
    virtual std::string_view repr() const = 0;
    constexpr std::strong_ordering operator<=>(const Object& other) const noexcept {
        return this <=> &other;
    }
    constexpr bool operator==(const Object& other) const noexcept { return this == &other; }
    Type* as_type();
    Value* as_value();
    virtual void transpile(Transpiler& transpiler) const noexcept = 0;
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
        assert(!(this == &source) || assignable_from_impl(source));
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
    static UnknownType instance;

private:
    UnknownType() noexcept : Type(kind) {}

public:
    std::string_view repr() const final { return "unknown"; }
    bool assignable_from_impl(const Type& source) const final { return true; }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class AnyType final : public Type {
public:
    static constexpr Kind kind = Kind::Any;
    static AnyType instance;

public:
    AnyType() noexcept : Type(kind) {}
    std::string_view repr() const final { return "any"; }
    bool assignable_from_impl(const Type& source) const final { return true; }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class NullType final : public Type {
public:
    static constexpr Kind kind = Kind::Null;
    static NullType instance;

public:
    NullType() noexcept : Type(kind) {}
    std::string_view repr() const final { return "null"; }
    bool assignable_from_impl(const Type& source) const final {
        /// No variable can have null type except null literal
        std::unreachable();
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class IntegerType final : public Type {
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
    const bool is_signed_;
    const std::uint8_t bits_;  // 8,16,32,64 bits (or 0 for untyped integer)

public:
    IntegerType(bool is_signed, std::uint8_t bits) noexcept
        : Type(kind), is_signed_(is_signed), bits_(bits) {
        assert(bits == 0 || bits == 8 || bits == 16 || bits == 32 || bits == 64);
    }
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}{}", is_signed_ ? "i" : "u", bits_);
    }
    bool assignable_from_impl(const Type& source) const final {
        if (source.kind_ != kind) {
            return false;
        }
        const IntegerType& other_int = static_cast<const IntegerType&>(source);
        return other_int.bits_ == 0 ||
               (this->is_signed_ == other_int.is_signed_ && this->bits_ >= other_int.bits_);
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FloatType final : public Type {
public:
    static constexpr Kind kind = Kind::Float;
    static FloatType untyped_instance;
    static FloatType f32_instance;
    static FloatType f64_instance;

public:
    const std::uint8_t bits_;  // 32,64 bits (or 0 for untyped float)

public:
    FloatType(std::uint8_t bits) noexcept : Type(kind), bits_(bits) {
        assert(bits == 0 || bits == 32 || bits == 64);
    }
    std::string_view repr() const final { return GlobalMemory::format_view("f{}", bits_); }
    bool assignable_from_impl(const Type& source) const final {
        if (source.kind_ != kind) {
            return false;
        }
        const FloatType& other_float = static_cast<const FloatType&>(source);
        return other_float.bits_ == 0 || this->bits_ >= other_float.bits_;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class BooleanType final : public Type {
public:
    static constexpr Kind kind = Kind::Boolean;
    static BooleanType instance;

public:
    BooleanType() noexcept : Type(kind) {}
    std::string_view repr() const final { return "bool"; }
    bool assignable_from_impl(const Type& source) const final { return source.kind_ == kind; }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    static GlobalMemory::Set<FunctionType*> list_all(Type* type);

public:
    const ComparableSpan<Type*> parameters_;
    Type* const return_type_;

public:
    FunctionType(ComparableSpan<Type*> parameters, Type* return_type) noexcept
        : Type(kind), parameters_(parameters), return_type_(return_type) {}

    std::string_view repr() const final {
        GlobalMemory::String params_repr =
            parameters_ | std::views::transform([](Type* type) { return type->repr(); }) |
            std::views::join_with(", "sv) | GlobalMemory::collect<GlobalMemory::String>();
        return GlobalMemory::format_view("({}) => {}", params_repr, return_type_->repr());
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
        return return_type_->assignable_from(*func_other.return_type_);
    }
    std::strong_ordering operator<=>(const FunctionType& other) const noexcept = default;
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ArrayType final : public Type {
public:
    Type* const element_type_;
    std::size_t size_ = 0;  // 0 means dynamic size

    ArrayType(Type* element_type) noexcept : Type(Kind::Array), element_type_(element_type) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}[]", element_type_->repr());
    }
    bool assignable_from_impl(const Type& source) const final {
        if (source.kind_ != Kind::Array) {
            return false;
        }
        const ArrayType& other_array = static_cast<const ArrayType&>(source);
        return element_type_->assignable_from(*other_array.element_type_) &&
               (size_ == 0 || size_ == other_array.size_);
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class RecordType : public Type {
public:
    static constexpr Kind kind = Kind::Record;

private:
    GlobalMemory::Map<std::string_view, Type*> fields_;

public:
    RecordType(GlobalMemory::Map<std::string_view, Type*> fields) noexcept
        : Type(kind), fields_(std::move(fields)) {}

    std::string_view repr() const final {
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ClassType : public Type {
private:
    const std::string_view identifier_;
    const ComparableSpan<InterfaceType*> implements_;
    ClassType* const extends_;
    const GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> methods_;
    const GlobalMemory::Map<std::string_view, Type*> attr_;

public:
    ClassType(
        std::string_view identifier,
        ComparableSpan<InterfaceType*> interfaces,
        ClassType* extends,
        GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> methods,
        GlobalMemory::Map<std::string_view, Type*> attr
    ) noexcept
        : Type(Kind::Instance),
          identifier_(identifier),
          implements_(interfaces),
          extends_(extends),
          methods_(std::move(methods)),
          attr_(std::move(attr)) {}

    std::string_view repr() const override {
        return GlobalMemory::format_view("class {}", identifier_);
    }

    bool assignable_from_impl(const Type& other) const override { return false; }

    OverloadedFunctionValue* get_method(std::string_view name) const {
        auto it = methods_.find(name);
        if (it == methods_.end()) {
            throw UnlocatedProblem::make<AttributeError>(
                GlobalMemory::format_view("Class {} has no method named {}", identifier_, name)
            );
        }
        return it->second;
    }

    Type* get_attr(std::string_view name) const {
        auto it = attr_.find(name);
        if (it == attr_.end()) {
            throw UnlocatedProblem::make<AttributeError>(
                GlobalMemory::format_view("Class {} has no attribute named {}", identifier_, name)
            );
        }
        return it->second;
    }
    void transpile(Transpiler& transpiler) const noexcept override;
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

public:
    const ComparableSpan<Type*> types_;

public:
    IntersectionType(Type* left, Type* right) noexcept : Type(kind), types_{combine(left, right)} {}

    std::string_view repr() const final {
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
    void transpile(Transpiler& transpiler) const noexcept final;
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

public:
    const ComparableSpan<Type*> types_;

public:
    UnionType(Type* left, Type* right) noexcept : Type(kind), types_(combine(left, right)) {}

    std::string_view repr() const final {
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
    void transpile(Transpiler& transpiler) const noexcept final;
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
    virtual Type* get_type() const = 0;
    virtual Value* resolve_to(Type* target) const = 0;
};

class UnknownValue final : public Value {
    friend class Object;

public:
    static constexpr Kind kind = Kind::NothingOrUnknown;

private:
    UnknownValue() noexcept : Value(kind) {}
    std::string_view repr() const final { return "unknown"; }
    UnknownType* get_type() const final { return &UnknownType::instance; }
    UnknownValue* resolve_to(Type* target) const final { return new UnknownValue(); }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class NullValue final : public Value {
public:
    static constexpr Kind kind = Kind::Null;

public:
    NullValue() noexcept : Value(kind) {}
    std::string_view repr() const final { return "null"; }
    NullType* get_type() const final { return &NullType::instance; }
    NullValue* resolve_to(Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Null) {
            throw UnlocatedProblem::make<TypeMismatchError>("null", target->repr());
        }
        return new NullValue();
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;

public:
    IntegerType* const type_;  // nullptr for integer literals without a specific type
    union {
        std::int64_t ivalue_;
        std::uint64_t uvalue_;
        std::string_view raw_;
    };

public:
    IntegerValue(IntegerType* type, std::int64_t value) noexcept
        : Value(kind), type_(type), ivalue_(value) {
        assert(type != nullptr && type->is_signed_);
    }
    IntegerValue(IntegerType* type, std::uint64_t value) noexcept
        : Value(kind), type_(type), uvalue_(value) {
        assert(type != nullptr && !type->is_signed_);
    }
    explicit IntegerValue(std::string_view value) noexcept
        : Value(kind), type_(&IntegerType::untyped_instance), raw_(value) {}
    std::string_view repr() const final { return GlobalMemory::format_view("{}", ivalue_); }
    IntegerType* get_type() const final { return type_; }
    IntegerValue* resolve_to(Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Integer) {
            throw UnlocatedProblem::make<TypeMismatchError>("integer", target->repr());
        }
        IntegerType* int_target = static_cast<IntegerType*>(target);
        if (int_target == &IntegerType::untyped_instance) {
            // most suitable type inference
            if (type_) {
                return new IntegerValue(*this);
            }
            if (raw_[0] == '-') {
                std::int64_t value;
                auto [ptr, ec] = std::from_chars(raw_.data(), raw_.data() + raw_.size(), value);
                if (ec != std::errc()) {
                    std::unreachable();
                }
                if (std::in_range<std::int32_t>(value)) {
                    return new IntegerValue(&IntegerType::i32_instance, value);
                } else {
                    return new IntegerValue(&IntegerType::i64_instance, value);
                }
            } else {
                std::uint64_t uvalue;
                auto [ptr, ec] = std::from_chars(raw_.data(), raw_.data() + raw_.size(), uvalue);
                if (ec != std::errc()) {
                    std::unreachable();
                }
                if (std::in_range<std::int32_t>(uvalue)) {
                    return new IntegerValue(&IntegerType::i32_instance, uvalue);
                } else if (std::in_range<std::int64_t>(uvalue)) {
                    return new IntegerValue(&IntegerType::i64_instance, uvalue);
                } else {
                    return new IntegerValue(&IntegerType::u64_instance, uvalue);
                }
            }
        } else {
            // convert to the specified target type
            if (type_ == int_target) {
                return new IntegerValue(*this);
            } else if (type_) {
                throw UnlocatedProblem::make<TypeMismatchError>(type_->repr(), target->repr());
            }
            if (int_target->is_signed_) {
                int64_t value;
                auto [ptr, ec] = std::from_chars(raw_.data(), raw_.data() + raw_.size(), value);
                if (ec != std::errc()) {
                    std::unreachable();
                }
                if (std::size_t shift = 64 - int_target->bits_;
                    (value << shift) >> shift != value) {
                    throw UnlocatedProblem::make<OverflowError>(raw_, int_target->repr());
                }
                return new IntegerValue(int_target, value);
            } else {
                uint64_t uvalue;
                auto [ptr, ec] = std::from_chars(raw_.data(), raw_.data() + raw_.size(), uvalue);
                if (ec != std::errc()) {
                    std::unreachable();
                }
                if (uvalue >> int_target->bits_ != 0) {
                    throw UnlocatedProblem::make<OverflowError>(raw_, int_target->repr());
                }
                return new IntegerValue(int_target, uvalue);
            }
        }
    }
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::Float;

public:
    FloatType* const type_;
    double value_;

public:
    FloatValue(FloatType* type, double value) noexcept : Value(kind), type_(type), value_(value) {
        assert(type != nullptr);
    }
    std::string_view repr() const final { return GlobalMemory::format_view("{}", value_); }
    FloatType* get_type() const final { return type_; }
    FloatValue* resolve_to(Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Float) {
            throw UnlocatedProblem::make<TypeMismatchError>("float", target->repr());
        }
        FloatType* float_target = static_cast<FloatType*>(target);
        if (float_target == &FloatType::untyped_instance) {
            // default to double
            if (type_) {
                return new FloatValue(*this);
            }
            return new FloatValue(&FloatType::f64_instance, value_);
        } else {
            if (type_ == float_target) {
                return new FloatValue(*this);
            } else if (type_) {
                throw UnlocatedProblem::make<TypeMismatchError>(type_->repr(), target->repr());
            }
            return new FloatValue(float_target, value_);
        }
    }
    explicit FloatValue(double value) noexcept
        : Value(kind), type_(&FloatType::untyped_instance), value_(value) {}
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class BooleanValue final : public Value {
public:
    static constexpr Kind kind = Kind::Boolean;

public:
    bool value_;

public:
    BooleanValue(bool value) noexcept : Value(kind), value_(value) {}
    std::string_view repr() const final { return this->value_ ? "true" : "false"; }
    BooleanType* get_type() const final { return &BooleanType::instance; }
    BooleanValue* resolve_to(Type* target) const final {
        assert(target);
        if (target->kind_ != Kind::Boolean) {
            throw UnlocatedProblem::make<TypeMismatchError>("boolean", target->repr());
        }
        return new BooleanValue(*this);
    }
    BooleanValue operator==(const BooleanValue& other) const;
    BooleanValue operator!=(const BooleanValue& other) const;
    BooleanValue operator&&(const BooleanValue& other) const;
    BooleanValue operator||(const BooleanValue& other) const;
    BooleanValue operator!() const;
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::Function;

public:
    FunctionType* type_;
    const void* source_;
    std::function<Value*(ComparableSpan<Value*>)> callback_;

public:
    FunctionValue(const void* source, decltype(callback_) invoke) noexcept
        : Value(kind), type_(nullptr), source_(source), callback_(std::move(invoke)) {}
    FunctionValue(FunctionType* type, const void* source, decltype(callback_) invoke) noexcept
        : Value(kind), type_(type), source_(source), callback_(std::move(invoke)) {}

    std::string_view repr() const final {
        return GlobalMemory::format_view("<function at {:p}>", static_cast<const void*>(this));
    }
    FunctionType* get_type() const final { return type_; }
    FunctionValue* resolve_to(Type* target) const final {
        /// Type of function value is managed by OverloadedFunctionValue
        /// @code let func: (a) -> b = ... @endcode
        /// Here, right-hand side is of type OverloadedFunctionValue, which holds overloads so
        /// the resolve_to of FunctionValue will never be called.
        std::unreachable();
    }
    Value* invoke(ComparableSpan<Value*> args) const { return callback_(args); }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ArrayValue final : public Value {
public:
    ArrayType* type_;
    union {
        GlobalMemory::Vector<Value*> elements_;
        std::string_view string_;
    };

public:
    ArrayValue(GlobalMemory::Vector<Value*>&& elements) noexcept
        : Value(Kind::Array), type_(nullptr), elements_(std::move(elements)) {}
    ArrayValue(std::string_view string) noexcept
        : Value(Kind::Array),
          type_(TypeRegistry::get<ArrayType>(&IntegerType::u8_instance)),
          string_(string) {}
    ~ArrayValue() {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("[{}]", type_->element_type_->repr());
    }
    ArrayType* get_type() const final { return type_; }
    ArrayValue* resolve_to(Type* target) const final {
        /// TODO: implement
        return nullptr;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class InstanceValue final : public Value {
public:
    ClassType* cls_;
    GlobalMemory::Map<std::string_view, Value*> attributes_;

public:
    InstanceValue(ClassType* cls, decltype(attributes_) attributes) noexcept
        : Value(Kind::Instance), cls_(cls), attributes_(std::move(attributes)) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("<instance of {}>", cls_->repr());
    }
    ClassType* get_type() const final { return cls_; }
    InstanceValue* resolve_to(Type* target) const final {
        if (target != cls_) {
            throw UnlocatedProblem::make<TypeMismatchError>("instance", target->repr());
        }
        return new InstanceValue(*this);
    }
    Value* get_attr(std::string_view attr) noexcept { return attributes_.at(attr); }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class OverloadedFunctionValue final : public Value {
private:
    Type* type_;
    GlobalMemory::Vector<Object*> overloads_;  /// vector of FunctionType* | FunctionValue*

public:
    OverloadedFunctionValue(FunctionValue* first) noexcept
        : Value(Kind::Intersection), type_(first->type_), overloads_{first} {}
    OverloadedFunctionValue(FunctionType* first) noexcept
        : Value(Kind::Intersection), type_(first), overloads_{first} {}
    std::string_view repr() const final {
        // TODO
        return {};
    }
    Type* get_type() const final { return type_; }
    OverloadedFunctionValue* resolve_to(Type* target) const final { std::unreachable(); }
    void add_overload(FunctionValue* func) noexcept { overloads_.push_back(func); }
    void overload_resolve_to(const void* source, FunctionType* target) noexcept {
        for (Object*& overload : overloads_) {
            if (overload->as_value() && static_cast<FunctionValue*>(overload)->source_ == source) {
                overload = new FunctionValue(
                    target, source, static_cast<FunctionValue*>(overload)->callback_
                );
                break;
            }
        }
        if (type_) {
            type_ = TypeRegistry::get<IntersectionType>(type_, target);
        } else {
            type_ = target;
        }
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

inline Type* Object::as_type() {
    if (!is_type_ && kind_ == Kind::NothingOrUnknown) return &UnknownType::instance;
    return is_type_ ? static_cast<Type*>(this) : nullptr;
}

inline Value* Object::as_value() {
    if (is_type_ && kind_ == Kind::NothingOrUnknown) return new UnknownValue();
    return !is_type_ ? static_cast<Value*>(this) : nullptr;
}

inline thread_local TypeRegistry TypeRegistry::instance;

inline Object* TypeRegistry::get_unknown() { return &UnknownType::instance; }

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

inline GlobalMemory::Set<FunctionType*> FunctionType::list_all(Type* type) {
    if (type->kind_ == Kind::Intersection) {
        const IntersectionType& intersection = static_cast<const IntersectionType&>(*type);
        return intersection.types_ | std::views::transform([](Type* func_type) {
                   return static_cast<FunctionType*>(func_type);
               }) |
               GlobalMemory::collect<GlobalMemory::Set<FunctionType*>>();
    } else {
        assert(type->kind_ == Kind::Function);
        return GlobalMemory::Set<FunctionType*>{static_cast<FunctionType*>(type)};
    }
}

// IntegerValue operators

inline IntegerValue IntegerValue::operator+(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ + other.ivalue_);
}

inline IntegerValue IntegerValue::operator-(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ - other.ivalue_);
}

inline IntegerValue IntegerValue::operator-() const { return IntegerValue(type_, -this->ivalue_); }

inline IntegerValue IntegerValue::operator*(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ * other.ivalue_);
}

inline IntegerValue IntegerValue::operator/(const IntegerValue& other) const {
    if (other.ivalue_ == 0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return IntegerValue(std::max(type_, other.type_), ivalue_ / other.ivalue_);
}

inline IntegerValue IntegerValue::operator%(const IntegerValue& other) const {
    if (other.ivalue_ == 0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return IntegerValue(std::max(type_, other.type_), ivalue_ % other.ivalue_);
}

inline BooleanValue IntegerValue::operator==(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ == other.ivalue_);
}

inline BooleanValue IntegerValue::operator!=(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ != other.ivalue_);
}

inline BooleanValue IntegerValue::operator<(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ < other.ivalue_);
}

inline BooleanValue IntegerValue::operator<=(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ <= other.ivalue_);
}

inline BooleanValue IntegerValue::operator>(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ > other.ivalue_);
}

inline BooleanValue IntegerValue::operator>=(const IntegerValue& other) const {
    return BooleanValue(this->ivalue_ >= other.ivalue_);
}

inline IntegerValue IntegerValue::operator&(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ & other.ivalue_);
}

inline IntegerValue IntegerValue::operator|(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ | other.ivalue_);
}

inline IntegerValue IntegerValue::operator^(const IntegerValue& other) const {
    return IntegerValue(std::max(type_, other.type_), ivalue_ ^ other.ivalue_);
}

inline IntegerValue IntegerValue::operator~() const { return IntegerValue(type_, ~this->ivalue_); }

inline IntegerValue IntegerValue::operator<<(const IntegerValue& other) const {
    if (other.ivalue_ < 0) throw UnlocatedProblem::make<ShiftByNegativeError>();
    return IntegerValue(type_, this->ivalue_ << other.ivalue_);
}

inline IntegerValue IntegerValue::operator>>(const IntegerValue& other) const {
    if (other.ivalue_ < 0) throw UnlocatedProblem::make<ShiftByNegativeError>();
    return IntegerValue(type_, this->ivalue_ >> other.ivalue_);
}

// FloatValue operators
inline FloatValue FloatValue::operator+(const FloatValue& other) const {
    return FloatValue(type_, this->value_ + other.value_);
}

inline FloatValue FloatValue::operator-(const FloatValue& other) const {
    return FloatValue(type_, this->value_ - other.value_);
}

inline FloatValue FloatValue::operator-() const { return FloatValue(type_, -this->value_); }

inline FloatValue FloatValue::operator*(const FloatValue& other) const {
    return FloatValue(type_, this->value_ * other.value_);
}

inline FloatValue FloatValue::operator/(const FloatValue& other) const {
    if (other.value_ == 0.0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return FloatValue(type_, this->value_ / other.value_);
}

inline FloatValue FloatValue::operator%(const FloatValue& other) const {
    if (other.value_ == 0.0) throw UnlocatedProblem::make<DivisionByZeroError>();
    return FloatValue(type_, std::fmod(this->value_, other.value_));
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
