#pragma once
#include <string>
#include "ref.hpp"

class Type {
public:
    virtual ~Type() = default;
    virtual operator std::string () const = 0;
};

template<const char* Name>
class PrimitiveType : public Type {
public:
    constexpr PrimitiveType() {}
    operator std::string () const override {
        return Name;
    }
};

static constexpr const char IntegerTypeName[] = "integer";
static constexpr const char FloatTypeName[] = "float";
static constexpr const char StringTypeName[] = "string";
static constexpr const char BooleanTypeName[] = "boolean";
static constexpr const char NullTypeName[] = "null";

using IntegerType = PrimitiveType<IntegerTypeName>;
using FloatType   = PrimitiveType<FloatTypeName>;
using StringType  = PrimitiveType<StringTypeName>;
using BooleanType = PrimitiveType<BooleanTypeName>;
using NullType    = PrimitiveType<NullTypeName>;

using TypeRef = Reference<Type>;