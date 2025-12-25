#include "object.hpp"

#include "pch.hpp"
#include <limits>
#include <utility>

#include "exception.hpp"

Object::Object(Kind kind, bool is_value) noexcept
    : ref_count_(is_value ? 0 : std::numeric_limits<decltype(ref_count_)>::min()), kind_(kind) {}

Type::Type(Kind kind) noexcept : Object(kind, false) {}
bool Type::assignable_from(const Type& source) const {
    assert(
        this == &source ? assignable_from_impl(source) : true
    );  // assignable_from_impl(source) must be true for identical types
    return this == &source || assignable_from_impl(source);
}

std::string FunctionType::repr() const {
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
bool FunctionType::assignable_from_impl(const Type& source) const {
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

ListType::ListType(Type* element_type) noexcept : Type(Kind::List), element_type_(element_type) {}
std::string ListType::repr() const { return "List<"s + element_type_->repr() + ">"s; }
bool ListType::assignable_from_impl(const Type& source) const {
    if (source.kind_ != this->kind_) {
        return false;
    }
    const ListType& other_list = static_cast<const ListType&>(source);
    return element_type_->assignable_from(*other_list.element_type_);
}

RecordType::RecordType(FlatMap<std::string_view, Type*> fields) noexcept
    : Type(Kind::Record), fields_(std::move(fields)) {}
std::string RecordType::repr() const {
    // TODO
    return {};
}
bool RecordType::assignable_from_impl(const Type& source) const {
    // (a,b,c) is assignable to (a,b)
    // i.e., source must have at least all fields of this
    if (source.kind_ != this->kind_) {
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

ClassType::ClassType(
    std::string_view name,
    ComparableSpan<InterfaceType*> interfaces,
    const ClassType* extends,
    FlatMap<std::string_view, Type*> properties
) noexcept
    : Type(Kind::Class),
      name_(name),
      interfaces_(interfaces),
      extends_(extends),
      properties_(std::move(properties)) {}
std::string ClassType::repr() const { return "class "s + std::string(name_); }
bool ClassType::assignable_from_impl(const Type& other) const { return false; }

ComparableSpan<Type*> IntersectionType::combine(Type* left, Type* right) {
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
        const IntersectionType& right_intersection = static_cast<const IntersectionType&>(*right);
        size += right_intersection.types_.size();
    } else {
        assert(right->kind_ == Kind::Function);
        size++;
    }
    ComparableSpan<Type*> buffer = GlobalMemory::allocate_array<Type*>(size);
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
        const IntersectionType& right_intersection = static_cast<const IntersectionType&>(*right);
        for (const auto& type : right_intersection.types_) {
            buffer[index++] = type;
        }
    } else {
        buffer[index++] = right;
    }
    return buffer;
}
IntersectionType::IntersectionType(Type* left, Type* right) noexcept
    : Type(Kind::Intersection), types_{combine(left, right)} {}
std::string IntersectionType::repr() const {
    // TODO
    return {};
}
bool IntersectionType::assignable_from_impl(const Type& source) const {
    // (a & b & c) is assignable to (a & b)
    // i.e., source supports at least all the function overloads of this
    if (source.kind_ == Kind::Intersection) {
        const IntersectionType& other_intersection = static_cast<const IntersectionType&>(source);
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

ComparableSpan<Type*> UnionType::combine(Type* left, Type* right) {
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
    ComparableSpan<Type*> buffer = GlobalMemory::allocate_array<Type*>(size);
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
UnionType::UnionType(Type* left, Type* right) noexcept
    : Type(Kind::Union), types_(combine(left, right)) {}
std::string UnionType::repr() const {
    // TODO
    return {};
}
bool UnionType::assignable_from_impl(const Type& source) const {
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

Value::Value(Kind kind) noexcept : Object(kind, true) {}

NullValue::NullValue() noexcept : Value(Kind::Null) {}
std::string NullValue::repr() const { return "null"; }

IntegerValue::IntegerValue(int64_t value) noexcept : Value(Kind::Integer), value_(value) {}
std::string IntegerValue::repr() const { return std::to_string(value_); }
IntegerValue IntegerValue::operator+(const IntegerValue& other) const {
    return IntegerValue(this->value_ + other.value_);
}
IntegerValue IntegerValue::operator-(const IntegerValue& other) const {
    return IntegerValue(this->value_ - other.value_);
}
IntegerValue IntegerValue::operator-() const { return IntegerValue(-this->value_); }
IntegerValue IntegerValue::operator*(const IntegerValue& other) const {
    return IntegerValue(this->value_ * other.value_);
}
IntegerValue IntegerValue::operator/(const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return IntegerValue(this->value_ / other.value_);
}
IntegerValue IntegerValue::operator%(const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return IntegerValue(this->value_ % other.value_);
}
BooleanValue IntegerValue::operator==(const IntegerValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}
BooleanValue IntegerValue::operator!=(const IntegerValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}
BooleanValue IntegerValue::operator<(const IntegerValue& other) const {
    return BooleanValue(this->value_ < other.value_);
}
BooleanValue IntegerValue::operator<=(const IntegerValue& other) const {
    return BooleanValue(this->value_ <= other.value_);
}
BooleanValue IntegerValue::operator>(const IntegerValue& other) const {
    return BooleanValue(this->value_ > other.value_);
}
BooleanValue IntegerValue::operator>=(const IntegerValue& other) const {
    return BooleanValue(this->value_ >= other.value_);
}
IntegerValue IntegerValue::operator&(const IntegerValue& other) const {
    return IntegerValue(this->value_ & other.value_);
}
IntegerValue IntegerValue::operator|(const IntegerValue& other) const {
    return IntegerValue(this->value_ | other.value_);
}
IntegerValue IntegerValue::operator^(const IntegerValue& other) const {
    return IntegerValue(this->value_ ^ other.value_);
}
IntegerValue IntegerValue::operator~() const { return IntegerValue(~this->value_); }
IntegerValue IntegerValue::operator<<(const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot left shift by negative amount");
    return IntegerValue(this->value_ << other.value_);
}
IntegerValue IntegerValue::operator>>(const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot right shift by negative amount");
    return IntegerValue(this->value_ >> other.value_);
}

FloatValue::FloatValue(double value) noexcept : Value(Kind::Float), value_(value) {}
std::string FloatValue::repr() const { return std::to_string(value_); }
FloatValue FloatValue::operator+(const FloatValue& other) const {
    return FloatValue(this->value_ + other.value_);
}
FloatValue FloatValue::operator-(const FloatValue& other) const {
    return FloatValue(this->value_ - other.value_);
}
FloatValue FloatValue::operator-() const { return FloatValue(-this->value_); }
FloatValue FloatValue::operator*(const FloatValue& other) const {
    return FloatValue(this->value_ * other.value_);
}
FloatValue FloatValue::operator/(const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return FloatValue(this->value_ / other.value_);
}
FloatValue FloatValue::operator%(const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return FloatValue(std::fmod(this->value_, other.value_));
}
BooleanValue FloatValue::operator==(const FloatValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}
BooleanValue FloatValue::operator!=(const FloatValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}
BooleanValue FloatValue::operator<(const FloatValue& other) const {
    return BooleanValue(this->value_ < other.value_);
}
BooleanValue FloatValue::operator<=(const FloatValue& other) const {
    return BooleanValue(this->value_ <= other.value_);
}
BooleanValue FloatValue::operator>(const FloatValue& other) const {
    return BooleanValue(this->value_ > other.value_);
}
BooleanValue FloatValue::operator>=(const FloatValue& other) const {
    return BooleanValue(this->value_ >= other.value_);
}

StringValue::StringValue(std::string value) noexcept
    : Value(Kind::String), value_(std::move(value)) {}
std::string StringValue::repr() const { return "\"" + this->value_ + "\""; }
StringValue StringValue::operator+(const StringValue& other) const {
    return StringValue(this->value_ + other.value_);
}
StringValue StringValue::operator*(const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value_.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        result += this->value_;
    }
    return StringValue(std::move(result));
}
BooleanValue StringValue::operator==(const StringValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}
BooleanValue StringValue::operator!=(const StringValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}

BooleanValue::BooleanValue(bool value) noexcept : Value(Kind::Boolean), value_(value) {}
std::string BooleanValue::repr() const { return this->value_ ? "true" : "false"; }
BooleanValue BooleanValue::operator==(const BooleanValue& other) const {
    return BooleanValue(this->value_ == other.value_);
}
BooleanValue BooleanValue::operator!=(const BooleanValue& other) const {
    return BooleanValue(this->value_ != other.value_);
}
BooleanValue BooleanValue::operator and(const BooleanValue& other) const {
    return BooleanValue(this->value_ && other.value_);
}
BooleanValue BooleanValue::operator or(const BooleanValue& other) const {
    return BooleanValue(this->value_ || other.value_);
}
BooleanValue BooleanValue::operator not() const { return BooleanValue(!this->value_); }

std::string FunctionValue::repr() const {
    return std::format("<function at {:p}>", static_cast<const void*>(this));
}
Value* FunctionValue::operator()(const std::vector<Value*>& args) const {
    try {
        return callback_(args);
    } catch (ReturnException<Value*>& e) {
        return e.value_;
    }
    std::unreachable();
}

InstanceValue::InstanceValue(ClassType* cls) noexcept : Value(Kind::Instance), cls_(cls) {}

ObjectRef::ObjectRef(const ObjectRef& other) noexcept : ptr_(other.ptr_) { retain(); }
ObjectRef::ObjectRef(ObjectRef&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
ObjectRef::~ObjectRef() noexcept { release(); }
ObjectRef& ObjectRef::operator=(ObjectRef other) noexcept {
    std::swap(ptr_, other.ptr_);
    return *this;
}
ObjectRef::operator bool() const noexcept { return ptr_ != nullptr; }
Object& ObjectRef::operator*() const noexcept { return *ptr_; }
Object* ObjectRef::operator->() const noexcept { return ptr_; }
Value* ObjectRef::value() const noexcept {
    assert(ptr_ && !ptr_->is_type());
    return static_cast<Value*>(ptr_);
}
Type* ObjectRef::type() const noexcept {
    assert(ptr_ && ptr_->is_type());
    return static_cast<Type*>(ptr_);
}
ObjectRef::ObjectRef(Object* ptr) noexcept : ptr_(ptr) { retain(); }
void ObjectRef::retain() noexcept {
    if (ptr_) {
        ptr_->ref_count_++;
    }
}
void ObjectRef::release() noexcept {
    if (ptr_) {
        ptr_->ref_count_--;
        if (ptr_->ref_count_ == 0) {
            delete ptr_;
        }
    }
}
