#include "pch.hpp"
#include "object.hpp"

template<typename Left, typename Func, typename Right = void>
consteval auto MakeBasicOperation() {
    if constexpr (std::is_same_v<Right, void>) {
        using ResultType = std::invoke_result_t<Func, Left>;
        return std::array{
            std::pair{
                OperationTuple{ GetOperatorString<Func>(), Left::Kind, KIND::KIND_NO_RIGHT_OPERAND },
                SpecifiedOperatorFunc([](TypeOrValue* expr, TypeOrValue*) -> TypeOrValue* {
                    return Func()(static_cast<Left&>(*expr));
                }),
            },
            std::pair{
                OperationTuple{ GetOperatorString<Func>(), Left::Type::Kind, KIND::KIND_NO_RIGHT_OPERAND },
                SpecifiedOperatorFunc([](TypeOrValue*, TypeOrValue*) -> TypeOrValue* {
                    return ResultType();
                }),
            },
        };
    } else {
        using ResultType = std::invoke_result_t<Func, Left, Right>;
        return std::array{
            std::pair{
                OperationTuple{ GetOperatorString<Func>(), Left::Kind, Right::Kind },
                SpecifiedOperatorFunc([](TypeOrValue* left, TypeOrValue* right) -> TypeOrValue* {
                    return Func()(static_cast<Left&>(*left), static_cast<Right&>(*right));
                }),
            },
            std::pair{
                OperationTuple{ GetOperatorString<Func>(), Left::Type::Kind, Right::Type::Kind },
                SpecifiedOperatorFunc([](TypeOrValue*, TypeOrValue*) -> TypeOrValue* {
                    return ResultType();
                })
            },
        };
    }
}

consteval auto MakeOperationMap() {
    return std::array{
        MakeBasicOperation<IntegerValue, OperatorFunctors::Add, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Subtract, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Negate>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Multiply, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Divide, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Remainder, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Equal, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::NotEqual, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::LessThan, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::LessEqual, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::GreaterThan, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::GreaterEqual, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::BitwiseAnd, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::BitwiseOr, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::BitwiseXor, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::BitwiseNot>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::LeftShift, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::RightShift, IntegerValue>(),
        MakeBasicOperation<IntegerValue, OperatorFunctors::Assign, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Add, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Subtract, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Negate>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Multiply, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Divide, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Remainder, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Equal, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::NotEqual, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::LessThan, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::LessEqual, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::GreaterThan, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::GreaterEqual, FloatValue>(),
        MakeBasicOperation<FloatValue, OperatorFunctors::Assign, IntegerValue>(),
        MakeBasicOperation<StringValue, OperatorFunctors::Add, StringValue>(),
        MakeBasicOperation<StringValue, OperatorFunctors::Multiply, IntegerValue>(),
        MakeBasicOperation<StringValue, OperatorFunctors::Equal, StringValue>(),
        MakeBasicOperation<StringValue, OperatorFunctors::NotEqual, StringValue>(),
        MakeBasicOperation<BooleanValue, OperatorFunctors::Equal, BooleanValue>(),
        MakeBasicOperation<BooleanValue, OperatorFunctors::NotEqual, BooleanValue>(),
        MakeBasicOperation<BooleanValue, OperatorFunctors::LogicalAnd, BooleanValue>(),
        MakeBasicOperation<BooleanValue, OperatorFunctors::LogicalOr, BooleanValue>(),
        MakeBasicOperation<BooleanValue, OperatorFunctors::LogicalNot>(),
        // MakeBasicOperation<ListValue, OperatorFunctors::Add, ListValue>(),
        // MakeBasicOperation<ListValue, OperatorFunctors::Multiply, IntegerValue>(),
        // MakeBasicOperation<ListValue, OperatorFunctors::Equal, ListValue>(),
        // MakeBasicOperation<ListValue, OperatorFunctors::NotEqual, ListValue>(),
    } | std::views::join;
}

const std::map<OperationTuple, SpecifiedOperatorFunc> OperationMap = std::ranges::to<std::map<OperationTuple, SpecifiedOperatorFunc>>(MakeOperationMap());

TypeOrValue::TypeOrValue(KIND kind) : kind_(kind) {}
bool TypeOrValue::is_truthy() const {
    if (static_cast<bool>(kind_ & KIND::KIND_TYPE_FLAG)) {
        throw std::runtime_error("value expected, got type");
    }
    return static_cast<const Value*>(this)->is_truthy();
}
bool TypeOrValue::contains(const TypeOrValue& other) const {
    if (not static_cast<bool>(kind_ & KIND::KIND_TYPE_FLAG) or
        not static_cast<bool>(other.kind_ & KIND::KIND_TYPE_FLAG)) {
        throw std::runtime_error("type expected, got value");
    }
    return static_cast<const Type*>(this)->contains(*static_cast<const Type*>(&other));
}
ObjRef TypeOrValue::eval_operation(std::string_view op) {
    auto it = OperationMap.find(OperationTuple{ std::string(op), this->kind_, KIND::KIND_NO_RIGHT_OPERAND });
    if (it != OperationMap.end()) {
        return it->second(this, nullptr);
    } else {
        throw std::runtime_error("Operation not supported");
    }
}
ObjRef TypeOrValue::eval_operation(std::string_view op, TypeOrValue& other) {
    if (this->kind_ != other.kind_) {
        if (this->kind_ == KIND::KIND_FLOAT and other.kind_ == KIND::KIND_INTEGER) {
            FloatValue promoted_other = static_cast<double>(static_cast<const IntegerValue&>(other).value_);
            auto result = this->eval_operation(op, promoted_other);
            return result;
        } else if (this->kind_ == KIND::KIND_INTEGER and other.kind_ == KIND::KIND_FLOAT) {
            FloatValue promoted_this = static_cast<double>(static_cast<const IntegerValue&>(*this).value_);
            auto result = promoted_this.eval_operation(op, other);
            return result;
        }
    }
    auto it = OperationMap.find(OperationTuple{ std::string(op), this->kind_, other.kind_ });
    if (it != OperationMap.end()) {
        return it->second(this, &other);
    } else {
        throw std::runtime_error("Operation not supported");
    }
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
Type::Type(KIND kind) : TypeOrValue(kind) {}

AnyType::AnyType() : Type(KIND::KIND_ANY) {}
std::string AnyType::repr() const {
    return "any";
}
bool AnyType::contains(const Type& other) const {
    return true;
}

ClassType::ClassType(std::string_view name, const std::vector<InterfaceTypeRef>& interfaces, const ClassTypeRef extends, const ScopeDefinition* properties)
    : Type(KIND::KIND_CLASS), name(name), interfaces(interfaces), extends(extends), properties(properties) {}
std::string ClassType::repr() const {
    return "class "s + std::string(name);
}
bool ClassType::contains(const Type& other) const {
    // TODO
    return false;
}

ListType::ListType(TypeRef element_type) : ClassType("List", {}, nullptr, nullptr), element_type_(element_type) {}
std::string ListType::repr() const {
    return "List<"s + element_type_->repr() + ">"s;
}
bool ListType::contains(const Type& other) const {
    if (other.kind_ != this->kind_) {
        return false;
    }
    const ListType& other_list = static_cast<const ListType&>(other);
    return this->element_type_->contains(*other_list.element_type_);
}

Value::Value(KIND kind) : TypeOrValue(kind) {}

NullValue::NullValue() : Value(KIND::KIND_NULL) {}
std::string NullValue::repr() const {
    return "null";
}
bool NullValue::is_truthy() const {
    return false;
}

IntegerValue::IntegerValue(int64_t value) : Value(KIND::KIND_INTEGER), value_(value) {}
std::string IntegerValue::repr() const {
    return std::to_string(value_);
}
bool IntegerValue::is_truthy() const {
    return this->value_ != 0;
}
IntegerValue* IntegerValue::operator + (const IntegerValue& other) const {
    return new IntegerValue(this->value_ + other.value_);
}
IntegerValue* IntegerValue::operator - (const IntegerValue& other) const {
    return new IntegerValue(this->value_ - other.value_);
}
IntegerValue* IntegerValue::operator - () const {
    return new IntegerValue(-this->value_);
}
IntegerValue* IntegerValue::operator * (const IntegerValue& other) const {
    return new IntegerValue(this->value_ * other.value_);
}
IntegerValue* IntegerValue::operator / (const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value_ / other.value_);
}
IntegerValue* IntegerValue::operator % (const IntegerValue& other) const {
    if (other.value_ == 0) throw std::runtime_error("Division by zero");
    return new IntegerValue(this->value_ % other.value_);
}
BooleanValue* IntegerValue::operator == (const IntegerValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* IntegerValue::operator != (const IntegerValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* IntegerValue::operator < (const IntegerValue& other) const {
    return new BooleanValue(this->value_ < other.value_);
}
BooleanValue* IntegerValue::operator <= (const IntegerValue& other) const {
    return new BooleanValue(this->value_ <= other.value_);
}
BooleanValue* IntegerValue::operator > (const IntegerValue& other) const {
    return new BooleanValue(this->value_ > other.value_);
}
BooleanValue* IntegerValue::operator >= (const IntegerValue& other) const {
    return new BooleanValue(this->value_ >= other.value_);
}
IntegerValue* IntegerValue::operator & (const IntegerValue& other) const {
    return new IntegerValue(this->value_ & other.value_);
}
IntegerValue* IntegerValue::operator | (const IntegerValue& other) const {
    return new IntegerValue(this->value_ | other.value_);
}
IntegerValue* IntegerValue::operator ^ (const IntegerValue& other) const {
    return new IntegerValue(this->value_ ^ other.value_);
}
IntegerValue* IntegerValue::operator ~ () const {
    return new IntegerValue(~this->value_);
}
IntegerValue* IntegerValue::operator << (const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot left shift by negative amount");
    return new IntegerValue(this->value_ << other.value_);
}
IntegerValue* IntegerValue::operator >> (const IntegerValue& other) const {
    if (other.value_ < 0) throw std::runtime_error("Cannot right shift by negative amount");
    return new IntegerValue(this->value_ >> other.value_);
}
IntegerValue* IntegerValue::operator = (const FloatValue& other) const {
    return new IntegerValue(static_cast<std::int64_t>(other.value_));
}

FloatValue::FloatValue(double value) : Value(KIND::KIND_FLOAT), value_(value) {}
std::string FloatValue::repr() const {
    return std::to_string(value_);
}
bool FloatValue::is_truthy() const {
    return this->value_ != 0.0;
}
FloatValue* FloatValue::operator + (const FloatValue& other) const {
    return new FloatValue(this->value_ + other.value_);
}
FloatValue* FloatValue::operator - (const FloatValue& other) const {
    return new FloatValue(this->value_ - other.value_);
}
FloatValue* FloatValue::operator - () const {
    return new FloatValue(-this->value_);
}
FloatValue* FloatValue::operator * (const FloatValue& other) const {
    return new FloatValue(this->value_ * other.value_);
}
FloatValue* FloatValue::operator / (const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(this->value_ / other.value_);
}
FloatValue* FloatValue::operator % (const FloatValue& other) const {
    if (other.value_ == 0.0) throw std::runtime_error("Division by zero");
    return new FloatValue(std::fmod(this->value_, other.value_));
}
BooleanValue* FloatValue::operator == (const FloatValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* FloatValue::operator != (const FloatValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* FloatValue::operator < (const FloatValue& other) const {
    return new BooleanValue(this->value_ < other.value_);
}
BooleanValue* FloatValue::operator <= (const FloatValue& other) const {
    return new BooleanValue(this->value_ <= other.value_);
}
BooleanValue* FloatValue::operator > (const FloatValue& other) const {
    return new BooleanValue(this->value_ > other.value_);
}
BooleanValue* FloatValue::operator >= (const FloatValue& other) const {
    return new BooleanValue(this->value_ >= other.value_);
}
FloatValue* FloatValue::operator = (const IntegerValue& other) const {
    return new FloatValue(static_cast<double>(other.value_));
}

StringValue::StringValue(std::string&& value) : Value(KIND::KIND_STRING), value_(std::move(value)) {}
std::string StringValue::repr() const {
    return "\"" + this->value_ + "\"";
}
bool StringValue::is_truthy() const {
    return not this->value_.empty();
}
StringValue* StringValue::operator + (const StringValue& other) const {
    return new StringValue(this->value_ + other.value_);
}
StringValue* StringValue::operator * (const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply string by positive integer");
    std::string result;
    result.reserve(this->value_.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        result += this->value_;
    }
    return new StringValue(std::move(result));
}
BooleanValue* StringValue::operator == (const StringValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* StringValue::operator != (const StringValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}

BooleanValue::BooleanValue(bool value) : Value(KIND::KIND_BOOLEAN), value_(value) {}
std::string BooleanValue::repr() const {
    return this->value_ ? "true" : "false";
}
bool BooleanValue::is_truthy() const {
    return this->value_;
}
BooleanValue* BooleanValue::operator == (const BooleanValue& other) const {
    return new BooleanValue(this->value_ == other.value_);
}
BooleanValue* BooleanValue::operator != (const BooleanValue& other) const {
    return new BooleanValue(this->value_ != other.value_);
}
BooleanValue* BooleanValue::operator and (const BooleanValue& other) const {
    return new BooleanValue(this->value_ && other.value_);
}
BooleanValue* BooleanValue::operator or (const BooleanValue& other) const {
    return new BooleanValue(this->value_ || other.value_);
}
BooleanValue* BooleanValue::operator not () const {
    return new BooleanValue(!this->value_);
}

std::string FunctionValue::repr() const {
    return std::format("<function at {:p}>", static_cast<const void*>(this));
}
bool FunctionValue::is_truthy() const {
    return true;
}
ValueRef FunctionValue::operator () (const Arguments &args) const {
    try {
        return callback_(args);
    } catch (ReturnException<ValueRef>& e) {
        return e.value_;
    }
    std::unreachable();
}

ObjectValue::ObjectValue(TypeRef cls) : Value(KIND::KIND_OBJECT), class_type_(cls) {}

ValueRef ListValue::ListClassInstance = new ClassType("list"sv, std::vector<ValueRef>{}, {}, nullptr);
ValueRef ListValue::Append(const std::vector<ValueRef>& args) {
    try {
        auto self = args.at(0);
        auto value = args.at(1);
        if (auto list_val = dynamic_cast<ListValue*>(&*self)) {
            list_val->values.push_back(value);
            return new NullValue();
        }
        throw;
    } catch (const std::out_of_range& e) {
        throw ArgumentException(e.what());
    }
}
ListValue::ListValue() : ObjectValue(ListClassInstance), values() {}
ListValue::ListValue(std::vector<ValueRef>&& values) : ObjectValue(ListClassInstance), values(std::forward<std::vector<ValueRef>>(values)) {}
std::string ListValue::repr() const {
    std::string result = "[";
    for (size_t i = 0; i < this->values.size(); i++) {
        result += this->values[i]->repr();
        if (i < this->values.size() - 1) {
            result += ", ";
        }
    }
    result += "]";
    return result;
}
bool ListValue::is_truthy() const {
    return not this->values.empty();
}
ListValue* ListValue::operator + (const ListValue& other) const {
    std::vector<ValueRef> new_values = this->values;
    new_values.insert(new_values.end(), other.values.begin(), other.values.end());
    return new ListValue(std::move(new_values));
}
ListValue* ListValue::operator * (const IntegerValue& other) const {
    if (other.value_ <= 0) throw std::runtime_error("Can only multiply list by positive integer");
    std::vector<ValueRef> new_values;
    new_values.reserve(this->values.size() * static_cast<std::uint64_t>(other.value_));
    for (uint64_t i = 0; i < static_cast<std::uint64_t>(other.value_); i++) {
        new_values.insert(new_values.end(), this->values.begin(), this->values.end());
    }
    return new ListValue(std::move(new_values));
}
ValueRef ListValue::operator [] (const Slice& indices) const {
    const auto& [start_opt, end_opt, step_opt] = indices;
    int64_t start = start_opt ? start_opt->value_ : 0;
    int64_t end = end_opt ? end_opt->value_ : static_cast<int64_t>(this->values.size());
    int64_t step = step_opt ? step_opt->value_ : 1;
    if (start < 0) start += this->values.size();
    if (end < 0) end += this->values.size();
    if (start < 0 || static_cast<uint64_t>(start) > this->values.size() || end < start || static_cast<size_t>(end) > this->values.size()) {
        throw std::runtime_error("List index out of range");
    }
    return new ListValue(std::vector<ValueRef>(this->values.begin() + start, this->values.begin() + end));
}
