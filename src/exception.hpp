#pragma once
#include "pch.hpp"

#include "source.hpp"

class Exception : public std::runtime_error {
public:
    Location location_;
    Exception();
    Exception(std::string_view message);
};

class BreakException : public Exception {
public:
    BreakException();
};

class ContinueException : public Exception {
public:
    ContinueException();
};

template <typename TargetType>
class ReturnException : public Exception {
public:
    TargetType value_;
    ReturnException(TargetType value) : Exception("Return"), value_(value) {}
};

class TypeException : public Exception {
public:
    TypeException(std::string_view expected, std::string_view actual);
    TypeException(std::string_view message);
};

class VariableException : public Exception {
public:
    VariableException(std::string_view name);
};

class ArgumentException : public Exception {
public:
    ArgumentException(std::string_view key);
};
