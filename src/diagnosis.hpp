#pragma once
#include "pch.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>

#include "source.hpp"

class Problem {
    friend class Diagnostic;

public:
    enum class Severity {
        Info,
        Warning,
        Error,
    };

private:
    Severity severity_;
    Location location_;
    std::string message_;

protected:
    Problem() = delete;
    Problem(Severity severity, Location location, std::string message)
        : severity_(severity), location_(location), message_(std::move(message)) {}
    std::generator<Problem> sub_problems() const noexcept { co_return; }
};

class NameError : public Problem {
protected:
    NameError(const Location& location, std::string message)
        : Problem(Severity::Error, location, std::move(message)) {}
};

class UndeclaredIdentifierError final : public NameError {
public:
    UndeclaredIdentifierError(const Location& location, std::string_view identifier)
        : NameError(location, std::format("Undeclared identifier: '{}'", identifier)) {}
};

class RedeclaredIdentifierError final : public NameError {
public:
    RedeclaredIdentifierError(const Location& location, std::string_view identifier)
        : NameError(location, std::format("Redeclared identifier: '{}'", identifier)) {}
};

class SymbolCategoryMismatchError final : public NameError {
public:
    SymbolCategoryMismatchError(const Location& location, bool expected_is_type)
        : NameError(
              location,
              std::format(
                  "Symbol kind mismatch: expected {}, got {}",
                  expected_is_type ? "type" : "value",
                  !expected_is_type ? "type" : "value"
              )
          ) {}
};

class TypeError : public Problem {
protected:
    TypeError(const Location& location, std::string message)
        : Problem(Severity::Error, location, std::move(message)) {}
};

class TypeMismatchError final : public TypeError {
public:
    TypeMismatchError(const Location& location, std::string_view expected, std::string_view actual)
        : TypeError(
              location, std::format("Type mismatch: expected '{}', got '{}'", expected, actual)
          ) {}
};

class OperationNotDefinedError final : public TypeError {
public:
    OperationNotDefinedError(
        const Location& location,
        std::string_view operator_repr,
        std::string_view left_repr,
        std::string_view right_repr = ""
    )
        : TypeError(
              location,
              right_repr.size()
                  ? std::format("Undefined operator '{}' for type '{}'", operator_repr, left_repr)
                  : std::format(
                        "Undefined operator '{}' for types '{}' and '{}'",
                        operator_repr,
                        left_repr,
                        right_repr
                    )
          ) {}
};

class NotCallableError final : public TypeError {
public:
    NotCallableError(const Location& location, std::string_view type_repr)
        : TypeError(location, std::format("Type '{}' is not callable", type_repr)) {}
};

class ArgumentMismatchError final : public TypeError {
public:
    ArgumentMismatchError(
        const Location& location, std::size_t expected_count, std::size_t actual_count
    )
        : TypeError(
              location,
              std::format(
                  "Argument count mismatch: expected {}, got {}", expected_count, actual_count
              )
          ) {}
};

class CircularTypeDependencyError final : public TypeError {
public:
    CircularTypeDependencyError(const Location& location, std::string_view type_name)
        : TypeError(
              location, std::format("Circular type definition detected for type '{}'", type_name)
          ) {}
};

class ImmutableMutationError final : public TypeError {
public:
    ImmutableMutationError(const Location& location)
        : TypeError(location, std::format("Attempted mutation of immutable value")) {}
};

class InvalidAssignmentTargetError final : public TypeError {
public:
    InvalidAssignmentTargetError(const Location& location)
        : TypeError(location, "Left-hand side of assignment is not an lvalue") {}
};

class DivisionByZeroError final : public TypeError {
public:
    DivisionByZeroError(const Location& location)
        : TypeError(location, "Division by zero in constant expression") {}
};

class ShiftByNegativeError final : public TypeError {
public:
    ShiftByNegativeError(const Location& location)
        : TypeError(location, "Shift by negative amount") {}
};

class MultiplyStringByNonPositiveIntegerError final : public TypeError {
public:
    MultiplyStringByNonPositiveIntegerError(const Location& location)
        : TypeError(location, "Cannot multiply string by non-positive integer") {}
};

class CompileTimeEvaluationError : public Problem {
public:
    CompileTimeEvaluationError(const Location& location, std::string_view message)
        : Problem(Severity::Error, location, std::string(message)) {}
};

class NotConstantExpressionError final : public CompileTimeEvaluationError {
public:
    NotConstantExpressionError(const Location& location)
        : CompileTimeEvaluationError(location, "Expression is not a constant expression") {}
};

class Diagnostic {
private:
    static inline std::mutex output_mutex_;

private:
    static Diagnostic& current_diagnostic_() {
        thread_local Diagnostic instance;
        return instance;
    }

public:
    static void report(Problem&& problem) {
        current_diagnostic_().problems_.push_back(std::move(problem));
    }

    static void output() {
        std::lock_guard lock(output_mutex_);
        for (const Problem& problem : current_diagnostic_().problems_) {
            std::print(
                std::cerr,
                "Error at {}:{}-{}: {}\n",
                problem.location_.id,
                problem.location_.begin,
                problem.location_.end,
                problem.message_
            );
        }
        std::cerr.flush();
    }

private:
    std::vector<Problem> problems_;
};

class UnlocatedProblem : std::runtime_error {
public:
    template <typename T, typename... Args>
        requires std::is_base_of_v<Problem, T> && std::is_constructible_v<T, Location, Args...>
    static UnlocatedProblem make(Args&&... args) {
        std::move_only_function<void(Location) &&> callback =
            [... params = std::forward<Args>(args)](Location loc) {
                Diagnostic::report(T(loc, std::move(params)...));
            };
        return UnlocatedProblem(std::move(callback));
    }

private:
    std::move_only_function<void(Location) &&> callback_;

public:
    UnlocatedProblem(decltype(callback_)&& callback)
        : std::runtime_error(""), callback_(std::move(callback)) {}
    void report_at(Location location) { std::move(callback_)(location); }
};
