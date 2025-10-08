#pragma once
#include "pch.hpp"

enum Operator {
    OP_ADD,
    OP_SUBTRACT,
    OP_MULTIPLY,
    OP_DIVIDE,
    OP_MOD,
    OP_EQ,
    OP_NEQ,
    OP_LT,
    OP_LTE,
    OP_GT,
    OP_GTE,
    OP_AND,
    OP_OR,
    OP_NOT,
    OP_BITAND,
    OP_BITOR,
    OP_BITXOR,
    OP_LSHIFT,
    OP_RSHIFT,
    OP_CALL,
    OP_INDEX,
};

enum Operand {
    INTEGER,
    FLOAT,
    STRING,
    BOOLEAN,
    FUNCTION,
    LIST,
    DICT,
    SET,
    OBJECT,
};
