#include <cmath>
#include "value.hpp"
#include "type.hpp"
#include "ref.hpp"
#include "out/parser.tab.hpp"

Value::Value(const TypeRef& type) : type(type) {}

BooleanValue* Value::ObjectIs(const Value& left, const Value& right) {
    return new BooleanValue(&left == &right);
}
ValueRef Value::FromLiteral(int64_t type, const char* literal) {
    using namespace yy;
    using enum parser::token::token_kind_type;
    switch (type) {
    case T_INTEGER:
        return std::shared_ptr<Value>(new IntegerValue(std::stoll(literal)));
    case T_FLOAT:
        return std::shared_ptr<Value>(new FloatValue(std::stod(literal)));
    case T_STRING:
        return std::shared_ptr<Value>(new StringValue(std::string(literal)));
    case T_BOOLEAN:
        if (std::string(literal) == "true") {
            return std::shared_ptr<Value>(new BooleanValue(true));
        } else if (std::string(literal) == "false") {
            return std::shared_ptr<Value>(new BooleanValue(false));
        } else {
            throw std::runtime_error("Invalid boolean literal: " + std::string(literal));
        }
    case T_NULL:
        return NullValue::Instance;
    default:
        throw std::runtime_error("Unknown literal type");
    }
}

const TypeRef NullValue::TypeInstance(std::shared_ptr<Type>(new NullType()));
const ValueRef NullValue::Instance(std::shared_ptr<Value>(new NullValue()));
NullValue::NullValue() : Value(TypeInstance) {}
NullValue::operator std::string () const {
    return "null";
}

const TypeRef IntegerValue::TypeInstance(std::shared_ptr<Type>(new IntegerType()));
IntegerValue::IntegerValue(int64_t value) : Value(TypeInstance), value(value) {}
Value* IntegerValue::operator + (const Value& other) const {
    return other + *this;
}
Value* IntegerValue::operator + (const IntegerValue& other) const {
    return new IntegerValue(this->value + other.value);
}
Value* IntegerValue::operator + (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) + other.value);
}
Value* IntegerValue::operator - (const Value& other) const {
    return *(-other) + *this;
}
Value* IntegerValue::operator - (const IntegerValue& other) const {
    return new IntegerValue(this->value - other.value);
}
Value* IntegerValue::operator - (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) - other.value);
}
Value* IntegerValue::operator - () const {
    return new IntegerValue(-this->value);
}
Value* IntegerValue::operator * (const Value& other) const {
    return other * *this;
}
Value* IntegerValue::operator * (const IntegerValue& other) const {
    return new IntegerValue(this->value * other.value);
}
Value* IntegerValue::operator * (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) * other.value);
}
Value* IntegerValue::operator / (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator/(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator/(*float_val);
    } else {
        throw std::runtime_error("Division not implemented for this type");
    }
}
Value* IntegerValue::operator / (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(static_cast<double>(this->value) / static_cast<double>(other.value));
}
Value* IntegerValue::operator / (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(static_cast<double>(this->value) / other.value);
}
Value* IntegerValue::operator % (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator%(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator%(*float_val);
    } else {
        throw std::runtime_error("Remainder not implemented for this type");
    }
}
Value* IntegerValue::operator % (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value % other.value);
}
Value* IntegerValue::operator % (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(static_cast<double>(this->value), other.value));
}
Value* IntegerValue::operator < (const Value& other) const {
    return other > *this;
}
Value* IntegerValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value < other.value);
}
Value* IntegerValue::operator < (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) < other.value);
}
Value* IntegerValue::operator <= (const Value& other) const {
    return other >= *this;
}
Value* IntegerValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
Value* IntegerValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) <= other.value);
}
Value* IntegerValue::operator > (const Value& other) const {
    return other < *this;
}
Value* IntegerValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value > other.value);
}
Value* IntegerValue::operator > (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) > other.value);
}
Value* IntegerValue::operator >= (const Value& other) const {
    return other <= *this;
}
Value* IntegerValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value >= other.value);
}
Value* IntegerValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) >= other.value);
}
Value* IntegerValue::operator == (const Value& other) const {
    return other.operator==(*this);
}
Value* IntegerValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value == other.value);
}
Value* IntegerValue::operator == (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) == other.value);
}
Value* IntegerValue::operator != (const Value& other) const {
    return other.operator!=(*this);
}
Value* IntegerValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value != other.value);
}
Value* IntegerValue::operator != (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) != other.value);
}
IntegerValue::operator std::string () const {
    return std::to_string(value);
}

const TypeRef FloatValue::TypeInstance(std::shared_ptr<Type>(new FloatType()));
FloatValue::FloatValue(double value) : Value(TypeInstance), value(value) {}
Value* FloatValue::operator + (const Value& other) const {
    return other + *this;
}
Value* FloatValue::operator + (const IntegerValue& other) const {
    return new FloatValue(this->value + static_cast<double>(other.value));
}
Value* FloatValue::operator + (const FloatValue& other) const {
    return new FloatValue(this->value + other.value);
}
Value* FloatValue::operator - (const Value& other) const {
    return *(-other) + *this;
}
Value* FloatValue::operator - (const IntegerValue& other) const {
    return new FloatValue(this->value - static_cast<double>(other.value));
}
Value* FloatValue::operator - (const FloatValue& other) const {
    return new FloatValue(this->value - other.value);
}
Value* FloatValue::operator - () const {
    return new FloatValue(-this->value);
}
Value* FloatValue::operator * (const Value& other) const {
    return other * *this;
}
Value* FloatValue::operator * (const IntegerValue& other) const {
    return new FloatValue(this->value * static_cast<double>(other.value));
}
Value* FloatValue::operator * (const FloatValue& other) const {
    return new FloatValue(this->value * other.value);
}
Value* FloatValue::operator / (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator/(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator/(*float_val);
    } else {
        throw std::runtime_error("Division not implemented for this type");
    }
}
Value* FloatValue::operator / (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value / static_cast<double>(other.value));
}
Value* FloatValue::operator / (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value / other.value);
}
Value* FloatValue::operator % (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator%(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator%(*float_val);
    } else {
        throw std::runtime_error("Remainder not implemented for this type");
    }
}
Value* FloatValue::operator % (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value, static_cast<double>(other.value)));
}
Value* FloatValue::operator % (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value, other.value));
}
Value* FloatValue::operator < (const Value& other) const {
    return other > *this;
}
Value* FloatValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value < static_cast<double>(other.value));
}
Value* FloatValue::operator < (const FloatValue& other) const {
    return new BooleanValue(this->value < other.value);
}
Value* FloatValue::operator <= (const Value& other) const {
    return other >= *this;
}
Value* FloatValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value <= static_cast<double>(other.value));
}
Value* FloatValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
Value* FloatValue::operator > (const Value& other) const {
    return other < *this;
}
Value* FloatValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value > static_cast<double>(other.value));
}
Value* FloatValue::operator > (const FloatValue& other) const {
    return new BooleanValue(this->value > other.value);
}
Value* FloatValue::operator >= (const Value& other) const {
    return other <= *this;
}
Value* FloatValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value >= static_cast<double>(other.value));
}
Value* FloatValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(this->value >= other.value);
}
Value* FloatValue::operator == (const Value& other) const {
    return other.operator==(*this);
}
Value* FloatValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value == static_cast<double>(other.value));
}
Value* FloatValue::operator == (const FloatValue& other) const {
    return new BooleanValue(this->value == other.value);
}
Value* FloatValue::operator != (const Value& other) const {
    return other.operator!=(*this);
}
Value* FloatValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value != static_cast<double>(other.value));
}
Value* FloatValue::operator != (const FloatValue& other) const {
    return new BooleanValue(this->value != other.value);
}
FloatValue::operator std::string () const {
    return std::to_string(value);
}

const TypeRef StringValue::TypeInstance(std::shared_ptr<Type>(new StringType()));
StringValue::StringValue(std::string&& value) : Value(TypeInstance), value(std::forward<std::string>(value)) {}
Value* StringValue::operator + (const Value& other) const {
    return other + *this;
}
Value* StringValue::operator + (const StringValue& other) const {
    return new StringValue(this->value + other.value);
}
Value* StringValue::operator * (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator*(*int_val);
    } else {
        throw std::runtime_error("Multiplication not implemented for this type");
    }
}
Value* StringValue::operator * (const IntegerValue& other) const {
    if (other.value <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value.size() * other.value);
    for (uint64_t i = 0; i < other.value; i++) {
        result += this->value;
    }
    return new StringValue(std::move(result));
}
StringValue::operator std::string () const {
    return "\"" + value + "\"";
}

const TypeRef BooleanValue::TypeInstance(std::shared_ptr<Type>(new BooleanType()));
BooleanValue::BooleanValue(bool value) : Value(TypeInstance), value(value) {}
Value* BooleanValue::operator and (const Value& other) const {
    return other and *this;
}
Value* BooleanValue::operator and (const BooleanValue& other) const {
    return new BooleanValue(this->value and other.value);
}
Value* BooleanValue::operator or (const Value& other) const {
    return other or *this;
}
Value* BooleanValue::operator or (const BooleanValue& other) const {
    return new BooleanValue(this->value or other.value);
}
Value* BooleanValue::operator not () const {
    return new BooleanValue(not this->value);
}
BooleanValue::operator std::string () const {
    return this->value ? "true" : "false";
}

Variable::Variable() : ref(NullValue::Instance) {}
Variable& Variable::operator = (const ValueRef& other) {
    ref = other;
    return *this;
}

