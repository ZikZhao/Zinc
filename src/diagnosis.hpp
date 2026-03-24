#pragma once
#include "pch.hpp"

#include "source.hpp"

enum class Severity : std::uint8_t {
    Info,
    Warning,
    Error,
};

struct Problem {
    Severity severity;
    Location location;
    GlobalMemory::String message;
    GlobalMemory::Vector<Problem> subproblems;
};

class Diagnostic {
    friend class ThreadGuard;

public:
    class ErrorTrap {
        friend class Diagnostic;

    private:
        Location location_;
        GlobalMemory::Vector<Problem> problems_;
        ErrorTrap* prev_;

    public:
        ErrorTrap(Location location) noexcept
            : location_(location), prev_(std::exchange(instance->current_trap_, this)) {}

        ~ErrorTrap() noexcept {
            GlobalMemory::Vector<Problem>& target =
                (prev_) ? prev_->problems_ : instance->problems_;
            for (Problem& problem : problems_) {
                if (problem.location.begin == problem.location.end) {
                    problem.location = location_;
                }
                target.push_back(std::move(problem));
            }
            instance->current_trap_ = prev_;
        }

        auto conclude() noexcept -> void {
            Problem main_problem = std::move(problems_.back());
            problems_.pop_back();
            main_problem.subproblems = std::move(problems_);
            problems_.clear();
            problems_.push_back(std::move(main_problem));
        }
    };

private:
    static inline std::mutex print_mutex_;
    static thread_local std::optional<Diagnostic> instance;

public:
    static void report(Problem&& problem) {
        assert(instance->current_trap_);
        instance->current_trap_->problems_.push_back(std::move(problem));
    }

    static void report_subproblem(Problem&& problem) {
        assert(instance->current_trap_ && !instance->current_trap_->problems_.empty());
        instance->current_trap_->problems_.back().subproblems.push_back(std::move(problem));
    }

    static auto print(SourceManager& sources) -> bool {
        std::lock_guard lock(print_mutex_);
        std::size_t error_count = 0;
        std::size_t warning_count = 0;
        for (const Problem& problem : instance->problems_) {
            print_problem(sources, problem);
            switch (problem.severity) {
            case Severity::Error:
                ++error_count;
                break;
            case Severity::Warning:
                ++warning_count;
                break;
            case Severity::Info:
                break;
            }
            std::cout << "\n";
        }

        if (error_count == 0 && warning_count == 0) return false;
        std::print(
            std::cerr,
            "{}{} error(s){}, {}{} warning(s){} generated.\n",
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

    static void print_info_msg(strview msg) {
        std::lock_guard lock(print_mutex_);
        std::print(std::cout, "{}[INFO]{} {}\n", ColourEscape::CYAN, ColourEscape::RESET, msg);
        std::cout.flush();
    }

    static void print_error_msg(strview msg) {
        std::lock_guard lock(print_mutex_);
        std::print(std::cerr, "{}[ERROR]{} {}\n", ColourEscape::RED, ColourEscape::RESET, msg);
        std::cerr.flush();
    }

private:
    static void print_problem(
        SourceManager& sources, const Problem& problem, std::size_t indent = 0
    ) {
        strview prefix;
        const char* colour = nullptr;
        switch (problem.severity) {
        case Severity::Info:
            prefix = "INFO";
            colour = ColourEscape::CYAN;
            break;
        case Severity::Warning:
            prefix = "WARNING";
            colour = ColourEscape::YELLOW;
            break;
        case Severity::Error:
            prefix = "ERROR";
            colour = ColourEscape::RED;
            break;
        }
        std::print(
            std::cerr, "{}[{}]{} {}\n", colour, prefix, ColourEscape::RESET, problem.message
        );
        print_code(sources, problem.location, indent + 2);
        for (const Problem& sub_problem : problem.subproblems) {
            print_problem(sources, sub_problem, indent + 2);
        }
    }

    static void print_code(
        SourceManager& sources, const Location& location, std::size_t indent = 0
    ) {
        const SourceFile& file = sources[location.id];
        strview content = file.content_;

        std::size_t context_start = content.rfind('\n', location.begin);
        context_start = (context_start == GlobalMemory::String::npos) ? 0 : context_start + 1;

        std::size_t context_end = content.find('\n', location.end);
        context_end = (context_end == GlobalMemory::String::npos) ? content.size() : context_end;

        std::int64_t start_line_num = file.get_line_number(context_start);
        std::int64_t end_line_num = file.get_line_number(context_end);
        std::size_t line_num_width = std::formatted_size("{}", end_line_num);

        std::size_t col_num = location.begin - context_start + 1;
        std::print(
            std::cerr,
            "{:{}}{}{}{}:{}:{}{}\n",
            "",
            indent,
            ColourEscape::DIM,
            ColourEscape::UNDERLINE,
            file.relative_path_
                .string<char, std::char_traits<char>, GlobalMemory::String::allocator_type>(),
            start_line_num,
            col_num,
            ColourEscape::RESET
        );

        auto print_line =
            [&](std::int64_t line_idx, strview prefix, strview match, strview suffix) {
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

        strview full_context = strview(content).substr(context_start, context_end - context_start);

        std::size_t current_pos_in_file = context_start;
        std::int64_t current_line_num = start_line_num;

        for (const auto& line_range : std::views::split(full_context, '\n')) {
            strview line(
                &*line_range.begin(), static_cast<std::size_t>(std::ranges::distance(line_range))
            );
            std::size_t line_start = current_pos_in_file;
            std::size_t line_end = line_start + line.size();
            std::size_t hl_start = std::clamp<std::size_t>(location.begin, line_start, line_end);
            std::size_t hl_end = std::clamp<std::size_t>(location.end, line_start, line_end);

            strview prefix = line.substr(0, hl_start - line_start);
            strview match = line.substr(hl_start - line_start, hl_end - hl_start);
            strview suffix = line.substr(hl_end - line_start);

            print_line(current_line_num++, prefix, match, suffix);

            current_pos_in_file += line.size() + 1;
        }
    }

private:
    GlobalMemory::Vector<Problem> problems_;
    ErrorTrap* current_trap_ = nullptr;

public:
    Diagnostic() noexcept = default;

private:
    static auto unlocated_error(GlobalMemory::String&& msg) noexcept -> void {
        Diagnostic::report(Problem{.severity = Severity::Error, .message = std::move(msg)});
    }

public:
    static auto error_undeclared_identifier(strview identifier) noexcept -> void {
        unlocated_error(GlobalMemory::format("Undeclared identifier: '{}'", identifier));
    }

    static auto error_redeclared_identifier(strview identifier) noexcept -> void {
        unlocated_error(GlobalMemory::format("Redeclared identifier: '{}'", identifier));
    }

    static auto error_symbol_category_mismatch(strview expected, strview actual) noexcept -> void {
        unlocated_error(
            GlobalMemory::format("Symbol kind mismatch: expected {}, got {}", expected, actual)
        );
    }

    static auto error_symbol_category_mismatch(
        Location location, strview expected, strview actual
    ) noexcept -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::format(
                    "Symbol kind mismatch: expected '{}', got '{}'", expected, actual
                )
            }
        );
    }

    static auto error_circular_type_dependency(Location location) noexcept -> void {
        unlocated_error(GlobalMemory::format("Circular type definition detected:"));
    }

    static auto error_duplicate_attribute(Location location, strview attribute_name) noexcept
        -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::format("Duplicate attribute name: '{}'", attribute_name)
            }
        );
    }

    static auto error_decl_of_duplicate_attribute(Location location) noexcept -> void {
        Diagnostic::report_subproblem(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::String("Previous declaration of attribute is here:"sv)
            }
        );
    }

    static auto error_overflow(auto literal, strview type) noexcept -> void {
        unlocated_error(GlobalMemory::format("Literal '{}' overflows type '{}'", literal, type));
    }

    static auto error_uninitialized_attribute(strview attribute_name) noexcept -> void {
        unlocated_error(
            GlobalMemory::format(
                "Attribute '{}' is not initialized and is not default constructible", attribute_name
            )
        );
    }

    static auto error_unrecognized_attribute(strview attribute_name) noexcept -> void {
        unlocated_error(GlobalMemory::format("Unrecognized attribute: '{}'", attribute_name));
    }

    static auto error_type_mismatch(strview expected, strview actual) noexcept -> void {
        unlocated_error(
            GlobalMemory::format("Type mismatch: expected '{}', got '{}'", expected, actual)
        );
    }

    static auto error_operation_not_defined(
        strview operator_repr, strview left_type_repr, strview right_type_repr = ""
    ) noexcept -> void {
        if (right_type_repr.empty()) {
            unlocated_error(
                GlobalMemory::format(
                    "Undefined operator '{}' for type '{}'", operator_repr, left_type_repr
                )
            );
        } else {
            unlocated_error(
                GlobalMemory::format(
                    "Undefined operator '{}' for types '{}' and '{}'",
                    operator_repr,
                    left_type_repr,
                    right_type_repr
                )
            );
        }
    }

    static auto error_no_matching_overload(Location location, strview args) noexcept -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::format("No matching overload found, arguments: {}"sv, args)
            }
        );
    }

    static auto error_candidate_argument_count_mismatch(
        strview signature, std::size_t expected, std::size_t actual
    ) noexcept -> void {
        unlocated_error(
            GlobalMemory::format(
                "Candidate with mismatching argument count: expected {}, got {}",
                signature,
                expected,
                actual
            )
        );
    }

    static auto error_candidate_type_mismatch(
        strview signature, std::size_t index, strview expected, strview actual
    ) noexcept -> void {
        unlocated_error(
            GlobalMemory::format(
                "Candidate with mismatching argument types at index {}: expected '{}', got '{}'",
                signature,
                index,
                expected,
                actual
            )
        );
    }

    static auto error_invalid_literal(strview literal, strview type) noexcept -> void {
        unlocated_error(GlobalMemory::format("Invalid literal '{}' for type '{}'", literal, type));
    }

    static auto error_not_constant_expression(Location location) noexcept -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::String("Expression is not a constant expression"sv)
            }
        );
    }

    static auto error_construct_instance_out_of_class(
        Location location, strview class_name
    ) noexcept -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::format(
                    "Cannot construct instance of class '{}' outside of its scope, calling "
                    "constructor instead",
                    class_name
                )
            }
        );
    }

    static auto error_invalid_cast(Location location, strview from_type, strview to_type) noexcept
        -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message =
                    GlobalMemory::format("Invalid cast from '{}' to '{}'", from_type, to_type)
            }
        );
    }

    static auto error_array_initializer_size_mismatch(
        Location location, std::size_t expected, std::size_t actual
    ) noexcept -> void {
        Diagnostic::report(
            Problem{
                .severity = Severity::Error,
                .location = location,
                .message = GlobalMemory::format(
                    "Array initializer size mismatch: expected {}, got {}", expected, actual
                )
            }
        );
    }
};

inline thread_local std::optional<Diagnostic> Diagnostic::instance;
