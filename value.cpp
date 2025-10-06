#include "pch.hpp"
#include "value.hpp"
#include "type.hpp"
#include "ref.hpp"
#include "out/parser.tab.hpp"

ValueRef Value::ObjectIs(const Value& left, const Value& right) {
    return new BooleanValue(&left == &right);
}
ValueRef Value::FromLiteral(LiteralType type, std::string_view literal) {
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
ValueRef IntegerValue::operator + (const Value& other) const {
    return other + *this;
}
ValueRef IntegerValue::operator + (const IntegerValue& other) const {
    return new IntegerValue(this->value + other.value);
}
ValueRef IntegerValue::operator + (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) + other.value);
}
ValueRef IntegerValue::operator - (const Value& other) const {
    return *(-other) + *this;
}
ValueRef IntegerValue::operator - (const IntegerValue& other) const {
    return new IntegerValue(this->value - other.value);
}
ValueRef IntegerValue::operator - (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) - other.value);
}
ValueRef IntegerValue::operator - () const {
    return new IntegerValue(-this->value);
}
ValueRef IntegerValue::operator * (const Value& other) const {
    return other * *this;
}
ValueRef IntegerValue::operator * (const IntegerValue& other) const {
    return new IntegerValue(this->value * other.value);
}
ValueRef IntegerValue::operator * (const FloatValue& other) const {
    return new FloatValue(static_cast<double>(this->value) * other.value);
}
ValueRef IntegerValue::operator / (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator/(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator/(*float_val);
    } else {
        throw std::runtime_error("Division not implemented for this type");
    }
}
ValueRef IntegerValue::operator / (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(static_cast<double>(this->value) / static_cast<double>(other.value));
}
ValueRef IntegerValue::operator / (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(static_cast<double>(this->value) / other.value);
}
ValueRef IntegerValue::operator % (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator%(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator%(*float_val);
    } else {
        throw std::runtime_error("Remainder not implemented for this type");
    }
}
ValueRef IntegerValue::operator % (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value % other.value);
}
ValueRef IntegerValue::operator % (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(static_cast<double>(this->value), other.value));
}
ValueRef IntegerValue::operator < (const Value& other) const {
    return other > *this;
}
ValueRef IntegerValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value < other.value);
}
ValueRef IntegerValue::operator < (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) < other.value);
}
ValueRef IntegerValue::operator <= (const Value& other) const {
    return other >= *this;
}
ValueRef IntegerValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
ValueRef IntegerValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) <= other.value);
}
ValueRef IntegerValue::operator > (const Value& other) const {
    return other < *this;
}
ValueRef IntegerValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value > other.value);
}
ValueRef IntegerValue::operator > (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) > other.value);
}
ValueRef IntegerValue::operator >= (const Value& other) const {
    return other <= *this;
}
ValueRef IntegerValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value >= other.value);
}
ValueRef IntegerValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) >= other.value);
}
ValueRef IntegerValue::operator == (const Value& other) const {
    return other.operator==(*this);
}
ValueRef IntegerValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value == other.value);
}
ValueRef IntegerValue::operator == (const FloatValue& other) const {
    return new BooleanValue(static_cast<double>(this->value) == other.value);
}
ValueRef IntegerValue::operator != (const Value& other) const {
    return other.operator!=(*this);
}
ValueRef IntegerValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value != other.value);
}
ValueRef IntegerValue::operator != (const FloatValue& other) const {
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
ValueRef FloatValue::operator + (const Value& other) const {
    return other + *this;
}
ValueRef FloatValue::operator + (const IntegerValue& other) const {
    return new FloatValue(this->value + static_cast<double>(other.value));
}
ValueRef FloatValue::operator + (const FloatValue& other) const {
    return new FloatValue(this->value + other.value);
}
ValueRef FloatValue::operator - (const Value& other) const {
    return *(-other) + *this;
}
ValueRef FloatValue::operator - (const IntegerValue& other) const {
    return new FloatValue(this->value - static_cast<double>(other.value));
}
ValueRef FloatValue::operator - (const FloatValue& other) const {
    return new FloatValue(this->value - other.value);
}
ValueRef FloatValue::operator - () const {
    return new FloatValue(-this->value);
}
ValueRef FloatValue::operator * (const Value& other) const {
    return other * *this;
}
ValueRef FloatValue::operator * (const IntegerValue& other) const {
    return new FloatValue(this->value * static_cast<double>(other.value));
}
ValueRef FloatValue::operator * (const FloatValue& other) const {
    return new FloatValue(this->value * other.value);
}
ValueRef FloatValue::operator / (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator/(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator/(*float_val);
    } else {
        throw std::runtime_error("Division not implemented for this type");
    }
}
ValueRef FloatValue::operator / (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value / static_cast<double>(other.value));
}
ValueRef FloatValue::operator / (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value / other.value);
}
ValueRef FloatValue::operator % (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator%(*int_val);
    } else if (auto float_val = dynamic_cast<const FloatValue*>(&other)) {
        return this->operator%(*float_val);
    } else {
        throw std::runtime_error("Remainder not implemented for this type");
    }
}
ValueRef FloatValue::operator % (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value, static_cast<double>(other.value)));
}
ValueRef FloatValue::operator % (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value, other.value));
}
ValueRef FloatValue::operator < (const Value& other) const {
    return other > *this;
}
ValueRef FloatValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value < static_cast<double>(other.value));
}
ValueRef FloatValue::operator < (const FloatValue& other) const {
    return new BooleanValue(this->value < other.value);
}
ValueRef FloatValue::operator <= (const Value& other) const {
    return other >= *this;
}
ValueRef FloatValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value <= static_cast<double>(other.value));
}
ValueRef FloatValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
ValueRef FloatValue::operator > (const Value& other) const {
    return other < *this;
}
ValueRef FloatValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value > static_cast<double>(other.value));
}
ValueRef FloatValue::operator > (const FloatValue& other) const {
    return new BooleanValue(this->value > other.value);
}
ValueRef FloatValue::operator >= (const Value& other) const {
    return other <= *this;
}
ValueRef FloatValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value >= static_cast<double>(other.value));
}
ValueRef FloatValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(this->value >= other.value);
}
ValueRef FloatValue::operator == (const Value& other) const {
    return other.operator==(*this);
}
ValueRef FloatValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value == static_cast<double>(other.value));
}
ValueRef FloatValue::operator == (const FloatValue& other) const {
    return new BooleanValue(this->value == other.value);
}
ValueRef FloatValue::operator != (const Value& other) const {
    return other.operator!=(*this);
}
ValueRef FloatValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value != static_cast<double>(other.value));
}
ValueRef FloatValue::operator != (const FloatValue& other) const {
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
ValueRef StringValue::operator + (const Value& other) const {
    return other + *this;
}
ValueRef StringValue::operator + (const StringValue& other) const {
    return new StringValue(this->value + other.value);
}
ValueRef StringValue::operator * (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator*(*int_val);
    } else {
        throw std::runtime_error("Multiplication not implemented for this type");
    }
}
ValueRef StringValue::operator * (const IntegerValue& other) const {
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
ValueRef BooleanValue::operator and (const Value& other) const {
    return other and *this;
}
ValueRef BooleanValue::operator and (const BooleanValue& other) const {
    return new BooleanValue(this->value and other.value);
}
ValueRef BooleanValue::operator or (const Value& other) const {
    return other or *this;
}
ValueRef BooleanValue::operator or (const BooleanValue& other) const {
    return new BooleanValue(this->value or other.value);
}
ValueRef BooleanValue::operator not () const {
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

FunctionValue::FunctionValue(const ASTFunctionDefinition* definition) : definition(definition) {}
ValueRef FunctionValue::operator () (Context &globals, const Arguments &args) const {
    try {
        Context locals = definition->signature->collect_arguments(args);
        definition->body->execute(globals, locals);
    } catch (ReturnException<ValueRef>& e) {
        return e.return_value;
    }
    std::unreachable();
}
bool FunctionValue::is_truthy() const {
    return true;
}
FunctionValue* FunctionValue::adapt_for_assignment(const Value& other) const {
    if (auto func_val = dynamic_cast<const FunctionValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to Function");
    }
}
FunctionValue::operator std::string () const {
    return std::format("<function {} at {:p}>", definition->name, static_cast<const void*>(this));
}

BuiltinFunctionValue::Signature::Signature(std::vector<std::pair<std::string, TypeRef>> param_names, std::pair<std::string, TypeRef> spread_param, TypeRef ret_type)
    : parameters(std::move(param_names)), spread_parameter(std::move(spread_param)), return_type(std::move(ret_type)) {}
Context BuiltinFunctionValue::Signature::collect_arguments(const Arguments &args) const {
    Context context;
    auto param_it = parameters.begin();
    auto arg_it = args.begin();
    for (; param_it != parameters.end() && arg_it != args.end(); ++param_it, ++arg_it) {
        context.emplace(param_it->first, *arg_it);
    }
    if (param_it != parameters.end()) {
        throw ArgumentException("Not enough arguments provided to function call"s);
    }
    if (spread_parameter.first != ""s) {
        std::vector<ValueRef> spread_args;
        for (; arg_it != args.end(); ++arg_it) {
            spread_args.emplace_back(*arg_it);
        }
        context.emplace(spread_parameter.first, new ListValue(std::move(spread_args)));
    } else if (arg_it != args.end()) {
        throw ArgumentException("Too many arguments provided to function call"s);
    }
    return context;
}
BuiltinFunctionValue::BuiltinFunctionValue(std::string_view name, FuncType func, Signature&& signature)
    : name(name), func(std::forward<FuncType>(func)), signature(std::forward<Signature>(signature)) {}
ValueRef BuiltinFunctionValue::operator () (Context& globals, const Arguments& args) const {
    return func(signature.collect_arguments(args));
}
bool BuiltinFunctionValue::is_truthy() const {
    return true;
}
BuiltinFunctionValue* BuiltinFunctionValue::adapt_for_assignment(const Value& other) const {
    if (auto func_val = dynamic_cast<const BuiltinFunctionValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to BuiltinFunction");
    }
}
BuiltinFunctionValue::operator std::string () const {
    return std::format("<builtin function {} at {:p}>", name, static_cast<const void*>(this));
}

ClassValue::ClassValue(std::string_view name, std::vector<InterfaceValueRef> implements, ClassValueRef extends, Map<ValueRef> properties)
    : name(name), implements(std::move(implements)), extends(std::move(extends)), properties(std::move(properties)) {}
ClassValue* ClassValue::adapt_for_assignment(const Value& other) const {
    if (auto class_val = dynamic_cast<const ClassValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to Class");
    }
}
ClassValue::operator std::string () const {
    return std::format("<class {} at {:p}>", name, static_cast<const void*>(this));
}

ObjectValue::ObjectValue(ClassValueRef cls) : cls(cls), properties(init_properties()) {}
ValueRef ObjectValue::get(const std::string_view property) const {
    auto it = properties.find(std::string(property));
    if (it != properties.end()) {
        return it->second;
    } else {
        throw std::runtime_error("Property '"s + std::string(property) + "' not found");
    }
}
Map<ValueRef> ObjectValue::init_properties() {
    Map props = static_cast<const ClassValue&>(*cls).properties;
    // TODO: Initialize properties from implements and extends
    return Map(props);
}

ValueRef ListValue::ListClassInstance(new ClassValue("list"sv, std::vector<ValueRef>{}, {}, Map<ValueRef>({
    {"append", new BuiltinFunctionValue(
        "append"sv,
        ListValue::Append,
        { { {"value", new IntegerType} }, {}, new NullType }
    )},
})));
ValueRef ListValue::Append(const Map<ValueRef>& args) {
    try {
        auto self = args.at("this");
        auto arg1 = args.at("list");
        auto arg2 = args.at("value");
        if (auto list_val = dynamic_cast<ListValue*>(&*arg1)) {
            list_val->values.push_back(arg2);
            return new NullValue();
        }
        throw;
    } catch (const std::out_of_range& e) {
        throw ArgumentException(e.what());
    }
}
ListValue::ListValue() : ObjectValue(ListClassInstance), values() {}
ListValue::ListValue(std::vector<ValueRef>&& values) : ObjectValue(ListClassInstance), values(std::forward<std::vector<ValueRef>>(values)) {}
ValueRef ListValue::operator + (const Value& other) const {
    if (auto list_val = dynamic_cast<const ListValue*>(&other)) {
        return this->operator+(*list_val);
    } else {
        throw std::runtime_error("Addition not implemented for this type");
    }
}
ValueRef ListValue::operator + (const ListValue& other) const {
    std::vector<ValueRef> new_values = this->values;
    new_values.insert(new_values.end(), other.values.begin(), other.values.end());
    return new ListValue(std::move(new_values));
}
ValueRef ListValue::operator * (const Value& other) const {
    if (auto int_val = dynamic_cast<const IntegerValue*>(&other)) {
        return this->operator*(*int_val);
    } else {
        throw std::runtime_error("Multiplication not implemented for this type");
    }
}
ValueRef ListValue::operator * (const IntegerValue& other) const {
    if (other.value <= 0) throw std::runtime_error("Can only multiply list by positive integer");
    std::vector<ValueRef> new_values;
    new_values.reserve(this->values.size() * other.value);
    for (uint64_t i = 0; i < other.value; i++) {
        new_values.insert(new_values.end(), this->values.begin(), this->values.end());
    }
    return new ListValue(std::move(new_values));
}
ValueRef ListValue::operator [] (const Slice& indices) const {
    const auto& [start_opt, end_opt, step_opt] = indices;
    int64_t start = start_opt ? start_opt->value : 0;
    int64_t end = end_opt ? end_opt->value : static_cast<int64_t>(this->values.size());
    int64_t step = step_opt ? step_opt->value : 1;
    if (start < 0) start += this->values.size();
    if (end < 0) end += this->values.size();
    if (start < 0 || static_cast<uint64_t>(start) > this->values.size() || end < start || static_cast<size_t>(end) > this->values.size()) {
        throw std::runtime_error("List index out of range");
    }
    return new ListValue(std::vector<ValueRef>(this->values.begin() + start, this->values.begin() + end));
}
bool ListValue::is_truthy() const {
    return not this->values.empty();
}
ListValue* ListValue::adapt_for_assignment(const Value& other) const {
    if (auto list_val = dynamic_cast<const ListValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to List");
    }
}
ListValue::operator std::string () const {
    std::string result = "[";
    for (size_t i = 0; i < values.size(); i++) {
        result += std::string(*values[i]);
        if (i < values.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}

ValueRef DictValue::DictClassInstance(new ClassValue("dict"sv, std::vector<ValueRef>{}, {}, Map<ValueRef>{
}));
DictValue::DictValue(std::unordered_map<std::string, ValueRef>&& values)
    : ObjectValue(DictValue::DictClassInstance), values(std::forward<decltype(values)>(values)) {}
bool DictValue::is_truthy() const {
    return not this->values.empty();
}
DictValue* DictValue::adapt_for_assignment(const Value& other) const {
    if (auto dict_val = dynamic_cast<const DictValue*>(&other)) {
        return nullptr;
    } else {
        throw std::runtime_error("Cannot adapt to Dict");
    }
}
DictValue::operator std::string () const {
    std::string result = "{";
    size_t count = 0;
    for (const auto& [key, value] : values) {
        result += std::format("\"{}\": {}", key, std::string(*value));
        if (count < values.size() - 1) {
            result += ", ";
        }
        count++;
    }
    result += "}";
    return result;
}
ValueRef DictValue::operator [] (const Slice& indices) const {
    const auto& [start_opt, end_opt, step_opt] = indices;
    if (start_opt or end_opt or step_opt) {
        throw std::runtime_error("Slicing not supported for Dict");
    }
    throw std::runtime_error("Indexing not supported for Dict");
}
decltype(auto) DictValue::begin() {
    return values.begin();
}
decltype(auto) DictValue::begin() const {
    return values.begin();
}
decltype(auto) DictValue::end() {
    return values.end();
}
decltype(auto) DictValue::end() const {
    return values.end();
}

Context Builtins{
    { "print"s, new BuiltinFunctionValue(
        "print"sv,
        [](const Map<ValueRef>& args) -> ValueRef {
            auto value_it = args.at("value");
            std::cout << std::string(value_it) << std::endl;
            return new NullValue();
        },
        { { {"value", new IntegerType} }, {}, new NullType }
    )},
};
