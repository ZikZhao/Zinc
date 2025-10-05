#include "ref.hpp"
#include "exception.hpp"

BreakException::BreakException() : std::runtime_error("Break") {}

ContinueException::ContinueException() : std::runtime_error("Continue") {}

ReturnException::ReturnException(ValueRef return_value) : std::runtime_error("Return"), return_value(return_value) {}
