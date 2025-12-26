#pragma once
#include "pch.hpp"
#include <string_view>

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
    static inline std::mutex print_mutex_;

private:
    static Diagnostic& current_diagnostic_() {
        thread_local Diagnostic instance;
        return instance;
    }

public:
    static void report(Problem&& problem) {
        current_diagnostic_().problems_.push_back(std::move(problem));
    }

    static void print(SourceManager& sources) {
        std::lock_guard lock(print_mutex_);
        for (const Problem& problem : current_diagnostic_().problems_) {
            output_problem(sources, problem);
        }
        std::cerr.flush();
    }

private:
    static void output_problem(
        SourceManager& sources, const Problem& problem, std::size_t indent = 0
    ) {
        std::string_view prefix;
        const char* colour;
        switch (problem.severity_) {
        case Problem::Severity::Info:
            prefix = "INFO";
            colour = ColourEscape::CYAN;
            break;
        case Problem::Severity::Warning:
            prefix = "WARNING";
            colour = ColourEscape::YELLOW;
            break;
        case Problem::Severity::Error:
            prefix = "ERROR";
            colour = ColourEscape::RED;
            break;
        }
        std::print(
            std::cerr, "{}[{}]{} {}\n", colour, prefix, ColourEscape::RESET, problem.message_
        );
        print_code(sources, problem.location_, indent);
        for (const Problem& sub_problem : problem.sub_problems()) {
            output_problem(sources, sub_problem, indent + 2);
        }
    }

    static void print_code(
        SourceManager& sources, const Location& location, std::size_t indent = 0
    ) {
        const auto& [_, content] = sources[location.id];

        std::size_t first_line_start = content.rfind('\n', location.begin);
        first_line_start = (first_line_start == std::string::npos) ? 0 : first_line_start + 1;
        std::size_t first_line_end = content.find('\n', location.begin);
        first_line_end = (first_line_end == std::string::npos) ? content.size() : first_line_end;

        std::size_t last_line_start = content.rfind('\n', location.end - 1);
        last_line_start = (last_line_start == std::string::npos) ? 0 : last_line_start + 1;
        std::size_t last_line_end = content.find('\n', location.end);
        last_line_end = (last_line_end == std::string::npos) ? content.size() : last_line_end;

        std::int64_t line_number = get_line_number(content, first_line_start);
        const std::size_t start_col = location.begin - first_line_start + 1;

        std::print(
            std::cerr,
            "{:>{}} --> {}:{}:{}\n",
            " ",
            indent,
            sources[location.id].path,
            line_number,
            start_col
        );

        std::string_view first_line_unrelevant =
            std::string_view(content.data() + first_line_start, location.begin - first_line_start);
        std::string_view first_line_relevant =
            std::string_view(content.data() + location.begin, first_line_end - location.begin);
        std::print(
            std::cerr,
            "{}{:>4} |{} {} {}{}",
            ColourEscape::DIM,
            line_number++,
            ColourEscape::RESET,
            first_line_unrelevant,
            ColourEscape::MAGENTA,
            first_line_relevant
        );

        if (first_line_start == last_line_start) {
            // Single line case
            std::string_view last_line_unrelevant =
                std::string_view(content.data() + first_line_end, last_line_end - first_line_end);
            std::print(std::cerr, "{}{}\n", ColourEscape::RESET, last_line_unrelevant);
            return;
        }

        std::string_view main_lines = std::string_view(
            content.data() + first_line_end + 1, last_line_start - first_line_end - 1
        );
        for (const auto& subrange : main_lines | std::views::split('\n')) {
            std::string_view line(
                &*subrange.begin(), static_cast<std::size_t>(std::ranges::distance(subrange))
            );
            std::print(
                std::cerr,
                "\n{}{:>4} |{} {}",
                ColourEscape::DIM,
                line_number++,
                ColourEscape::RESET,
                line
            );
        }

        std::string_view last_line_relevant =
            std::string_view(content.data() + last_line_start, location.end - last_line_start);
        std::string_view last_line_unrelevant =
            std::string_view(content.data() + location.end, last_line_end - location.end);
        std::print(
            std::cerr,
            "\n{}{:>4} |{} {} {}\n",
            ColourEscape::DIM,
            line_number,
            ColourEscape::RESET,
            last_line_relevant,
            last_line_unrelevant
        );
    }

    static std::int64_t get_line_number(const std::string& content, std::size_t where) {
        static FlatMap<void*, std::vector<std::size_t>> line_cache;
        auto it = line_cache.find((void*)content.data());
        if (it == line_cache.end()) {
            std::vector<std::size_t> line_starts = {0};
            for (std::size_t pos = 0; pos < content.size(); ++pos) {
                if (content[pos] == '\n') {
                    line_starts.push_back(pos + 1);
                }
            }
            line_cache[(void*)content.data()] = std::move(line_starts);
            it = line_cache.find((void*)content.data());
        }
        const std::vector<std::size_t>& line_starts = it->second;
        auto line_it = std::upper_bound(line_starts.begin(), line_starts.end(), where);
        std::int64_t line_number = std::distance(line_starts.begin(), line_it);
        return line_number;
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
