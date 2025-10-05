#pragma once
#include "pch.hpp"
#include "ref.hpp"
#define AllTypeVirtualBinaryOperator(op) \
    virtual ValueRef operator op (const Value& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const IntegerValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const FloatValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const StringValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const BooleanValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const FunctionValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual ValueRef operator op (const ListValue& other) const { throw std::runtime_error(#op " not implemented for this type"); };
#define AllTypeVirtualUnaryOperator(op) \
    virtual ValueRef operator op () const { throw std::runtime_error(#op " not implemented for this type"); };

enum LiteralType {
    LITERAL_NULL,
    LITERAL_INTEGER,
    LITERAL_FLOAT,
    LITERAL_STRING,
    LITERAL_BOOLEAN,
};


class ASTFunctionDefinition;

class Value;
class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;
class FunctionValue;
class ListValue;
class DictValue;

using ValueRef = Reference<Value>;
using Context = std::unordered_map<std::string, ValueRef>;
using Map = std::unordered_map<std::string, ValueRef>;
using Slice = std::tuple<const IntegerValue*, const IntegerValue*, const IntegerValue*>;

class Value {
public:
    static ValueRef FromLiteral(LiteralType type, std::string_view literal);
    static ValueRef ObjectIs(const Value& left, const Value& right);
    virtual ~Value() = default;
    AllTypeVirtualBinaryOperator(+);
    AllTypeVirtualBinaryOperator(-);
    AllTypeVirtualUnaryOperator(-);
    AllTypeVirtualBinaryOperator(*);
    AllTypeVirtualBinaryOperator(/);
    AllTypeVirtualBinaryOperator(%);
    AllTypeVirtualBinaryOperator(==);
    AllTypeVirtualBinaryOperator(!=);
    AllTypeVirtualBinaryOperator(<);
    AllTypeVirtualBinaryOperator(<=);
    AllTypeVirtualBinaryOperator(>);
    AllTypeVirtualBinaryOperator(>=);
    AllTypeVirtualBinaryOperator(and);
    AllTypeVirtualBinaryOperator(or);
    AllTypeVirtualUnaryOperator(not);
    AllTypeVirtualBinaryOperator(&);
    AllTypeVirtualBinaryOperator(|);
    AllTypeVirtualBinaryOperator(^);
    AllTypeVirtualUnaryOperator(~);
    AllTypeVirtualBinaryOperator(<<);
    AllTypeVirtualBinaryOperator(>>);
    virtual ValueRef operator () (const DictValue* args) const { throw std::runtime_error("Function call not implemented for this type"); };
    virtual ValueRef operator [] (const Slice& indices) const { throw std::runtime_error("Indexing not implemented for this type"); };
    virtual ValueRef get(const std::string_view property) const { throw std::runtime_error("Property access not implemented for this type"); };
    virtual bool is_truthy() const { throw std::runtime_error("Truthiness not implemented for this type"); };
    virtual Value* adapt_for_assignment(const Value& other) const = 0;
    virtual operator std::string () const = 0;
};

class NullValue : public Value {
public:
    NullValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class IntegerValue : public Value {
public:
    const int64_t value;
    IntegerValue(int64_t value);
    ValueRef operator + (const Value& other) const final;
    ValueRef operator + (const IntegerValue& other) const final;
    ValueRef operator + (const FloatValue& other) const final;
    ValueRef operator - (const Value& other) const final;
    ValueRef operator - (const IntegerValue& other) const final;
    ValueRef operator - (const FloatValue& other) const final;
    ValueRef operator - () const final;
    ValueRef operator * (const Value& other) const final;
    ValueRef operator * (const IntegerValue& other) const final;
    ValueRef operator * (const FloatValue& other) const final;
    ValueRef operator / (const Value& other) const final;
    ValueRef operator / (const IntegerValue& other) const final;
    ValueRef operator / (const FloatValue& other) const final;
    ValueRef operator % (const Value& other) const final;
    ValueRef operator % (const IntegerValue& other) const final;
    ValueRef operator % (const FloatValue& other) const final;
    ValueRef operator < (const Value& other) const final;
    ValueRef operator < (const IntegerValue& other) const final;
    ValueRef operator < (const FloatValue& other) const final;
    ValueRef operator <= (const Value& other) const final;
    ValueRef operator <= (const IntegerValue& other) const final;
    ValueRef operator <= (const FloatValue& other) const final;
    ValueRef operator > (const Value& other) const final;
    ValueRef operator > (const IntegerValue& other) const final;
    ValueRef operator > (const FloatValue& other) const final;
    ValueRef operator >= (const Value& other) const final;
    ValueRef operator >= (const IntegerValue& other) const final;
    ValueRef operator >= (const FloatValue& other) const final;
    ValueRef operator == (const Value& other) const final;
    ValueRef operator == (const IntegerValue& other) const final;
    ValueRef operator == (const FloatValue& other) const final;
    ValueRef operator != (const Value& other) const final;
    ValueRef operator != (const IntegerValue& other) const final;
    ValueRef operator != (const FloatValue& other) const final;
    bool is_truthy() const final;
    IntegerValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class FloatValue : public Value {
public:
    const double value;
    FloatValue(double value);
    ValueRef operator + (const Value& other) const final;
    ValueRef operator + (const IntegerValue& other) const final;
    ValueRef operator + (const FloatValue& other) const final;
    ValueRef operator - (const Value& other) const final;
    ValueRef operator - (const IntegerValue& other) const final;
    ValueRef operator - (const FloatValue& other) const final;
    ValueRef operator - () const final;
    ValueRef operator * (const Value& other) const final;
    ValueRef operator * (const IntegerValue& other) const final;
    ValueRef operator * (const FloatValue& other) const final;
    ValueRef operator / (const Value& other) const final;
    ValueRef operator / (const IntegerValue& other) const final;
    ValueRef operator / (const FloatValue& other) const final;
    ValueRef operator % (const Value& other) const final;
    ValueRef operator % (const IntegerValue& other) const final;
    ValueRef operator % (const FloatValue& other) const final;
    ValueRef operator < (const Value& other) const final;
    ValueRef operator < (const IntegerValue& other) const final;
    ValueRef operator < (const FloatValue& other) const final;
    ValueRef operator <= (const Value& other) const final;
    ValueRef operator <= (const IntegerValue& other) const final;
    ValueRef operator <= (const FloatValue& other) const final;
    ValueRef operator > (const Value& other) const final;
    ValueRef operator > (const IntegerValue& other) const final;
    ValueRef operator > (const FloatValue& other) const final;
    ValueRef operator >= (const Value& other) const final;
    ValueRef operator >= (const IntegerValue& other) const final;
    ValueRef operator >= (const FloatValue& other) const final;
    ValueRef operator == (const Value& other) const final;
    ValueRef operator == (const IntegerValue& other) const final;
    ValueRef operator == (const FloatValue& other) const final;
    ValueRef operator != (const Value& other) const final;
    ValueRef operator != (const IntegerValue& other) const final;
    ValueRef operator != (const FloatValue& other) const final;
    bool is_truthy() const final;
    FloatValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class StringValue : public Value {
public:
    const std::string value;
    StringValue(std::string&& value);
    ValueRef operator + (const Value& other) const final;
    ValueRef operator + (const StringValue& other) const final;
    ValueRef operator * (const Value& other) const final;
    ValueRef operator * (const IntegerValue& other) const final;
    bool is_truthy() const final;
    StringValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class BooleanValue : public Value {
public:
    const bool value;
    BooleanValue(bool value);
    ValueRef operator and (const Value& other) const final;
    ValueRef operator and (const BooleanValue& other) const final;
    ValueRef operator or (const Value& other) const final;
    ValueRef operator or (const BooleanValue& other) const final;
    ValueRef operator not () const final;
    bool is_truthy() const final;
    BooleanValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class FunctionValue : public Value {
public:
    const ASTFunctionDefinition* const definition;
    FunctionValue(const ASTFunctionDefinition* definition);
    bool is_truthy() const final;
    FunctionValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class BuiltinFunctionValue : public Value {
public:
    using FuncType = std::function<ValueRef(const DictValue*)>;
    const std::string name;
    const FuncType func;
    BuiltinFunctionValue(std::string_view name, FuncType func);
    ValueRef operator () (const DictValue* args) const final;
    bool is_truthy() const final;
    BuiltinFunctionValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class ClassValue : public Value {
public:
    std::string name;
    Map properties;
    std::vector<ValueRef> implements;
    ValueRef extends;
private:
    ClassValue();
public:
    ClassValue(std::string_view name, std::vector<ValueRef> implements, ValueRef extends, Map properties);
    ClassValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class ObjectValue : public Value {
public:
    ValueRef cls;
    Map properties;
    ObjectValue(ValueRef cls);
    ValueRef get(const std::string_view property) const final;
private:
    Map InitProperties();
};

class ListValue : public ObjectValue {
private:
    static ValueRef ListClassInstance;
    static ValueRef Append(const DictValue* args);
public:
    std::vector<ValueRef> values;
    ListValue(std::vector<ValueRef>&& values);
    ValueRef operator + (const Value& other) const final;
    ValueRef operator + (const ListValue& other) const final;
    ValueRef operator * (const Value& other) const final;
    ValueRef operator * (const IntegerValue& other) const final;
    ValueRef operator [] (const Slice& indices) const final;
    bool is_truthy() const final;
    ListValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class DictValue : public ObjectValue {
public:
    static ValueRef DictClassInstance;
public:
    Map values;
    DictValue(Map&& values);
    ValueRef operator [] (const Slice& indices) const final;
    bool is_truthy() const final;
    DictValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
    decltype(auto) begin();
    decltype(auto) begin() const;
    decltype(auto) end();
    decltype(auto) end() const;
};

extern Map Builtins;
