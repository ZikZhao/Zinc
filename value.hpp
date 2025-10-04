#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#define AllTypeVirtualBinaryOperator(op) \
    virtual Value* operator op (const Value& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual Value* operator op (const IntegerValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual Value* operator op (const FloatValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual Value* operator op (const StringValue& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual Value* operator op (const BooleanValue& other) const { throw std::runtime_error(#op " not implemented for this type"); };
#define AllTypeVirtualUnaryOperator(op) \
    virtual Value* operator op () const { throw std::runtime_error(#op " not implemented for this type"); };

enum LiteralType {
    LITERAL_NULL,
    LITERAL_INTEGER,
    LITERAL_FLOAT,
    LITERAL_STRING,
    LITERAL_BOOLEAN,
};

class Value;
class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;

class Value {
public:
    static Value* FromLiteral(LiteralType type, std::string_view literal);
    static BooleanValue* ObjectIs(const Value& left, const Value& right);
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
    virtual Value* operator () (std::vector<const Value*> args) const { throw std::runtime_error("Function call not implemented for this type"); };
    virtual Value* operator [] (size_t index) const { throw std::runtime_error("Indexing not implemented for this type"); };
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
    Value* operator + (const Value& other) const final;
    Value* operator + (const IntegerValue& other) const final;
    Value* operator + (const FloatValue& other) const final;
    Value* operator - (const Value& other) const final;
    Value* operator - (const IntegerValue& other) const final;
    Value* operator - (const FloatValue& other) const final;
    Value* operator - () const final;
    Value* operator * (const Value& other) const final;
    Value* operator * (const IntegerValue& other) const final;
    Value* operator * (const FloatValue& other) const final;
    Value* operator / (const Value& other) const final;
    Value* operator / (const IntegerValue& other) const final;
    Value* operator / (const FloatValue& other) const final;
    Value* operator % (const Value& other) const final;
    Value* operator % (const IntegerValue& other) const final;
    Value* operator % (const FloatValue& other) const final;
    Value* operator < (const Value& other) const final;
    Value* operator < (const IntegerValue& other) const final;
    Value* operator < (const FloatValue& other) const final;
    Value* operator <= (const Value& other) const final;
    Value* operator <= (const IntegerValue& other) const final;
    Value* operator <= (const FloatValue& other) const final;
    Value* operator > (const Value& other) const final;
    Value* operator > (const IntegerValue& other) const final;
    Value* operator > (const FloatValue& other) const final;
    Value* operator >= (const Value& other) const final;
    Value* operator >= (const IntegerValue& other) const final;
    Value* operator >= (const FloatValue& other) const final;
    Value* operator == (const Value& other) const final;
    Value* operator == (const IntegerValue& other) const final;
    Value* operator == (const FloatValue& other) const final;
    Value* operator != (const Value& other) const final;
    Value* operator != (const IntegerValue& other) const final;
    Value* operator != (const FloatValue& other) const final;
    bool is_truthy() const final;
    IntegerValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class FloatValue : public Value {
public:
    const double value;
    FloatValue(double value);
    Value* operator + (const Value& other) const final;
    Value* operator + (const IntegerValue& other) const final;
    Value* operator + (const FloatValue& other) const final;
    Value* operator - (const Value& other) const final;
    Value* operator - (const IntegerValue& other) const final;
    Value* operator - (const FloatValue& other) const final;
    Value* operator - () const final;
    Value* operator * (const Value& other) const final;
    Value* operator * (const IntegerValue& other) const final;
    Value* operator * (const FloatValue& other) const final;
    Value* operator / (const Value& other) const final;
    Value* operator / (const IntegerValue& other) const final;
    Value* operator / (const FloatValue& other) const final;
    Value* operator % (const Value& other) const final;
    Value* operator % (const IntegerValue& other) const final;
    Value* operator % (const FloatValue& other) const final;
    Value* operator < (const Value& other) const final;
    Value* operator < (const IntegerValue& other) const final;
    Value* operator < (const FloatValue& other) const final;
    Value* operator <= (const Value& other) const final;
    Value* operator <= (const IntegerValue& other) const final;
    Value* operator <= (const FloatValue& other) const final;
    Value* operator > (const Value& other) const final;
    Value* operator > (const IntegerValue& other) const final;
    Value* operator > (const FloatValue& other) const final;
    Value* operator >= (const Value& other) const final;
    Value* operator >= (const IntegerValue& other) const final;
    Value* operator >= (const FloatValue& other) const final;
    Value* operator == (const Value& other) const final;
    Value* operator == (const IntegerValue& other) const final;
    Value* operator == (const FloatValue& other) const final;
    Value* operator != (const Value& other) const final;
    Value* operator != (const IntegerValue& other) const final;
    Value* operator != (const FloatValue& other) const final;
    bool is_truthy() const final;
    FloatValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class StringValue : public Value {
public:
    const std::string value;
    StringValue(std::string&& value);
    Value* operator + (const Value& other) const final;
    Value* operator + (const StringValue& other) const final;
    Value* operator * (const Value& other) const final;
    Value* operator * (const IntegerValue& other) const final;
    bool is_truthy() const final;
    StringValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};

class BooleanValue : public Value {
public:
    const bool value;
    BooleanValue(bool value);
    Value* operator and (const Value& other) const final;
    Value* operator and (const BooleanValue& other) const final;
    Value* operator or (const Value& other) const final;
    Value* operator or (const BooleanValue& other) const final;
    Value* operator not () const final;
    bool is_truthy() const final;
    BooleanValue* adapt_for_assignment(const Value& other) const final;
    operator std::string () const final;
};
