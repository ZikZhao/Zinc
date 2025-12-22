#include "entity.hpp"

#include "pch.hpp"
#include <limits>
#include <utility>

#include "exception.hpp"

Entity::Entity(Kind kind, bool is_value) noexcept
    : kind_(kind), ref_count_(is_value ? 0 : std::numeric_limits<decltype(ref_count_)>::min()) {}

Type::Type(Kind kind) noexcept : Entity(kind, false) {}

AnyType::AnyType() noexcept : Type(Kind::KIND_ANY) {}
std::string AnyType::repr() const { return "any"; }
bool AnyType::contains(const Type& other) const { return true; }

ListType::ListType(Type* element_type) noexcept
    : Type(Kind::KIND_LIST), element_type_(element_type) {}
std::string ListType::repr() const { return "List<"s + element_type_->repr() + ">"s; }
bool ListType::contains(const Type& other) const {
    if (other.kind_ != this->kind_) {
        return false;
    }
    const ListType& other_list = static_cast<const ListType&>(other);
    return Type::contains(*this->element_type_, *other_list.element_type_);
}

RecordType::RecordType(std::map<std::string, Type*> fields) noexcept
    : Type(Kind::KIND_RECORD), fields_(std::move(fields)) {}
std::string RecordType::repr() const {
    // TODO
    return {};
}
bool RecordType::contains(const Type& other) const {
    // TODO
    return false;
}

ClassType::ClassType(
    std::string_view name,
    std::vector<InterfaceType*> interfaces,
    const ClassType* extends,
    std::map<std::string_view, Type*> properties
) noexcept
    : Type(Kind::KIND_CLASS),
      name_(name),
      interfaces_(std::move(interfaces)),
      extends_(extends),
      properties_(std::move(properties)) {}
std::string ClassType::repr() const { return "class "s + std::string(name_); }
bool ClassType::contains(const Type& other) const {
    // TODO
    return false;
}

std::string IntersectionType::repr() const {
    // TODO
    return {};
}
bool IntersectionType::contains(const Type& other) const {
    // TODO
    return false;
}
IntersectionType& IntersectionType::combine(Type* other) {
    if (other->kind_ == Kind::KIND_INTERSECTION) {
        const IntersectionType& other_intersection = static_cast<const IntersectionType&>(*other);
        this->types_.reserve(this->types_.size() + other_intersection.types_.size());
        for (const auto& type : other_intersection.types_) {
            this->types_.emplace_back(type);
        }
    } else {
        assert(other->kind_ == Kind::KIND_FUNCTION);
        this->types_.emplace_back(other);
    }
    return *this;
}

std::string UnionType::repr() const {
    // TODO
    return {};
}
bool UnionType::contains(const Type& other) const {
    // TODO
    return false;
}
UnionType& UnionType::combine(Type* other) {
    if (other->kind_ == Kind::KIND_UNION) {
        const UnionType& other_union = static_cast<const UnionType&>(*other);
        this->types_.reserve(this->types_.size() + other_union.types_.size());
        for (const auto& type : other_union.types_) {
            this->types_.emplace_back(type);
        }
    } else {
        this->types_.emplace_back(other);
    }
    return *this;
}

Value::Value(Kind kind) noexcept : Entity(kind, true) {}

NullValue::NullValue() noexcept : Value(Kind::KIND_NULL) {}
std::string NullValue::repr() const { return "null"; }

IntegerValue::IntegerValue(int64_t value) noexcept : Value(Kind::KIND_INTEGER), value_(value) {}
std::string IntegerValue::repr() const { return std::to_string(value_); }
IntegerValue* IntegerValue::operator+(const IntegerValue& other) const {
    return new IntegerValue(this->value_ + other.value_);
}
IntegerValue* IntegerValue::operator-(const IntegerValue& other) const {
    return new IntegerValue(this->value_ - other.value_);
}
IntegerValue* IntegerValue::operator-() const { return new IntegerValue(-this->value_); }
IntegerValue* IntegerValue::operator*(const IntegerValue& other) const {
    return new IntegerValue(this->value_ * other.value_);
}
IntegerValue* IntegerValue::operator/(const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value_ / other.value_);
}
IntegerValue* IntegerValue::operator%(const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value_ % other.value_);
}
BooleanValue* IntegerValue::operator==(const IntegerValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* IntegerValue::operator!=(const IntegerValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* IntegerValue::operator<(const IntegerValue& other) const {
    return new BooleanValue(this->value_ < other.value_);
}
BooleanValue* IntegerValue::operator<=(const IntegerValue& other) const {
    return new BooleanValue(this->value_ <= other.value_);
}
BooleanValue* IntegerValue::operator>(const IntegerValue& other) const {
    return new BooleanValue(this->value_ > other.value_);
}
BooleanValue* IntegerValue::operator>=(const IntegerValue& other) const {
    return new BooleanValue(this->value_ >= other.value_);
}
IntegerValue* IntegerValue::operator&(const IntegerValue& other) const {
    return new IntegerValue(this->value_ & other.value_);
}
IntegerValue* IntegerValue::operator|(const IntegerValue& other) const {
    return new IntegerValue(this->value_ | other.value_);
}
IntegerValue* IntegerValue::operator^(const IntegerValue& other) const {
    return new IntegerValue(this->value_ ^ other.value_);
}
IntegerValue* IntegerValue::operator~() const { return new IntegerValue(~this->value_); }
IntegerValue* IntegerValue::operator<<(const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot left shift by negative amount");
    return new IntegerValue(this->value_ << other.value_);
}
IntegerValue* IntegerValue::operator>>(const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot right shift by negative amount");
    return new IntegerValue(this->value_ >> other.value_);
}
IntegerValue* IntegerValue::operator=(const FloatValue& other) const {
    return new IntegerValue(static_cast<std::int64_t>(other.value_));
}

FloatValue::FloatValue(double value) noexcept : Value(Kind::KIND_FLOAT), value_(value) {}
std::string FloatValue::repr() const { return std::to_string(value_); }
FloatValue* FloatValue::operator+(const FloatValue& other) const {
    return new FloatValue(this->value_ + other.value_);
}
FloatValue* FloatValue::operator-(const FloatValue& other) const {
    return new FloatValue(this->value_ - other.value_);
}
FloatValue* FloatValue::operator-() const { return new FloatValue(-this->value_); }
FloatValue* FloatValue::operator*(const FloatValue& other) const {
    return new FloatValue(this->value_ * other.value_);
}
FloatValue* FloatValue::operator/(const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value_ / other.value_);
}
FloatValue* FloatValue::operator%(const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value_, other.value_));
}
BooleanValue* FloatValue::operator==(const FloatValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* FloatValue::operator!=(const FloatValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* FloatValue::operator<(const FloatValue& other) const {
    return new BooleanValue(this->value_ < other.value_);
}
BooleanValue* FloatValue::operator<=(const FloatValue& other) const {
    return new BooleanValue(this->value_ <= other.value_);
}
BooleanValue* FloatValue::operator>(const FloatValue& other) const {
    return new BooleanValue(this->value_ > other.value_);
}
BooleanValue* FloatValue::operator>=(const FloatValue& other) const {
    return new BooleanValue(this->value_ >= other.value_);
}
FloatValue* FloatValue::operator=(const IntegerValue& other) const {
    return new FloatValue(static_cast<double>(other.value_));
}

StringValue::StringValue(std::string value) noexcept
    : Value(Kind::KIND_STRING), value_(std::move(value)) {}
std::string StringValue::repr() const { return "\"" + this->value_ + "\""; }
StringValue* StringValue::operator+(const StringValue& other) const {
    return new StringValue(this->value_ + other.value_);
}
StringValue* StringValue::operator*(const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value_.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        result += this->value_;
    }
    return new StringValue(std::move(result));
}
BooleanValue* StringValue::operator==(const StringValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* StringValue::operator!=(const StringValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}

BooleanValue::BooleanValue(bool value) noexcept : Value(Kind::KIND_BOOLEAN), value_(value) {}
std::string BooleanValue::repr() const { return this->value_ ? "true" : "false"; }
BooleanValue* BooleanValue::operator==(const BooleanValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* BooleanValue::operator!=(const BooleanValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* BooleanValue::operator and(const BooleanValue& other) const {
    return new BooleanValue(this->value_ && other.value_);
}
BooleanValue* BooleanValue::operator or(const BooleanValue& other) const {
    return new BooleanValue(this->value_ || other.value_);
}
BooleanValue* BooleanValue::operator not() const { return new BooleanValue(!this->value_); }

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

ObjectValue::ObjectValue(ClassType* cls) noexcept : Value(Kind::KIND_OBJECT), cls_(cls) {}

EntityRef::EntityRef(const EntityRef& other) noexcept : ptr_(other.ptr_) { retain(); }
EntityRef::EntityRef(EntityRef&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }
EntityRef::~EntityRef() noexcept { release(); }
EntityRef& EntityRef::operator=(const EntityRef& other) noexcept {
    if (this != &other) {
        release();
        ptr_ = other.ptr_;
        retain();
    }
    return *this;
}
EntityRef& EntityRef::operator=(EntityRef&& other) noexcept {
    if (this != &other) {
        release();
        ptr_ = other.ptr_;
        other.ptr_ = nullptr;
    }
    return *this;
}

EntityRef::operator bool() const noexcept { return ptr_ != nullptr; }
Entity& EntityRef::operator*() const noexcept { return *ptr_; }
Entity* EntityRef::operator->() const noexcept { return ptr_; }
Value* EntityRef::value() const noexcept {
    assert(ptr_ && !ptr_->is_type());
    return static_cast<Value*>(ptr_);
}
Type* EntityRef::type() const noexcept {
    assert(ptr_ && ptr_->is_type());
    return static_cast<Type*>(ptr_);
}
EntityRef::EntityRef(Entity* ptr) noexcept : ptr_(ptr) { retain(); }
void EntityRef::retain() noexcept {
    if (ptr_) {
        ptr_->ref_count_++;
    }
}
void EntityRef::release() noexcept {
    if (ptr_) {
        ptr_->ref_count_--;
        if (ptr_->ref_count_ == 0) {
            delete ptr_;
        }
    }
}
