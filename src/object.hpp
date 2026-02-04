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
    Reference,
    Template,
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
class ReferenceType;

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
concept TypeClass = !std::is_same_v<T, Type> && std::derived_from<T, Type>;
template <typename V>
concept ValueClass = !std::is_same_v<V, Value> && std::derived_from<V, Value>;

using FunctionOverloads = GlobalMemory::Vector<Object*>;

class TypeRegistry {
    friend class ThreadGuard;

private:
    struct TypeComparator {
        template <TypeClass T>
        constexpr bool operator()(T* a, T* b) const noexcept {
            GlobalMemory::Set<std::pair<const Type*, const Type*>> assumed_equal;
            return a->compare_congruent(b, assumed_equal) == std::strong_ordering::less;
        }
    };

private:
    static thread_local std::optional<TypeRegistry> instance;

public:
    template <TypeClass T>
    static T* get(auto&&... args) noexcept {
        using Primitives = std::tuple<AnyType, NullType, IntegerType, FloatType, BooleanType>;
        using OtherInternals = std::
            tuple<FunctionType, RecordType, IntersectionType, UnionType, ClassType, ReferenceType>;
        if constexpr (TypeInTupleV<T, Primitives>) {
            static_assert(false);
        } else if constexpr (std::is_same_v<T, ClassType>) {
            // classes with same definition are distinct types
            return new T(std::forward<decltype(args)>(args)...);
        } else if constexpr (TypeInTupleV<T, OtherInternals>) {
            return instance->get_cached<T>(std::forward<decltype(args)>(args)...);
        } else {
            // builtin singleton types, e.g. StringType
            std::type_index type_index = std::type_index(typeid(T));
            auto it = instance->builtin_types_.find(type_index);
            if (it != instance->builtin_types_.end()) {
                return static_cast<T*>(it->second);
            } else {
                T* new_type = new T(std::forward<decltype(args)>(args)...);
                instance->builtin_types_.insert({type_index, new_type});
                return new_type;
            }
        }
    }

    static Type* get_unknown() noexcept;

private:
    std::tuple<
        GlobalMemory::Set<FunctionType*, TypeComparator>,
        GlobalMemory::Set<RecordType*, TypeComparator>,
        GlobalMemory::Set<IntersectionType*, TypeComparator>,
        GlobalMemory::Set<UnionType*, TypeComparator>,
        GlobalMemory::Set<ReferenceType*, TypeComparator>>
        cache_;
    GlobalMemory::Map<std::type_index, Type*> builtin_types_;
    GlobalMemory::MultiMap<Type*, Type*> pending_;

private:
    template <TypeClass T>
    T* get_cached(auto&&... args) noexcept {
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

public:
    TypeRegistry() noexcept = default;
};

class Object : public GlobalMemory::MemoryManaged {
public:
    Kind kind_;

private:
    bool is_type_;

public:
    Object(Kind kind, bool is_type) noexcept : kind_(kind), is_type_(is_type) {}

    Type* dyn_type();

    Value* dyn_value();

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

    virtual void transpile(Transpiler& transpiler) const noexcept = 0;
};

class Type : public Object {
protected:
    Type(Kind kind) noexcept : Object(kind, true) {}

public:
    Type* dyn_type() = delete;
    Value* dyn_value() = delete;

    std::strong_ordering compare_congruent(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept {
        if (this == other) {
            return std::strong_ordering::equal;
        }
        if (kind_ != other->kind_) {
            return kind_ <=> other->kind_;
        }
        if (assumed_equal.contains({this, other})) {
            return std::strong_ordering::equal;
        }
        assumed_equal.insert({this, other});
        return compare_congruent_impl(other, assumed_equal);
    }

    bool assignable_from(const Type* source) const {
        assert(!(this == source) || assignable_from_impl(source));
        return this == source || assignable_from_impl(source);
    }

protected:
    virtual bool assignable_from_impl(const Type* source) const = 0;

    virtual std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept {
        // Default implementation for primitive types
        UNREACHABLE();
    };
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
    bool assignable_from_impl(const Type* source) const final { return true; }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class AnyType final : public Type {
public:
    static constexpr Kind kind = Kind::Any;
    static AnyType instance;

public:
    AnyType() noexcept : Type(kind) {}
    std::string_view repr() const final { return "any"; }
    bool assignable_from_impl(const Type* source) const final { return true; }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class NullType final : public Type {
public:
    static constexpr Kind kind = Kind::Null;
    static NullType instance;

public:
    NullType() noexcept : Type(kind) {}
    std::string_view repr() const final { return "null"; }
    bool assignable_from_impl(const Type* source) const final {
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
    bool is_signed_;
    std::uint8_t bits_;  // 8, 16, 32, 64 bits (or 0 for untyped integer)

public:
    IntegerType(bool is_signed, std::uint8_t bits) noexcept
        : Type(kind), is_signed_(is_signed), bits_(bits) {
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FloatType final : public Type {
public:
    static constexpr Kind kind = Kind::Float;
    static FloatType untyped_instance;
    static FloatType f32_instance;
    static FloatType f64_instance;

public:
    std::uint8_t bits_;  // 32, 64 bits (or 0 for untyped float)

public:
    FloatType(std::uint8_t bits) noexcept : Type(kind), bits_(bits) {
        assert(bits == 0 || bits == 32 || bits == 64);
    }
    std::string_view repr() const final { return GlobalMemory::format_view("f{}", bits_); }
    bool assignable_from_impl(const Type* source) const final {
        const FloatType* other_float = source->dyn_cast<FloatType>();
        return other_float && (other_float->bits_ == 0 || this->bits_ >= other_float->bits_);
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
    bool assignable_from_impl(const Type* source) const final {
        return source->dyn_cast<BooleanType>() != nullptr;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FunctionType final : public Type {
public:
    static constexpr Kind kind = Kind::Function;

public:
    ComparableSpan<Type*> parameters_;
    Type* return_type_;

public:
    FunctionType(ComparableSpan<Type*> parameters, Type* return_type) noexcept
        : Type(kind), parameters_(parameters), return_type_(return_type) {}

    std::string_view repr() const final {
        GlobalMemory::String params_repr =
            parameters_ | std::views::transform([](Type* type) { return type->repr(); }) |
            std::views::join_with(", "sv) | GlobalMemory::collect<GlobalMemory::String>();
        return GlobalMemory::format_view("({}) => {}", params_repr, return_type_->repr());
    }

    bool assignable_from_impl(const Type* source) const final;
    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const FunctionType* other_func = other->cast<FunctionType>();
        if (parameters_.size() != other_func->parameters_.size()) {
            return parameters_.size() <=> other_func->parameters_.size();
        }
        for (std::size_t i = 0; i < parameters_.size(); ++i) {
            assumed_equal.insert({parameters_[i], other_func->parameters_[i]});
            auto cmp = parameters_[i]->compare_congruent(other_func->parameters_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        assumed_equal.insert({return_type_, other_func->return_type_});
        return return_type_->compare_congruent(other_func->return_type_, assumed_equal);
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ArrayType final : public Type {
public:
    static constexpr Kind kind = Kind::Array;

public:
    Type* element_type_;
    std::size_t size_ = 0;  // 0 means dynamic size

    ArrayType(Type* element_type) noexcept : Type(kind), element_type_(element_type) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}[]", element_type_->repr());
    }
    bool assignable_from_impl(const Type* source) const final {
        const ArrayType* other_array = source->dyn_cast<ArrayType>();
        return other_array && element_type_->assignable_from(other_array->element_type_) &&
               (size_ == 0 || size_ == other_array->size_);
    }
    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ArrayType* other_array = other->cast<ArrayType>();
        if (size_ != other_array->size_) {
            return size_ <=> other_array->size_;
        }
        assumed_equal.insert({element_type_, other_array->element_type_});
        return element_type_->compare_congruent(other_array->element_type_, assumed_equal);
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

    bool assignable_from_impl(const Type* source) const final {
        // (a,b,c) is assignable to (a,b)
        // i.e., source must have at least all fields of this
        const RecordType* other_record = source->dyn_cast<RecordType>();
        if (other_record == nullptr) {
            return false;
        }
        for (const auto& [name, type] : fields_) {
            auto it = other_record->fields_.find(name);
            if (it == other_record->fields_.end() || !(*it).second->assignable_from(type)) {
                return false;
            }
        }
        return true;
    }
    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const RecordType* other_record = other->cast<RecordType>();
        if (fields_.size() != other_record->fields_.size()) {
            return fields_.size() <=> other_record->fields_.size();
        }
        auto it1 = fields_.begin();
        auto it2 = other_record->fields_.begin();
        for (; it1 != fields_.end() && it2 != other_record->fields_.end(); ++it1, ++it2) {
            if (it1->first != it2->first) {
                return it1->first <=> it2->first;
            }
            assumed_equal.insert({it1->second, it2->second});
            auto cmp = it1->second->compare_congruent(it2->second, assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class InterfaceType final : public Type {
public:
    static constexpr Kind kind = Kind::Interface;

private:
    GlobalMemory::Map<std::string_view, OverloadedFunctionValue*> methods_;

public:
    InterfaceType() noexcept : Type(kind) {}
    std::string_view repr() const override {
        // TODO
        return {};
    }
    bool assignable_from_impl(const Type* source) const final {
        // TODO
        return false;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ClassType : public Type {
public:
    static constexpr Kind kind = Kind::Instance;

private:
    std::string_view identifier_;
    Type* extends_;
    ComparableSpan<Type*> implements_;
    GlobalMemory::Map<std::string_view, Type*> attr_;
    GlobalMemory::Map<std::string_view, FunctionOverloads> methods_;

public:
    ClassType(
        std::string_view identifier,
        Type* extends,
        ComparableSpan<Type*> interfaces,
        GlobalMemory::Map<std::string_view, Type*> attr,
        GlobalMemory::Map<std::string_view, FunctionOverloads> methods
    ) noexcept
        : Type(kind),
          identifier_(identifier),
          extends_(extends),
          implements_(interfaces),
          attr_(std::move(attr)),
          methods_(std::move(methods)) {}

    std::string_view repr() const override {
        return GlobalMemory::format_view("class {}", identifier_);
    }

    bool assignable_from_impl(const Type* other) const final { return false; }

    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        // ClassType is not interned, so this function is never called
        UNREACHABLE();
    }

    FunctionOverloads get_method(std::string_view name) const {
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
    static ComparableSpan<Type*> flatten(ComparableSpan<Type*> unflattened_types) {
        std::size_t size = 0;
        for (Type* type : unflattened_types) {
            if (IntersectionType* intersection_type = type->dyn_cast<IntersectionType>()) {
                size += intersection_type->types_.size();
            } else {
                size++;
            }
        }
        ComparableSpan<Type*> buffer = GlobalMemory::alloc_array<Type*>(size);
        std::size_t index = 0;
        for (Type* type : unflattened_types) {
            if (IntersectionType* intersection_type = type->dyn_cast<IntersectionType>()) {
                for (Type* inner_type : intersection_type->types_) {
                    buffer[index++] = inner_type;
                }
            } else {
                buffer[index++] = type;
            }
        }
        return buffer;
    }
    static ComparableSpan<Type*> flatten(Type* left, Type* right) {
        Type* types[] = {left, right};
        return flatten(ComparableSpan<Type*>(types));
    }

public:
    ComparableSpan<Type*> types_;

public:
    IntersectionType(auto&&... unflattened_types) noexcept
        : Type(kind), types_{flatten(unflattened_types...)} {}

    std::string_view repr() const final {
        // TODO
        return {};
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

    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const IntersectionType* other_intersection = other->cast<IntersectionType>();
        if (types_.size() != other_intersection->types_.size()) {
            return types_.size() <=> other_intersection->types_.size();
        }
        for (std::size_t i = 0; i < types_.size(); ++i) {
            assumed_equal.insert({types_[i], other_intersection->types_[i]});
            auto cmp = types_[i]->compare_congruent(other_intersection->types_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    void transpile(Transpiler& transpiler) const noexcept final;
};

class UnionType final : public Type {
public:
    static constexpr Kind kind = Kind::Union;

private:
    static ComparableSpan<Type*> flatten(ComparableSpan<Type*> unflattened_types) {
        std::size_t size = 0;
        for (Type* type : unflattened_types) {
            if (UnionType* union_type = type->dyn_cast<UnionType>()) {
                size += union_type->types_.size();
            } else {
                size++;
            }
        }
        ComparableSpan<Type*> buffer = GlobalMemory::alloc_array<Type*>(size);
        std::size_t index = 0;
        for (Type* type : unflattened_types) {
            if (UnionType* union_type = type->dyn_cast<UnionType>()) {
                for (Type* inner_type : union_type->types_) {
                    buffer[index++] = inner_type;
                }
            } else {
                buffer[index++] = type;
            }
        }
        return buffer;
    }
    static ComparableSpan<Type*> flatten(Type* left, Type* right) {
        Type* types[] = {left, right};
        return flatten(ComparableSpan<Type*>(types));
    }

public:
    ComparableSpan<Type*> types_;

public:
    UnionType(auto&&... unflattened_types) noexcept
        : Type(kind), types_(flatten(unflattened_types...)) {}

    std::string_view repr() const final {
        // TODO
        return {};
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

    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const UnionType* other_union = other->cast<UnionType>();
        if (types_.size() != other_union->types_.size()) {
            return types_.size() <=> other_union->types_.size();
        }
        for (std::size_t i = 0; i < types_.size(); ++i) {
            assumed_equal.insert({types_[i], other_union->types_[i]});
            auto cmp = types_[i]->compare_congruent(other_union->types_[i], assumed_equal);
            if (cmp != std::strong_ordering::equal) {
                return cmp;
            }
        }
        return std::strong_ordering::equal;
    }

    void transpile(Transpiler& transpiler) const noexcept final;
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
    bool assignable_from_impl(const Type* source) const final {
        const ReferenceType* other_ref = source->dyn_cast<ReferenceType>();
        return other_ref && referenced_type_->assignable_from(other_ref->referenced_type_);
    }
    std::strong_ordering compare_congruent_impl(
        const Type* other, GlobalMemory::Set<std::pair<const Type*, const Type*>>& assumed_equal
    ) const noexcept final {
        const ReferenceType* other_ref = other->cast<ReferenceType>();
        assumed_equal.insert({referenced_type_, other_ref->referenced_type_});
        return referenced_type_->compare_congruent(other_ref->referenced_type_, assumed_equal);
    }
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
    Type* dyn_type() = delete;
    Value* dyn_value() = delete;
    virtual Type* get_type() const = 0;
    virtual Value* resolve_to(Type* target) const = 0;
    virtual void assign_from(Value* source) = 0;
};

class UnknownValue final : public Value {
    friend class Object;
    friend class Term;

public:
    static constexpr Kind kind = Kind::NothingOrUnknown;

private:
    UnknownValue() noexcept : Value(kind) {}
    std::string_view repr() const final { return "unknown"; }
    UnknownType* get_type() const final { return &UnknownType::instance; }
    UnknownValue* resolve_to(Type* target) const final { return new UnknownValue(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
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
    void assign_from(Value* source) final { UNREACHABLE(); }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class IntegerValue final : public Value {
public:
    static constexpr Kind kind = Kind::Integer;

public:
    IntegerType* const type_;  // nullptr for integer literals without a specific type
    BigInt value_;

public:
    explicit IntegerValue(std::string_view value) noexcept
        : Value(kind), type_(&IntegerType::untyped_instance), value_(value) {}
    IntegerValue(IntegerType* type, BigInt value) noexcept
        : Value(kind), type_(type), value_(std::move(value)) {
        assert(type && type != &IntegerType::untyped_instance);
    }
    std::string_view repr() const final {
        return GlobalMemory::format_view("{}", value_.to_string());
    }
    IntegerType* get_type() const final { return type_; }
    IntegerValue* resolve_to(Type* target) const final {
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
            IntegerType* int_target = target->cast<IntegerType>();
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
            IntegerType* int_target = target->cast<IntegerType>();
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FloatValue final : public Value {
public:
    static constexpr Kind kind = Kind::Float;

public:
    FloatType* const type_;
    double value_;

public:
    explicit FloatValue(double value) noexcept
        : Value(kind), type_(&FloatType::untyped_instance), value_(value) {}
    FloatValue(FloatType* type, double value) noexcept : Value(kind), type_(type), value_(value) {
        assert(type && type != &FloatType::untyped_instance);
    }
    std::string_view repr() const final { return GlobalMemory::format_view("{}", value_); }
    FloatType* get_type() const final { return type_; }
    FloatValue* resolve_to(Type* target) const final {
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
            FloatType* float_target = target->cast<FloatType>();
            if (type_ == float_target) {
                return new FloatValue(*this);
            } else if (type_) {
                throw UnlocatedProblem::make<TypeMismatchError>(type_->repr(), target->repr());
            }
            return new FloatValue(float_target, value_);
        } else {
            FloatType* float_target = target->cast<FloatType>();
            return new FloatValue(float_target, value_);
        }
    }
    void assign_from(Value* source) final {
        FloatValue* float_source = source->cast<FloatValue>();
        this->value_ = float_source->value_;
    }
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
        if (target && target->kind_ != Kind::Boolean) {
            throw UnlocatedProblem::make<TypeMismatchError>("boolean", target->repr());
        }
        return new BooleanValue(*this);
    }
    void assign_from(Value* source) final {
        BooleanValue* bool_source = source->cast<BooleanValue>();
        this->value_ = bool_source->value_;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class FunctionValue final : public Value {
public:
    static constexpr Kind kind = Kind::Function;

public:
    FunctionType* type_;
    std::function<Value*(ComparableSpan<Value*>)> callback_;

public:
    FunctionValue(FunctionType* type, decltype(callback_) invoke) noexcept
        : Value(kind), type_(type), callback_(std::move(invoke)) {}

    std::string_view repr() const final {
        return GlobalMemory::format_view("<function at {:p}>", static_cast<const void*>(this));
    }
    FunctionType* get_type() const final { return type_; }
    FunctionValue* resolve_to(Type* target) const final { UNREACHABLE(); }
    void assign_from(Value* source) final { UNREACHABLE(); }
    Value* invoke(ComparableSpan<Value*> args) const { return callback_(args); }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ArrayValue final : public Value {
public:
    static constexpr Kind kind = Kind::Array;

public:
    ArrayType* type_;
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
    ~ArrayValue() {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("[{}]", type_->element_type_->repr());
    }
    ArrayType* get_type() const final { return type_; }
    ArrayValue* resolve_to(Type* target) const final {
        /// TODO: implement
        return nullptr;
    }
    void assign_from(Value* source) final {
        /// TODO: implement
        UNREACHABLE();
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class InstanceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Instance;

public:
    ClassType* cls_;
    GlobalMemory::Map<std::string_view, Value*> attributes_;

public:
    InstanceValue(ClassType* cls, decltype(attributes_) attributes) noexcept
        : Value(kind), cls_(cls), attributes_(std::move(attributes)) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("<instance of {}>", cls_->repr());
    }
    ClassType* get_type() const final { return cls_; }
    InstanceValue* resolve_to(Type* target) const final {
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
    void transpile(Transpiler& transpiler) const noexcept final;
};

class ReferenceValue final : public Value {
public:
    static constexpr Kind kind = Kind::Reference;

private:
    ReferenceType* type_;
    Value** referenced_value_;

public:
    ReferenceValue(ReferenceType* type, Value** referenced_value) noexcept
        : Value(kind), type_(type), referenced_value_(referenced_value) {}
    std::string_view repr() const final {
        return GlobalMemory::format_view("&{}", (*referenced_value_)->repr());
    }
    ReferenceType* get_type() const final { return type_; }
    ReferenceValue* resolve_to(Type* target) const final {
        if (target && !target->dyn_cast<ReferenceType>()) {
            throw UnlocatedProblem::make<TypeMismatchError>("reference", target->repr());
        }
        return new ReferenceValue(*this);
    }
    void assign_from(Value* source) final {
        ReferenceValue* ref_source = source->cast<ReferenceValue>();
        referenced_value_ = ref_source->referenced_value_;
    }
    void transpile(Transpiler& transpiler) const noexcept final;
};

class Term : public GlobalMemory::MemoryManaged {
public:
    static Term from_const(Value* value) { return Term(value, false, false); }
    static Term from_var(Type* type, bool is_mutable) { return Term(type, is_mutable, false); }
    static Term from_rvalue(Type* type) { return Term(type, false, true); }
    static Term unknown() { return Term(new UnknownValue(), false, false); }

private:
    Object* ptr_;
    bool is_mutable_ = false;
    bool is_rvalue_ = false;

private:
    Term(Object* ptr, bool is_mutable, bool is_rvalue) noexcept
        : ptr_(ptr), is_mutable_(is_mutable), is_rvalue_(is_rvalue) {
        assert(!(is_mutable && is_rvalue_));
    }

public:
    Term() noexcept = default;
    Type* effective_type() const noexcept {
        if (auto type = ptr_->dyn_type()) {
            return type;
        } else {
            return ptr_->dyn_value()->get_type();
        }
    }
    bool is_const() const noexcept { return ptr_->dyn_value() != nullptr; }
    bool is_mutable() const noexcept { return is_mutable_; }
    bool is_rvalue() const noexcept { return is_rvalue_; }
    Object* operator->() const noexcept { return ptr_; }
    operator bool() const noexcept { return ptr_ != nullptr; }
};

inline Type* Object::dyn_type() {
    if (!is_type_ && kind_ == Kind::NothingOrUnknown) return &UnknownType::instance;
    return is_type_ ? static_cast<Type*>(this) : nullptr;
}

inline Value* Object::dyn_value() {
    if (is_type_ && kind_ == Kind::NothingOrUnknown) return new UnknownValue();
    return !is_type_ ? static_cast<Value*>(this) : nullptr;
}

inline thread_local std::optional<TypeRegistry> TypeRegistry::instance;

inline Type* TypeRegistry::get_unknown() noexcept { return &UnknownType::instance; }

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
