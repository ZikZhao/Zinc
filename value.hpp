#pragma once
#include "pch.hpp"
#include "ref.hpp"
#include "exception.hpp"

class ASTFunctionDefinition;

class Value;
class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;
class FunctionValue;
class BuiltinFunctionValue;
class InterfaceValue;
class ClassValue;
class ObjectValue;
class ListValue;
class DictValue;
class SetValue;

class BuiltinFunctionSignature;

using ValueRef = Reference<Value>;
using InterfaceValueRef = ValueRef; // Should always point to an InterfaceValue
using ClassValueRef = ValueRef; // Should always point to a ClassValue
using DictValueRef = ValueRef; // Should always point to a DictValue

using Context = std::unordered_map<std::string, ValueRef>;
using Arguments = std::vector<ValueRef>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;
using OperationTuple = std::tuple<std::string, std::type_index, std::type_index>;

struct OperatorWithResult {
    std::type_index result_type;
    std::function<Value*(const Value*, const Value*)> func;
};

extern const std::map<OperationTuple, OperatorWithResult> OperationMap;

class Value {
public:
    template<typename ValueType>
    static ValueRef FromLiteral(std::string_view literal) {
        if constexpr (std::is_same_v<ValueType, NullValue>) {
            return new ValueType();
        } else if constexpr (std::is_same_v<ValueType, IntegerValue>) {
            return new ValueType(std::stoll(literal.data()));
        } else if constexpr (std::is_same_v<ValueType, FloatValue>) {
            return new ValueType(std::stod(literal.data()));
        } else if constexpr (std::is_same_v<ValueType, StringValue>) {
            return new ValueType(std::string(literal.data()));
        } else if constexpr (std::is_same_v<ValueType, BooleanValue>) {
            if (literal == "true") {
                return new ValueType(true);
            } else if (literal == "false") {
                return new ValueType(false);
            } else {
                throw std::runtime_error("Invalid boolean literal: "s + literal.data());
            }
        } else {
            throw std::runtime_error("FromLiteral not implemented for this type");
        }
    };
    static ValueRef ObjectIs(const Value& left, const Value& right);
public:
    const std::type_index type_index;
    Value(const std::type_info& type) : type_index(type) {}
    virtual ~Value() = default;
    Value* eval_operation(std::string_view op) const;
    Value* eval_operation(std::string_view op, const Value& other) const;
    virtual ValueRef operator () (Context& globals, const Arguments& args) const { throw std::runtime_error("Function call not implemented for this type"); };
    virtual ValueRef operator [] (const Slice& indices) const { throw std::runtime_error("Indexing not implemented for this type"); };
    virtual ValueRef get(const std::string_view property) const { throw std::runtime_error("Property access not implemented for this type"); };
    virtual bool is_truthy() const { throw std::runtime_error("Truthiness not implemented for this type"); };
    virtual Value* adapt_for_assignment(const Value& other) const = 0;
    virtual operator std::string () const = 0;
};

class NullValue : public Value {
public:
    NullValue();
    NullValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class IntegerValue : public Value {
public:
    const int64_t value;
    IntegerValue(int64_t value);
    IntegerValue* operator + (const IntegerValue& other) const;
    IntegerValue* operator - (const IntegerValue& other) const;
    IntegerValue* operator - () const;
    IntegerValue* operator * (const IntegerValue& other) const;
    IntegerValue* operator / (const IntegerValue& other) const;
    IntegerValue* operator % (const IntegerValue& other) const;
    BooleanValue* operator == (const IntegerValue& other) const;
    BooleanValue* operator != (const IntegerValue& other) const;
    BooleanValue* operator < (const IntegerValue& other) const;
    BooleanValue* operator <= (const IntegerValue& other) const;
    BooleanValue* operator > (const IntegerValue& other) const;
    BooleanValue* operator >= (const IntegerValue& other) const;
    IntegerValue* operator & (const IntegerValue& other) const;
    IntegerValue* operator | (const IntegerValue& other) const;
    IntegerValue* operator ^ (const IntegerValue& other) const;
    IntegerValue* operator ~ () const;
    IntegerValue* operator << (const IntegerValue& other) const;
    IntegerValue* operator >> (const IntegerValue& other) const;
    bool is_truthy() const final;
    IntegerValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class FloatValue : public Value {
public:
    const double value;
    FloatValue(double value);
    FloatValue* operator + (const FloatValue& other) const;
    FloatValue* operator - (const FloatValue& other) const;
    FloatValue* operator - () const;
    FloatValue* operator * (const FloatValue& other) const;
    FloatValue* operator / (const FloatValue& other) const;
    FloatValue* operator % (const FloatValue& other) const;
    BooleanValue* operator == (const FloatValue& other) const;
    BooleanValue* operator != (const FloatValue& other) const;
    BooleanValue* operator < (const FloatValue& other) const;
    BooleanValue* operator <= (const FloatValue& other) const;
    BooleanValue* operator > (const FloatValue& other) const;
    BooleanValue* operator >= (const FloatValue& other) const;
    bool is_truthy() const final;
    FloatValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class StringValue : public Value {
public:
    const std::string value;
    StringValue(std::string&& value);
    StringValue* operator + (const StringValue& other) const;
    StringValue* operator * (const IntegerValue& other) const;
    BooleanValue* operator == (const StringValue& other) const;
    BooleanValue* operator != (const StringValue& other) const;
    bool is_truthy() const final;
    StringValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class BooleanValue : public Value {
public:
    const bool value;
    BooleanValue(bool value);
    BooleanValue* operator == (const BooleanValue& other) const;
    BooleanValue* operator != (const BooleanValue& other) const;
    BooleanValue* operator and (const BooleanValue& other) const;
    BooleanValue* operator or (const BooleanValue& other) const;
    BooleanValue* operator not () const;
    bool is_truthy() const final;
    BooleanValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class FunctionValue : public Value {
public:
    const ASTFunctionDefinition* const definition;
    FunctionValue(const ASTFunctionDefinition* definition);
    ValueRef operator () (Context& globals, const Arguments& args) const final;
    bool is_truthy() const final;
    FunctionValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class BuiltinFunctionValue : public Value {
public:
    using FuncType = std::function<ValueRef(const Map<ValueRef>&)>;
    const std::string name;
    const FuncType func;
    const std::unique_ptr<const BuiltinFunctionSignature> signature;
    BuiltinFunctionValue(std::string_view name, FuncType func, const BuiltinFunctionSignature* signature);
    ValueRef operator () (Context& globals, const Arguments& args) const final;
    bool is_truthy() const final;
    BuiltinFunctionValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class ClassValue : public Value {
public:
    std::string name;
    Map<ValueRef> properties;
    std::vector<InterfaceValueRef> implements;
    ClassValueRef extends;
private:
    ClassValue();
public:
    ClassValue(std::string_view name, std::vector<InterfaceValueRef> implements, ClassValueRef extends, Map<ValueRef> properties);
    ClassValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class ObjectValue : public Value {
public:
    ClassValueRef cls;
    Map<ValueRef> properties;
    ObjectValue(ClassValueRef cls);
    ValueRef get(const std::string_view property) const final;
private:
    Map<ValueRef> init_properties();
};

class ListValue : public ObjectValue {
private:
    static ClassValueRef ListClassInstance;
    static ValueRef Append(const Map<ValueRef>& args);
public:
    std::vector<ValueRef> values;
    ListValue();
    ListValue(std::vector<ValueRef>&& values);
    ListValue* operator + (const ListValue& other) const;
    ListValue* operator * (const IntegerValue& other) const;
    ValueRef operator [] (const Slice& indices) const final;
    bool is_truthy() const final;
    ListValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class DictValue : public ObjectValue {
public:
    static ValueRef DictClassInstance;
public:
    std::unordered_map<std::string, ValueRef> values;
    DictValue(std::unordered_map<std::string, ValueRef>&& values);
    ValueRef operator [] (const Slice& indices) const final;
    bool is_truthy() const final;
    DictValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
    decltype(auto) begin();
    decltype(auto) begin() const;
    decltype(auto) end();
    decltype(auto) end() const;
};

class SetValue : public ObjectValue {
public:
    static ValueRef SetClassInstance;
public:
    std::set<ValueRef> values;
    SetValue(std::set<ValueRef>&& values);
    ValueRef operator [] (const Slice& indices) const final;
    bool is_truthy() const final;
    SetValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
    decltype(auto) begin();
    decltype(auto) begin() const;
    decltype(auto) end();
    decltype(auto) end() const;
};

extern Context Builtins;
