#include <cmath>
#include "value.hpp"
#include "type.hpp"
#include "ref.hpp"
#include "out/parser.tab.hpp"

BooleanValue* Value::ObjectIs(const Value& left, const Value& right) {
    return new BooleanValue(&left == &right);
}
Value* Value::FromLiteral(LiteralType type, std::string_view literal) {
    using namespace yy;
    switch (type) {
    case LITERAL_NULL:
        return new NullValue();
    case LITERAL_INTEGER:
        return new IntegerValue(std::stoll(literal.data()));
    case LITERAL_FLOAT:
        return new FloatValue(std::stod(literal.data()));
    case LITERAL_STRING:
        return new StringValue(std::string(literal.data()));
    case LITERAL_BOOLEAN:
        if (literal == "true") {
            return new BooleanValue(true);
        } else if (literal == "false") {
            return new BooleanValue(false);
        } else {
            throw std::runtime_error("Invalid boolean literal: "s + literal.data());
        }
    default:
        throw std::runtime_error("Unknown literal type");
    }
}

NullValue* NullValue::adapt_for_assignment(const Value& other) const {
    if (dynamic_cast<const NullValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot assign non-null value to null");
    }
}
NullValue::operator std::string () const {
    return "null";
}

IntegerValue::IntegerValue(int64_t value) : value(value) {}
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
bool IntegerValue::is_truthy() const {
    return this->value != 0;
}
IntegerValue* IntegerValue::adapt_for_assignment(const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return nullptr;
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return new IntegerValue(static_cast<int64_t>(float_val->value));
    } else {
        throw std::runtime_error("Cannot adapt to Integer");
    }
}
IntegerValue::operator std::string () const {
    return std::to_string(value);
}

FloatValue::FloatValue(double value) : value(value) {}
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
bool FloatValue::is_truthy() const {
    return this->value != 0.0;
}
FloatValue* FloatValue::adapt_for_assignment(const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return new FloatValue(static_cast<double>(int_val->value));
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to Float");
    }
}
FloatValue::operator std::string () const {
    return std::to_string(value);
}

StringValue::StringValue(std::string&& value) : value(std::forward<std::string>(value)) {}
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
bool StringValue::is_truthy() const {
    return not this->value.empty();
}
StringValue* StringValue::adapt_for_assignment(const Value& other) const {
    if (auto str_val = dynamic_cast<const StringValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to String");
    }
}
StringValue::operator std::string () const {
    return "\"" + value + "\"";
}

BooleanValue::BooleanValue(bool value) : value(value) {}
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
bool BooleanValue::is_truthy() const {
    return this->value;
}
BooleanValue* BooleanValue::adapt_for_assignment(const Value& other) const {
    if (auto bool_val = dynamic_cast<const BooleanValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to Boolean");
    }
}
BooleanValue::operator std::string () const {
    return this->value ? "true" : "false";
}
