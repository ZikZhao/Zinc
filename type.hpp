#pragma once
#include "pch.hpp"
#include "ref.hpp"
#include "value.hpp"

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

class BuiltinFunctionSignature;

using TypeRef = Reference<Type>;

class Type {
public:
    virtual ~Type() = default;
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

class BuiltinFunctionSignature {
private:
    std::vector<std::pair<std::string, TypeRef>> parameters;
    std::pair<std::string, TypeRef> spread_parameter;
    TypeRef return_type;
public:
    BuiltinFunctionSignature(std::vector<std::pair<std::string, TypeRef>> params, std::pair<std::string, TypeRef> spread_param, TypeRef ret_type);
    Context collect_arguments(const Arguments& args) const;
};
