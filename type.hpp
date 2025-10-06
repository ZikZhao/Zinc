#pragma once
#include "pch.hpp"
#include "ref.hpp"
#define AllTypeVirtualBinaryOperator(op) \
    virtual TypeRef operator op (const Type& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const IntegerType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const FloatType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const StringType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const BooleanType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const FunctionType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const ListType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const DictType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const SetType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const IntersectionType& other) const { throw std::runtime_error(#op " not implemented for this type"); }; \
    virtual TypeRef operator op (const UnionType& other) const { throw std::runtime_error(#op " not implemented for this type"); };
#define AllTypeVirtualUnaryOperator(op) \
    virtual TypeRef operator op () const { throw std::runtime_error(#op " not implemented for this type"); };

class Type;
class NullType;
class IntegerType;
class FloatType;
class StringType;
class BooleanType;
class FunctionType;
class ObjectType;
class ListType;
class DictType;
class SetType;
class IntersectionType;
class UnionType;

using TypeRef = Reference<Type>;

class Type {
public:
    virtual ~Type() = default;
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
    AllTypeVirtualBinaryOperator(<<);
    AllTypeVirtualBinaryOperator(>>);
    virtual bool contains(const Type& other) const = 0;
    virtual operator std::string () const = 0;
};

class NullType : public Type {
public:
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class IntegerType : public Type {
public:
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class FloatType : public Type {
public:
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class StringType : public Type {
public:
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class BooleanType : public Type {
public:
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class FunctionType : public Type {
public:
    std::vector<TypeRef> parameter_types;
    TypeRef return_type;
    FunctionType(std::vector<TypeRef>&& parameter_types, TypeRef return_type);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class ObjectType : public Type {
public:
    std::string name;
    std::vector<TypeRef> interfaces;
    TypeRef extends;
    Map<TypeRef> property_types;
    ObjectType(std::string_view name, std::vector<TypeRef>&& interfaces, TypeRef extends, Map<TypeRef>&& property_types);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class ListType : public Type {
public:
    TypeRef element_type;
    ListType(TypeRef element_type);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class DictType : public Type {
public:
    TypeRef key_type;
    TypeRef value_type;
    DictType(TypeRef key_type, TypeRef value_type);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class SetType : public Type {
public:
    TypeRef element_type;
    SetType(TypeRef element_type);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class IntersectionType : public Type {
public:
    std::vector<TypeRef> types;
    IntersectionType(std::vector<TypeRef>&& types);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

class UnionType : public Type {
public:
    std::vector<TypeRef> types;
    UnionType(std::vector<TypeRef>&& types);
    virtual bool contains(const Type& other) const final;
    operator std::string () const final;
};

#undef AllTypeVirtualBinaryOperator
#undef AllTypeVirtualUnaryOperator