#pragma once
#include "pch.hpp"

class BreakException : public std::runtime_error {
public:
    BreakException();
};

class ContinueException : public std::runtime_error {
public:
    ContinueException();
};

template<typename TargetType>
class ReturnException : public std::runtime_error {
public:
    TargetType return_value;
    ReturnException(TargetType return_value) : std::runtime_error("Return"), return_value(return_value) {}
};

class TypeException : public std::runtime_error {
public:
    TypeException(const std::string_view expected, const std::string_view actual);
    TypeException(const std::string_view message);
};

class ArgumentException : public std::runtime_error {
public:
    ArgumentException(const std::string_view key);
};
