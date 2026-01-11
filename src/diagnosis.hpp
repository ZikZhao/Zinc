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
    std::string_view message_;

protected:
    Problem() = delete;
    Problem(Severity severity, Location location, std::string_view message)
        : severity_(severity), location_(location), message_(message) {}
    std::generator<Problem> sub_problems() const noexcept { co_return; }
};

class NameError : public Problem {
protected:
    NameError(const Location& location, std::string_view message)
        : Problem(Severity::Error, location, message) {}
};

class UndeclaredIdentifierError final : public NameError {
public:
    UndeclaredIdentifierError(const Location& location, std::string_view identifier)
        : NameError(location, GlobalMemory::format("Undeclared identifier: '{}'", identifier)) {}
};

class RedeclaredIdentifierError final : public NameError {
public:
    RedeclaredIdentifierError(const Location& location, std::string_view identifier)
        : NameError(location, GlobalMemory::format("Redeclared identifier: '{}'", identifier)) {}
};

class SymbolCategoryMismatchError final : public NameError {
public:
    SymbolCategoryMismatchError(const Location& location, bool expected_is_type)
        : NameError(
              location,
              GlobalMemory::format(
                  "Symbol kind mismatch: expected {}, got {}",
                  expected_is_type ? "type" : "value",
                  !expected_is_type ? "type" : "value"
              )
          ) {}
};

class TypeError : public Problem {
protected:
    TypeError(const Location& location, std::string_view message)
        : Problem(Severity::Error, location, message) {}
};

class TypeMismatchError final : public TypeError {
public:
    TypeMismatchError(const Location& location, std::string_view expected, std::string_view actual)
        : TypeError(
              location,
              GlobalMemory::format("Type mismatch: expected '{}', got '{}'", expected, actual)
          ) {}
};

class OperationNotDefinedError final : public TypeError {
public:
    OperationNotDefinedError(
        const Location& location,
        std::string_view operator_repr,
        std::string_view left_type_repr,
        std::string_view right_type_repr = ""
    )
        : TypeError(
              location,
              right_type_repr.size()
                  ? GlobalMemory::format(
                        "Undefined operator '{}' for types '{}' and '{}'",
                        operator_repr,
                        left_type_repr,
                        right_type_repr
                    )
                  : GlobalMemory::format(
                        "Undefined operator '{}' for type '{}'", operator_repr, left_type_repr
                    )
          ) {}
};

class NotCallableError final : public TypeError {
public:
    NotCallableError(const Location& location, std::string_view type_repr)
        : TypeError(location, GlobalMemory::format("Type '{}' is not callable", type_repr)) {}
};

class ArgumentMismatchError final : public TypeError {
public:
    ArgumentMismatchError(
        const Location& location, std::size_t expected_count, std::size_t actual_count
    )
        : TypeError(
              location,
              GlobalMemory::format(
                  "Argument count mismatch: expected {}, got {}", expected_count, actual_count
              )
          ) {}
};

class CircularTypeDependencyError final : public TypeError {
public:
    CircularTypeDependencyError(const Location& location)
        : TypeError(location, GlobalMemory::format("Circular type definition detected")) {}
};

class ImmutableMutationError final : public TypeError {
public:
    ImmutableMutationError(const Location& location)
        : TypeError(location, GlobalMemory::format("Attempted mutation of immutable value")) {}
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

class AttributeError : public TypeError {
public:
    AttributeError(const Location& location, std::string_view message)
        : TypeError(location, message) {}
};

class CompileTimeEvaluationError : public Problem {
public:
    CompileTimeEvaluationError(const Location& location, std::string_view message)
        : Problem(Severity::Error, location, message) {}
};

class InvalidLiteralError final : public CompileTimeEvaluationError {
public:
    InvalidLiteralError(const Location& location, std::string_view literal, std::string_view type)
        : CompileTimeEvaluationError(
              location, GlobalMemory::format("Invalid literal '{}' for type '{}'", literal, type)
          ) {}
};

class OverflowError final : public CompileTimeEvaluationError {
public:
    OverflowError(const Location& location, std::string_view literal, std::string_view type)
        : CompileTimeEvaluationError(
              location,
              GlobalMemory::format("Literal '{}' overflows the range of type '{}'", literal, type)
          ) {}
};

class NotConstantExpressionError final : public CompileTimeEvaluationError {
public:
    NotConstantExpressionError(const Location& location)
        : CompileTimeEvaluationError(location, "Expression is not a constant expression") {}
};

class Diagnostic {
private:
    static inline std::mutex print_mutex_;
    static inline std::vector<Problem> problems_;

public:
    static void report(Problem&& problem) { problems_.push_back(std::move(problem)); }

    static bool print(SourceManager& sources) {
        std::lock_guard lock(print_mutex_);
        std::size_t error_count = 0;
        std::size_t warning_count = 0;
        for (const Problem& problem : problems_) {
            print_problem(sources, problem);
            switch (problem.severity_) {
            case Problem::Severity::Error:
                ++error_count;
                break;
            case Problem::Severity::Warning:
                ++warning_count;
                break;
            case Problem::Severity::Info:
                break;
            }
        }

        if (error_count == 0 && warning_count == 0) return false;
        std::print(
            std::cerr,
            "\n{}{} error(s){}, {}{} warning(s){} generated.\n",
            ColourEscape::RED,
            error_count,
            ColourEscape::RESET,
            ColourEscape::YELLOW,
            warning_count,
            ColourEscape::RESET
        );
        std::cerr.flush();

        return true;
    }

    static void message(std::string_view msg) {
        std::lock_guard lock(print_mutex_);
        std::print(std::cout, "{}[INFO]{} {}\n", ColourEscape::CYAN, ColourEscape::RESET, msg);
        std::cout.flush();
    }

private:
    static void print_problem(
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
        print_code(sources, problem.location_, indent + 2);
        for (const Problem& sub_problem : problem.sub_problems()) {
            print_problem(sources, sub_problem, indent + 2);
        }
    }

    static void print_code(
        SourceManager& sources, const Location& location, std::size_t indent = 0
    ) {
        const auto& [_, path, content, line_offsets] = sources[location.id];

        std::size_t context_start = content.rfind('\n', location.begin);
        context_start = (context_start == GlobalMemory::String::npos) ? 0 : context_start + 1;

        std::size_t context_end = content.find('\n', location.end);
        context_end = (context_end == GlobalMemory::String::npos) ? content.size() : context_end;

        auto get_line_number = [&](std::size_t where) -> std::int64_t {
            return static_cast<std::int64_t>(std::distance(
                line_offsets.begin(),
                std::upper_bound(line_offsets.begin(), line_offsets.end(), where)
            ));
        };
        std::int64_t start_line_num = get_line_number(context_start);
        std::int64_t end_line_num = get_line_number(context_end);
        std::size_t line_num_width = std::formatted_size("{}", end_line_num);

        std::size_t col_num = location.begin - context_start + 1;
        std::print(
            std::cerr,
            "{:{}}{}{}{}:{}:{}{}\n",
            "",
            indent,
            ColourEscape::DIM,
            ColourEscape::UNDERLINE,
            path,
            start_line_num,
            col_num,
            ColourEscape::RESET
        );

        auto print_line = [&](std::int64_t line_idx,
                              std::string_view prefix,
                              std::string_view match,
                              std::string_view suffix) {
            std::print(
                std::cerr,
                "{:{}}{}{:>{}} |{}",
                "",
                indent,
                ColourEscape::DIM,
                line_idx,
                line_num_width,
                ColourEscape::RESET
            );

            if (!prefix.empty())
                std::print(std::cerr, " {}", prefix);
            else
                std::print(std::cerr, " ");

            if (!match.empty()) {
                std::print(
                    std::cerr, "{}{}{}", ColourEscape::HI_MAGENTA, match, ColourEscape::RESET
                );
            }

            if (!suffix.empty()) std::print(std::cerr, "{}", suffix);

            std::print(std::cerr, "\n");
        };

        std::string_view full_context =
            std::string_view(content).substr(context_start, context_end - context_start);

        std::size_t current_pos_in_file = context_start;
        std::int64_t current_line_num = start_line_num;

        for (const auto& line_range : std::views::split(full_context, '\n')) {
            std::string_view line(
                &*line_range.begin(), static_cast<std::size_t>(std::ranges::distance(line_range))
            );
            std::size_t line_start = current_pos_in_file;
            std::size_t line_end = line_start + line.size();
            std::size_t hl_start = std::clamp<std::size_t>(location.begin, line_start, line_end);
            std::size_t hl_end = std::clamp<std::size_t>(location.end, line_start, line_end);

            std::string_view prefix = line.substr(0, hl_start - line_start);
            std::string_view match = line.substr(hl_start - line_start, hl_end - hl_start);
            std::string_view suffix = line.substr(hl_end - line_start);

            print_line(current_line_num++, prefix, match, suffix);

            current_pos_in_file += line.size() + 1;
        }
    }

public:
    Diagnostic() = delete;
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
