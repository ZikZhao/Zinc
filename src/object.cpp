#include "object.hpp"

#include "pch.hpp"

#include "exception.hpp"

TypeOrValue::TypeOrValue(Kind kind, bool is_value) : kind_(kind), is_value_(is_value) {}
bool TypeOrValue::contains(const TypeOrValue& other) const {
    if (is_value_ or other.is_value_) {
        throw std::runtime_error("type expected, got value");
    }
    return static_cast<const Type*>(this)->contains(*static_cast<const Type*>(&other));
}

TypeRef Type::FromTypeIndex(const std::type_index& type) {
    if (type == typeid(NullValue)) {
        return new NullType();
    } else if (type == typeid(IntegerValue)) {
        return new IntegerType();
    } else if (type == typeid(FloatValue)) {
        return new FloatType();
    } else if (type == typeid(StringValue)) {
        return new StringType();
    } else if (type == typeid(BooleanValue)) {
        return new BooleanType();
    } else {
        throw std::runtime_error("Unknown type index: "s + type.name());
    }
}
Type::Type(Kind kind) : TypeOrValue(kind, false) {}

AnyType::AnyType() : Type(Kind::KIND_ANY) {}
std::string AnyType::repr() const { return "any"; }
bool AnyType::contains(const Type& other) const { return true; }

ListType::ListType(TypeRef element_type) : Type(Kind::KIND_LIST), element_type_(element_type) {}
std::string ListType::repr() const { return "List<"s + element_type_->repr() + ">"s; }
bool ListType::contains(const Type& other) const {
    if (other.kind_ != this->kind_) {
        return false;
    }
    const ListType& other_list = static_cast<const ListType&>(other);
    return this->element_type_->contains(*other_list.element_type_);
}

RecordType::RecordType(std::map<std::string, TypeRef> fields)
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
    const std::vector<InterfaceTypeRef>& interfaces,
    const ClassTypeRef extends,
    const Context* properties
)
    : Type(Kind::KIND_CLASS),
      name_(name),
      interfaces_(interfaces),
      extends_(extends),
      properties_(properties) {}
std::string ClassType::repr() const { return "class "s + std::string(name_); }
bool ClassType::contains(const Type& other) const {
    // TODO
    return false;
}

IntersectionType::IntersectionType(std::vector<TypeRef>&& types)
    : Type(Kind::KIND_INTERSECTION), types_(std::move(types)) {}
std::string IntersectionType::repr() const {
    // TODO
    return {};
}
bool IntersectionType::contains(const Type& other) const {
    // TODO
    return false;
}

UnionType::UnionType(std::vector<TypeRef>&& types)
    : Type(Kind::KIND_UNION), types_(std::move(types)) {}
std::string UnionType::repr() const {
    // TODO
    return {};
}
bool UnionType::contains(const Type& other) const {
    // TODO
    return false;
}

Value::Value(Kind kind) : TypeOrValue(kind, true) {}

NullValue::NullValue() : Value(Kind::KIND_NULL) {}
std::string NullValue::repr() const { return "null"; }

IntegerValue::IntegerValue(int64_t value) : Value(Kind::KIND_INTEGER), value_(value) {}
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

FloatValue::FloatValue(double value) : Value(Kind::KIND_FLOAT), value_(value) {}
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

StringValue::StringValue(std::string&& value)
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

BooleanValue::BooleanValue(bool value) : Value(Kind::KIND_BOOLEAN), value_(value) {}
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
ValueRef FunctionValue::operator()(const Arguments& args) const {
    try {
        return callback_(args);
    } catch (ReturnException<ValueRef>& e) {
        return e.value_;
    }
    std::unreachable();
}

ObjectValue::ObjectValue(TypeRef cls) : Value(Kind::KIND_OBJECT), class_type_(cls) {}

ValueRef ListValue::ListClassInstance =
    new ClassType("list"sv, std::vector<ValueRef>{}, {}, nullptr);
ValueRef ListValue::Append(const std::vector<ValueRef>& args) {
    try {
        auto self = args.at(0);
        auto value = args.at(1);
        if (auto list_val = dynamic_cast<ListValue*>(&*self)) {
            list_val->values_.push_back(value);
            return new NullValue();
        }
        throw;
    } catch (const std::out_of_range& e) {
        throw ArgumentException(e.what());
    }
}
ListValue::ListValue() : ObjectValue(ListClassInstance), values_() {}
ListValue::ListValue(std::vector<ValueRef>&& values)
    : ObjectValue(ListClassInstance), values_(std::forward<std::vector<ValueRef>>(values)) {}
std::string ListValue::repr() const {
    std::string result = "[";
    for (size_t i = 0; i < this->values_.size(); i++) {
        result += this->values_[i]->repr();
        if (i < this->values_.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}
ListValue* ListValue::operator+(const ListValue& other) const {
    std::vector<ValueRef> new_values = this->values_;
    // new_values.insert(new_values.end(), other.values_.begin(), other.values_.end());
    return new ListValue(std::move(new_values));
}
ListValue* ListValue::operator*(const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply list by positive integer");
    std::vector<ValueRef> new_values;
    new_values.reserve(this->values_.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        // new_values.insert(new_values.end(), this->values_.begin(), this->values_.end());
    }
    return new ListValue(std::move(new_values));
}
ValueRef ListValue::operator[](const Slice& indices) const {
    const auto& [start_opt, end_opt, step_opt] = indices;
    int64_t start = start_opt ? start_opt->value_ : 0;
    int64_t end = end_opt ? end_opt->value_ : static_cast<int64_t>(this->values_.size());
    int64_t step = step_opt ? step_opt->value_ : 1;
    if (start < 0) start += this->values_.size();
    if (end < 0) end += this->values_.size();
    if (start < 0 || static_cast<uint64_t>(start) > this->values_.size() || end < start ||
        static_cast<size_t>(end) > this->values_.size()) {
        throw std::runtime_error("List index out of range");
    }
    return new ListValue(
        std::vector<ValueRef>(this->values_.begin() + start, this->values_.begin() + end)
    );
}
