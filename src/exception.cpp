#include "pch.hpp"
#include "exception.hpp"

Exception::Exception()
    : std::runtime_error("") {}
Exception::Exception(std::string_view message)
    : std::runtime_error(std::string(message)) {}

BreakException::BreakException() : Exception("Break") {}

ContinueException::ContinueException() : Exception("Continue") {}

TypeException::TypeException(std::string_view expected, std::string_view actual)
    : Exception("Type error: expected " + std::string(expected) + ", got " + std::string(actual)) {}

TypeException::TypeException(std::string_view message)
    : Exception(message) {}

VariableException::VariableException(std::string_view name)
    : Exception("Variable not found: " + std::string(name)) {}

ArgumentException::ArgumentException(std::string_view key) : Exception("Argument not found: " + std::string(key)) {}
