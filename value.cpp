#include "pch.hpp"
#include "value.hpp"
#include "ref.hpp"
#include "out/parser.tab.hpp"

template<typename Left, typename Func, typename Right = void>
auto MakeRule() {
    if constexpr (std::is_same_v<Right, void>) {
        using ResultType = std::invoke_result_t<Func, Left>;
        const auto wrapped = [](const Value* expr, const Value* _) -> Value* {
            return Func()(static_cast<const Left&>(*expr));
        };
        return std::make_pair(
            OperationTuple{ GetOperatorString<Func>(), typeid(Left), typeid(void) },
            OperatorWithResult{ std::type_index(typeid(ResultType)), std::function<Value*(const Value*, const Value*)>(wrapped) }
        );
    } else {
        using ResultType = std::invoke_result_t<Func, Left, Right>;
        const auto wrapped = [](const Value* left, const Value* right) -> Value* {
            return Func()(static_cast<const Left&>(*left), static_cast<const Right&>(*right));
        };
        return std::make_pair(
            OperationTuple{ GetOperatorString<Func>(), typeid(Left), typeid(Right) },
            OperatorWithResult{ std::type_index(typeid(ResultType)), std::function<Value*(const Value*, const Value*)>(wrapped) }
        );
    }
}

const std::map<OperationTuple, OperatorWithResult> OperationMap = {
    MakeRule<IntegerValue, OperatorFunctors::Add, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::Subtract, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::Negate>(),
    MakeRule<IntegerValue, OperatorFunctors::Multiply, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::Divide, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::Remainder, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::Equal, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::NotEqual, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::LessThan, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::LessEqual, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::GreaterThan, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::GreaterEqual, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::BitwiseAnd, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::BitwiseOr, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::BitwiseXor, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::BitwiseNot>(),
    MakeRule<IntegerValue, OperatorFunctors::LeftShift, IntegerValue>(),
    MakeRule<IntegerValue, OperatorFunctors::RightShift, IntegerValue>(),
    MakeRule<FloatValue, OperatorFunctors::Add, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::Subtract, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::Negate>(),
    MakeRule<FloatValue, OperatorFunctors::Multiply, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::Divide, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::Remainder, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::Equal, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::NotEqual, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::LessThan, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::LessEqual, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::GreaterThan, FloatValue>(),
    MakeRule<FloatValue, OperatorFunctors::GreaterEqual, FloatValue>(),
    MakeRule<StringValue, OperatorFunctors::Add, StringValue>(),
    MakeRule<StringValue, OperatorFunctors::Multiply, IntegerValue>(),
    MakeRule<StringValue, OperatorFunctors::Equal, StringValue>(),
    MakeRule<StringValue, OperatorFunctors::NotEqual, StringValue>(),
    MakeRule<BooleanValue, OperatorFunctors::Equal, BooleanValue>(),
    MakeRule<BooleanValue, OperatorFunctors::NotEqual, BooleanValue>(),
    MakeRule<BooleanValue, OperatorFunctors::LogicalAnd, BooleanValue>(),
    MakeRule<BooleanValue, OperatorFunctors::LogicalOr, BooleanValue>(),
    MakeRule<BooleanValue, OperatorFunctors::LogicalNot>(),
    // MakeRule<ListValue, OperatorFunctors::Add, ListValue>(),
    // MakeRule<ListValue, OperatorFunctors::Multiply, IntegerValue>(),
    // MakeRule<ListValue, OperatorFunctors::Equal, ListValue>(),
    // MakeRule<ListValue, OperatorFunctors::NotEqual, ListValue>(),
};

ValueRef Value::ObjectIs(const Value& left, const Value& right) {
    return new BooleanValue(&left == &right);
}
Value* Value::eval_operation(std::string_view op) const {
    auto it = OperationMap.find(OperationTuple{ std::string(op), this->type_index, std::type_index(typeid(void)) });
    if (it != OperationMap.end()) {
        return it->second.func(this, nullptr);
    } else {
        throw std::runtime_error("Operation "s + std::string(op) + " not supported for type " + static_cast<std::string>(*this));
    }
}
Value* Value::eval_operation(std::string_view op, const Value& other) const {
    if (this->type_index != other.type_index) {
        if (this->type_index == typeid(FloatValue) and other.type_index == typeid(IntegerValue)) {
            FloatValue promoted_other = static_cast<const IntegerValue&>(other).value;
            auto result = promoted_other.eval_operation(op, *this);
            return result;
        } else if (this->type_index == typeid(IntegerValue) and other.type_index == typeid(FloatValue)) {
            FloatValue promoted_this = static_cast<const IntegerValue&>(*this).value;
            auto result = promoted_this.eval_operation(op, other);
            return result;
        }
    }
    auto it = OperationMap.find(OperationTuple{ std::string(op), this->type_index, other.type_index });
    if (it != OperationMap.end()) {
        return it->second.func(this, &other);
    } else {
        throw std::runtime_error("Operation "s + std::string(op) + " not supported for types " + static_cast<std::string>(*this) + " and " + static_cast<std::string>(other));
    }
}

NullValue::NullValue() : Value(typeid(NullValue)) {}
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

IntegerValue::IntegerValue(int64_t value) : Value(typeid(IntegerValue)), value(value) {}
IntegerValue* IntegerValue::operator + (const IntegerValue& other) const {
    return new IntegerValue(this->value + other.value);
}
IntegerValue* IntegerValue::operator - (const IntegerValue& other) const {
    return new IntegerValue(this->value - other.value);
}
IntegerValue* IntegerValue::operator - () const {
    return new IntegerValue(-this->value);
}
IntegerValue* IntegerValue::operator * (const IntegerValue& other) const {
    return new IntegerValue(this->value * other.value);
}
IntegerValue* IntegerValue::operator / (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value / other.value);
}
IntegerValue* IntegerValue::operator % (const IntegerValue& other) const {
    if (other.value == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value % other.value);
}
BooleanValue* IntegerValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value == other.value);
}
BooleanValue* IntegerValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value != other.value);
}
BooleanValue* IntegerValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value < other.value);
}
BooleanValue* IntegerValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
BooleanValue* IntegerValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value > other.value);
}
BooleanValue* IntegerValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value >= other.value);
}
IntegerValue* IntegerValue::operator & (const IntegerValue& other) const {
    return new IntegerValue(this->value & other.value);
}
IntegerValue* IntegerValue::operator | (const IntegerValue& other) const {
    return new IntegerValue(this->value | other.value);
}
IntegerValue* IntegerValue::operator ^ (const IntegerValue& other) const {
    return new IntegerValue(this->value ^ other.value);
}
IntegerValue* IntegerValue::operator ~ () const {
    return new IntegerValue(~this->value);
}
IntegerValue* IntegerValue::operator << (const IntegerValue& other) const {
    if (other.value < 0) throw std::runtime_error("Cannot left shift by negative amount");
    return new IntegerValue(this->value << other.value);
}
IntegerValue* IntegerValue::operator >> (const IntegerValue& other) const {
    if (other.value < 0) throw std::runtime_error("Cannot right shift by negative amount");
    return new IntegerValue(this->value >> other.value);
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

FloatValue::FloatValue(double value) : Value(typeid(FloatValue)), value(value) {}
FloatValue* FloatValue::operator + (const FloatValue& other) const {
    return new FloatValue(this->value + other.value);
}
FloatValue* FloatValue::operator - (const FloatValue& other) const {
    return new FloatValue(this->value - other.value);
}
FloatValue* FloatValue::operator - () const {
    return new FloatValue(-this->value);
}
FloatValue* FloatValue::operator * (const FloatValue& other) const {
    return new FloatValue(this->value * other.value);
}
FloatValue* FloatValue::operator / (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value / other.value);
}
FloatValue* FloatValue::operator % (const FloatValue& other) const {
    if (other.value == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value, other.value));
}
BooleanValue* FloatValue::operator == (const FloatValue& other) const {
    return new BooleanValue(this->value == other.value);
}
BooleanValue* FloatValue::operator != (const FloatValue& other) const {
    return new BooleanValue(this->value != other.value);
}
BooleanValue* FloatValue::operator < (const FloatValue& other) const {
    return new BooleanValue(this->value < other.value);
}
BooleanValue* FloatValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(this->value <= other.value);
}
BooleanValue* FloatValue::operator > (const FloatValue& other) const {
    return new BooleanValue(this->value > other.value);
}
BooleanValue* FloatValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(this->value >= other.value);
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

StringValue::StringValue(std::string&& value) : Value(typeid(StringValue)), value(std::forward<std::string>(value)) {}
StringValue* StringValue::operator + (const StringValue& other) const {
    return new StringValue(this->value + other.value);
}
StringValue* StringValue::operator * (const IntegerValue& other) const {
    if (other.value <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value.size() * other.value);
    for (uint64_t i = 0; i < other.value; i++) {
        result += this->value;
    }
    return new StringValue(std::move(result));
}
BooleanValue* StringValue::operator == (const StringValue& other) const {
    return new BooleanValue(this->value == other.value);
}
BooleanValue* StringValue::operator != (const StringValue& other) const {
    return new BooleanValue(this->value != other.value);
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

BooleanValue::BooleanValue(bool value) : Value(typeid(BooleanValue)), value(value) {}
BooleanValue* BooleanValue::operator == (const BooleanValue& other) const {
    return new BooleanValue(this->value == other.value);
}
BooleanValue* BooleanValue::operator != (const BooleanValue& other) const {
    return new BooleanValue(this->value != other.value);
}
BooleanValue* BooleanValue::operator and (const BooleanValue& other) const {
    return new BooleanValue(this->value && other.value);
}
BooleanValue* BooleanValue::operator or (const BooleanValue& other) const {
    return new BooleanValue(this->value || other.value);
}
BooleanValue* BooleanValue::operator not () const {
    return new BooleanValue(!this->value);
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

FunctionValue::FunctionValue(const ASTFunctionDefinition* definition) : Value(typeid(FunctionValue)), definition(definition) {}
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

BuiltinFunctionValue::BuiltinFunctionValue(std::string_view name, FuncType func, const BuiltinFunctionSignature* signature)
    : Value(typeid(BuiltinFunctionValue)), name(name), func(std::forward<FuncType>(func)), signature(signature) {}
ValueRef BuiltinFunctionValue::operator () (Context& globals, const Arguments& args) const {
    return func(signature->collect_arguments(args));
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
    : Value(typeid(ClassValue)), name(name), implements(std::move(implements)), extends(std::move(extends)), properties(std::move(properties)) {}
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

ObjectValue::ObjectValue(ClassValueRef cls) : Value(typeid(ObjectValue)), cls(cls), properties(init_properties()) {}
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
        new BuiltinFunctionSignature({ {"value", new IntegerType} }, {}, new NullType)
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
ListValue* ListValue::operator + (const ListValue& other) const {
    std::vector<ValueRef> new_values = this->values;
    new_values.insert(new_values.end(), other.values.begin(), other.values.end());
    return new ListValue(std::move(new_values));
}
ListValue* ListValue::operator * (const IntegerValue& other) const {
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
        new BuiltinFunctionSignature({ {"value", new IntegerType} }, {}, new NullType)
    )},
};
