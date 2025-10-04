#include "exception.hpp"

BreakException::BreakException() : std::runtime_error("Break") {}

ContinueException::ContinueException() : std::runtime_error("Continue") {}