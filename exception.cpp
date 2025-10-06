#include "exception.hpp"

BreakException::BreakException() : std::runtime_error("Break") {}

ContinueException::ContinueException() : std::runtime_error("Continue") {}

TypeException::TypeException(const std::string_view expected, const std::string_view actual)
    : std::runtime_error("Type error: expected " + std::string(expected) + ", got " + std::string(actual)) {}

TypeException::TypeException(const std::string_view message)
    : std::runtime_error("Type error: " + std::string(message)) {}

ArgumentException::ArgumentException(const std::string_view key) : std::runtime_error("Argument not found: " + std::string(key)) {}
