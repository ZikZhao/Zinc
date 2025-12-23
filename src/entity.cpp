#include "entity.hpp"

#include "pch.hpp"
#include <compare>
#include <limits>
#include <utility>

#include "exception.hpp"

Entity::Entity(Kind kind, bool is_value) noexcept
    : ref_count_(is_value ? 0 : std::numeric_limits<decltype(ref_count_)>::min()), kind_(kind) {}

Type::Type(Kind kind) noexcept : Entity(kind, false) {}

std::strong_ordering FunctionType::operator<=>(const FunctionType& other) const noexcept {
    if (auto param_cmp = parameters_.size() <=> other.parameters_.size(); param_cmp != 0) {
        return param_cmp;
    }
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        if (auto cmp = parameters_[i] <=> other.parameters_[i]; cmp != 0) {
            return cmp;
        }
    }
    if (auto spread_cmp = (spread_ == nullptr ? 0 : 1) <=> (other.spread_ == nullptr ? 0 : 1);
        spread_cmp != 0) {
        return spread_cmp;
    } else if (spread_ && other.spread_) {
        if (auto cmp = spread_ <=> other.spread_; cmp != 0) {
            return cmp;
        }
    }
    return return_type_ <=> other.return_type_;
}
bool FunctionType::operator==(const FunctionType& other) const noexcept {
    if (parameters_.size() != other.parameters_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < parameters_.size(); ++i) {
        if (parameters_[i] != other.parameters_[i]) {
            return false;
        }
    }
    if ((spread_ == nullptr) != (other.spread_ == nullptr)) {
        return false;
    } else if (spread_ && other.spread_ && (spread_ != other.spread_)) {
        return false;
    }
    return return_type_ == other.return_type_;
}
bool FunctionType::operator!=(const FunctionType& other) const noexcept {
    return !(*this == other);
}

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

RecordType::RecordType(FlatMap<std::string_view, Type*> fields) noexcept
    : Type(Kind::KIND_RECORD), fields_(std::move(fields)) {}
std::string RecordType::repr() const {
    // TODO
    return {};
}
bool RecordType::contains(const Type& other) const {
    // TODO
    return false;
}
std::strong_ordering RecordType::operator<=>(const RecordType& other) const noexcept {
    if (auto field_cmp = fields_.size() <=> other.fields_.size(); field_cmp != 0) {
        return field_cmp;
    }
    auto it1 = fields_.begin();
    auto it2 = other.fields_.begin();
    for (; it1 != fields_.end() and it2 != other.fields_.end(); ++it1, ++it2) {
        if (auto key_cmp = (*it1).first <=> (*it2).first; key_cmp != 0) {
            return key_cmp;
        }
        if (auto type_cmp = (*it1).second <=> (*it2).second; type_cmp != 0) {
            return type_cmp;
        }
    }
    return std::strong_ordering::equal;
}
bool RecordType::operator==(const RecordType& other) const noexcept {
    if (fields_.size() != other.fields_.size()) {
        return false;
    }
    auto it1 = fields_.begin();
    auto it2 = other.fields_.begin();
    for (; it1 != fields_.end() and it2 != other.fields_.end(); ++it1, ++it2) {
        if ((*it1).first != (*it2).first || (*it1).second != (*it2).second) {
            return false;
        }
    }
    return true;
}

ClassType::ClassType(
    std::string_view name,
    std::span<InterfaceType*> interfaces,
    const ClassType* extends,
    FlatMap<std::string_view, Type*> properties
) noexcept
    : Type(Kind::KIND_CLASS),
      name_(name),
      interfaces_(interfaces),
      extends_(extends),
      properties_(std::move(properties)) {}
std::string ClassType::repr() const { return "class "s + std::string(name_); }
bool ClassType::contains(const Type& other) const {
    // TODO
    return false;
}

std::span<Type*> IntersectionType::combine(Type* left, Type* right) {
    std::size_t size = 0;
    if (right < left) std::swap(left, right);
    if (left->kind_ == Kind::KIND_INTERSECTION) {
        const IntersectionType& left_intersection = static_cast<const IntersectionType&>(*left);
        size += left_intersection.types_.size();
    } else {
        size++;
    }
    if (right->kind_ == Kind::KIND_INTERSECTION) {
        const IntersectionType& right_intersection = static_cast<const IntersectionType&>(*right);
        size += right_intersection.types_.size();
    } else {
        size++;
    }
    std::span<Type*> buffer = GlobalMemory::allocate_array<Type*>(size);
    std::size_t index = 0;
    if (left->kind_ == Kind::KIND_INTERSECTION) {
        const IntersectionType& left_intersection = static_cast<const IntersectionType&>(*left);
        for (const auto& type : left_intersection.types_) {
            buffer[index++] = type;
        }
    } else {
        buffer[index++] = left;
    }
    if (right->kind_ == Kind::KIND_INTERSECTION) {
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
    : Type(Kind::KIND_INTERSECTION), types_{combine(left, right)} {}
std::string IntersectionType::repr() const {
    // TODO
    return {};
}
bool IntersectionType::contains(const Type& other) const {
    // TODO
    return false;
}
std::strong_ordering IntersectionType::operator<=>(const IntersectionType& other) const noexcept {
    if (auto type_cmp = types_.size() <=> other.types_.size(); type_cmp != 0) {
        return type_cmp;
    }
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (auto cmp = types_[i] <=> other.types_[i]; cmp != 0) {
            return cmp;
        }
    }
    return std::strong_ordering::equal;
}
bool IntersectionType::operator==(const IntersectionType& other) const noexcept {
    if (types_.size() != other.types_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (types_[i] != other.types_[i]) {
            return false;
        }
    }
    return true;
}
bool IntersectionType::operator!=(const IntersectionType& other) const noexcept {
    return !(*this == other);
}

std::span<Type*> UnionType::combine(Type* left, Type* right) {
    std::size_t size = 0;
    if (right < left) std::swap(left, right);
    if (left->kind_ == Kind::KIND_UNION) {
        const UnionType& left_union = static_cast<const UnionType&>(*left);
        size += left_union.types_.size();
    } else {
        size++;
    }
    if (right->kind_ == Kind::KIND_UNION) {
        const UnionType& right_union = static_cast<const UnionType&>(*right);
        size += right_union.types_.size();
    } else {
        size++;
    }
    std::span<Type*> buffer = GlobalMemory::allocate_array<Type*>(size);
    std::size_t index = 0;
    if (left->kind_ == Kind::KIND_UNION) {
        const UnionType& left_union = static_cast<const UnionType&>(*left);
        for (const auto& type : left_union.types_) {
            buffer[index++] = type;
        }
    } else {
        buffer[index++] = left;
    }
    if (right->kind_ == Kind::KIND_UNION) {
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
    : Type(Kind::KIND_UNION), types_(combine(left, right)) {}
std::string UnionType::repr() const {
    // TODO
    return {};
}
bool UnionType::contains(const Type& other) const {
    // TODO
    return false;
}
std::strong_ordering UnionType::operator<=>(const UnionType& other) const noexcept {
    if (auto type_cmp = types_.size() <=> other.types_.size(); type_cmp != 0) {
        return type_cmp;
    }
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (auto cmp = types_[i] <=> other.types_[i]; cmp != 0) {
            return cmp;
        }
    }
    return std::strong_ordering::equal;
}
bool UnionType::operator==(const UnionType& other) const noexcept {
    if (types_.size() != other.types_.size()) {
        return false;
    }
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (types_[i] != other.types_[i]) {
            return false;
        }
    }
    return true;
}
bool UnionType::operator!=(const UnionType& other) const noexcept { return !(*this == other); }

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
EntityRef& EntityRef::operator=(EntityRef other) noexcept {
    std::swap(ptr_, other.ptr_);
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
