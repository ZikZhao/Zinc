#include <stdexcept>

class BreakException : public std::runtime_error {
public:
    BreakException();
};

class ContinueException : public std::runtime_error {
public:
    ContinueException();
};

class ReturnException : public std::runtime_error {
public:
    ValueRef return_value;
    ReturnException(ValueRef return_value);
};