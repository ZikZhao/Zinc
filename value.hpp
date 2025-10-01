#pragma once
#include <stdexcept>
#include <string>
#include <vector>
#include "type.hpp"
#include "ref.hpp"

class Value;
class NullValue;
class IntegerValue;
class FloatValue;
class StringValue;
class BooleanValue;

using ValueRef = Reference<Value>;

class Value {
public:
    TypeRef type;
    Value(const TypeRef& type);
    static ValueRef FromLiteral(int64_t type, const char* literal);
    static BooleanValue* ObjectIs(const Value& left, const Value& right);
    virtual ~Value() = default;
    virtual Value* operator + (const Value& other) const { throw std::runtime_error("Addition not implemented for this type"); };
    virtual Value* operator - (const Value& other) const { throw std::runtime_error("Subtraction not implemented for this type"); };
    virtual Value* operator - () const { throw std::runtime_error("Negation not implemented for this type"); };
    virtual Value* operator * (const Value& other) const { throw std::runtime_error("Multiplication not implemented for this type"); };
    virtual Value* operator / (const Value& other) const { throw std::runtime_error("Division not implemented for this type"); };
    virtual Value* operator % (const Value& other) const { throw std::runtime_error("Remainder not implemented for this type"); };
    virtual Value* operator == (const Value& other) const { throw std::runtime_error("Equality not implemented for this type"); };
    virtual Value* operator != (const Value& other) const { throw std::runtime_error("Inequality not implemented for this type"); };
    virtual Value* operator < (const Value& other) const { throw std::runtime_error("Less than not implemented for this type"); };
    virtual Value* operator <= (const Value& other) const { throw std::runtime_error("Less than or equal not implemented for this type"); };
    virtual Value* operator > (const Value& other) const { throw std::runtime_error("Greater than not implemented for this type"); };
    virtual Value* operator >= (const Value& other) const { throw std::runtime_error("Greater than or equal not implemented for this type"); };
    virtual Value* operator and (const Value& other) const { throw std::runtime_error("Logical AND not implemented for this type"); };
    virtual Value* operator or (const Value& other) const { throw std::runtime_error("Logical OR not implemented for this type"); };
    virtual Value* operator & (const Value& other) const { throw std::runtime_error("Bitwise AND not implemented for this type"); };
    virtual Value* operator | (const Value& other) const { throw std::runtime_error("Bitwise OR not implemented for this type"); };
    virtual Value* operator ^ (const Value& other) const { throw std::runtime_error("Bitwise XOR not implemented for this type"); };
    virtual Value* operator ~ () const { throw std::runtime_error("Bitwise NOT not implemented for this type"); };
    virtual Value* operator not () const { throw std::runtime_error("Logical NOT not implemented for this type"); };
    virtual Value* operator () (std::vector<const Value*> args) const { throw std::runtime_error("Function call not implemented for this type"); };
    virtual Value* operator [] (size_t index) const { throw std::runtime_error("Indexing not implemented for this type"); };
    virtual operator std::string () const = 0;
};

class NullValue : public Value {
public:
    static const TypeRef TypeInstance;
    static const ValueRef Instance;
private:
    NullValue();
public:
    operator std::string () const override;
};

class IntegerValue : public Value {
private:
    static const TypeRef TypeInstance;
public:
    const int64_t value;
    IntegerValue(int64_t value);
    Value* operator + (const Value& other) const override;
    Value* operator + (const IntegerValue& other) const;
    Value* operator + (const FloatValue& other) const;
    Value* operator - (const Value& other) const override;
    Value* operator - (const IntegerValue& other) const;
    Value* operator - (const FloatValue& other) const;
    Value* operator - () const override;
    Value* operator * (const Value& other) const override;
    Value* operator * (const IntegerValue& other) const;
    Value* operator * (const FloatValue& other) const;
    Value* operator / (const Value& other) const override;
    Value* operator / (const IntegerValue& other) const;
    Value* operator / (const FloatValue& other) const;
    Value* operator % (const Value& other) const override;
    Value* operator % (const IntegerValue& other) const;
    Value* operator % (const FloatValue& other) const;
    Value* operator < (const Value& other) const override;
    Value* operator < (const IntegerValue& other) const;
    Value* operator < (const FloatValue& other) const;
    Value* operator <= (const Value& other) const override;
    Value* operator <= (const IntegerValue& other) const;
    Value* operator <= (const FloatValue& other) const;
    Value* operator > (const Value& other) const override;
    Value* operator > (const IntegerValue& other) const;
    Value* operator > (const FloatValue& other) const;
    Value* operator >= (const Value& other) const override;
    Value* operator >= (const IntegerValue& other) const;
    Value* operator >= (const FloatValue& other) const;
    Value* operator == (const Value& other) const override;
    Value* operator == (const IntegerValue& other) const;
    Value* operator == (const FloatValue& other) const;
    Value* operator != (const Value& other) const override;
    Value* operator != (const IntegerValue& other) const;
    Value* operator != (const FloatValue& other) const;
    operator std::string () const override;
};

class FloatValue : public Value {
private:
    static const TypeRef TypeInstance;
public:
    const double value;
    FloatValue(double value);
    Value* operator + (const Value& other) const override;
    Value* operator + (const IntegerValue& other) const;
    Value* operator + (const FloatValue& other) const;
    Value* operator - (const Value& other) const override;
    Value* operator - (const IntegerValue& other) const;
    Value* operator - (const FloatValue& other) const;
    Value* operator - () const override;
    Value* operator * (const Value& other) const override;
    Value* operator * (const IntegerValue& other) const;
    Value* operator * (const FloatValue& other) const;
    Value* operator / (const Value& other) const override;
    Value* operator / (const IntegerValue& other) const;
    Value* operator / (const FloatValue& other) const;
    Value* operator % (const Value& other) const override;
    Value* operator % (const IntegerValue& other) const;
    Value* operator % (const FloatValue& other) const;
    Value* operator < (const Value& other) const override;
    Value* operator < (const IntegerValue& other) const;
    Value* operator < (const FloatValue& other) const;
    Value* operator <= (const Value& other) const override;
    Value* operator <= (const IntegerValue& other) const;
    Value* operator <= (const FloatValue& other) const;
    Value* operator > (const Value& other) const override;
    Value* operator > (const IntegerValue& other) const;
    Value* operator > (const FloatValue& other) const;
    Value* operator >= (const Value& other) const override;
    Value* operator >= (const IntegerValue& other) const;
    Value* operator >= (const FloatValue& other) const;
    Value* operator == (const Value& other) const override;
    Value* operator == (const IntegerValue& other) const;
    Value* operator == (const FloatValue& other) const;
    Value* operator != (const Value& other) const override;
    Value* operator != (const IntegerValue& other) const;
    Value* operator != (const FloatValue& other) const;
    operator std::string () const override;
};

class StringValue : public Value {
private:
    static const TypeRef TypeInstance;
public:
    const std::string value;
    StringValue(std::string&& value);
    Value* operator + (const Value& other) const override;
    Value* operator + (const StringValue& other) const;
    Value* operator * (const Value& other) const override;
    Value* operator * (const IntegerValue& other) const;
    operator std::string () const override;
};

class BooleanValue : public Value {
private:
    static const TypeRef TypeInstance;
public:
    const bool value;
    BooleanValue(bool value);
    Value* operator and (const Value& other) const override;
    Value* operator and (const BooleanValue& other) const;
    Value* operator or (const Value& other) const override;
    Value* operator or (const BooleanValue& other) const;
    Value* operator not () const override;
    operator std::string () const override;
};

class Variable {
private:
    ValueRef ref;
public:
    Variable();
    Variable& operator = (const ValueRef& other);
};
