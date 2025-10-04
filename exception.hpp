#include <stdexcept>

class BreakException : public std::runtime_error {
public:
    BreakException();
};

class ContinueException : public std::runtime_error {
public:
    ContinueException();
};