#pragma once
// IWYU pragma: begin_exports
#include <array>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <generator>
#include <iostream>
#include <map>
#include <memory>
#include <memory_resource>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "antlr4-runtime.h"
// IWYU pragma: end_exports

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

struct Location {
    std::size_t id;
    struct {
        std::size_t line;
        std::size_t column;
    } begin, end;
};

class SourceManager {
public:
    std::map<std::string, std::string> files_;
    std::vector<std::string> file_order_;

public:
    SourceManager() = default;
    auto operator[](std::string_view filename) {
        struct SourceFile {
            std::string path;
            const std::string& content;
        };
        std::ifstream file_stream(filename.data());
        if (file_stream.fail()) {
            throw std::runtime_error("Cannot open source file: "s + filename.data());
        }
        std::string absolute_path = std::filesystem::canonical(filename).string();
        std::string content(
            (std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>()
        );
        const std::string& file_content = (files_[absolute_path] = std::move(content));
        file_order_.push_back(absolute_path);
        return SourceFile{.path = absolute_path, .content = file_content};
    }
    const std::string& operator[](std::size_t index) const noexcept {
        assert(index < file_order_.size());
        return files_.at(file_order_.at(index));
    }
    std::size_t index(std::string filename) const noexcept {
        for (std::size_t i = 0; i < file_order_.size(); ++i) {
            if (file_order_[i] == filename) {
                return i;
            }
        }
        assert(false && "File not found in SourceManager");
    }
};

class ConstantStringPool {
private:
    std::pmr::monotonic_buffer_resource buffer_;
    std::pmr::polymorphic_allocator<char> allocator_;

public:
    ConstantStringPool(std::size_t initial_size = 1024 * 1024)
        : buffer_(initial_size), allocator_(&buffer_) {}
    ~ConstantStringPool() = default;
    std::string_view make(std::string_view str) {
        char* mem = allocator_.allocate(str.size());
        std::copy_n(str.data(), str.size(), mem);
        return std::string_view(mem, str.size());
    }
    std::string_view make(const std::string& str) {
        char* mem = allocator_.allocate(str.size());
        std::copy_n(str.data(), str.size(), mem);
        return std::string_view(mem, str.size());
    }
};

template <std::size_t length>
class FixedString {
public:
    const char str_[length];
    constexpr FixedString(const char (&str)[length]) { std::copy_n(str, length, str_); }
    constexpr std::string_view operator*() const { return std::string_view(str_, length); }
};

template <typename Derived, typename Base>
    requires(std::has_virtual_destructor_v<Base>)
constexpr std::unique_ptr<Derived> StaticUniqueCast(
    std::unique_ptr<Base, std::default_delete<Base>>&& ptr
) {
    assert(dynamic_cast<Derived*>(ptr.get()) != nullptr);
    return std::unique_ptr<Derived>(static_cast<Derived*>(ptr.release()));
}

template <typename Derived, typename Base, typename Deleter>
    requires(!std::is_same_v<Deleter, std::default_delete<Base>>)
constexpr std::unique_ptr<Derived, Deleter> StaticUniqueCast(std::unique_ptr<Base, Deleter>&& ptr) {
    return std::unique_ptr<Derived, Deleter>(
        static_cast<Derived*>(ptr.release()), std::move(ptr.get_deleter())
    );
}

namespace OperatorFunctors {
struct Increment {
    template <typename T>
    auto operator()(const T& value) const {
        return value++;
    }
};
struct Decrement {
    template <typename T>
    auto operator()(const T& value) const {
        return value--;
    }
};
struct LeftShift {
    template <typename T>
    auto operator()(const T& left, const T& right) const {
        return left << right;
    }
};
struct RightShift {
    template <typename T>
    auto operator()(const T& left, const T& right) const {
        return left >> right;
    }
};
template <typename Functor = void>
struct OperateAndAssign {};
template <>
struct OperateAndAssign<void> {
    template <typename Left, typename Right>
    auto operator()(const Left& left, const Right& right) const {
        return left = right;
    }
};
using Add = std::plus<>;
using Subtract = std::minus<>;
using Negate = std::negate<>;
using Multiply = std::multiplies<>;
using Divide = std::divides<>;
using Remainder = std::modulus<>;
using Equal = std::equal_to<>;
using NotEqual = std::not_equal_to<>;
using LessThan = std::less<>;
using LessEqual = std::less_equal<>;
using GreaterThan = std::greater<>;
using GreaterEqual = std::greater_equal<>;
using LogicalAnd = std::logical_and<>;
using LogicalOr = std::logical_or<>;
using LogicalNot = std::logical_not<>;
using BitwiseAnd = std::bit_and<>;
using BitwiseOr = std::bit_or<>;
using BitwiseXor = std::bit_xor<>;
using BitwiseNot = std::bit_not<>;
using Assign = OperateAndAssign<>;
using AddAssign = OperateAndAssign<Add>;
using SubtractAssign = OperateAndAssign<Subtract>;
using MultiplyAssign = OperateAndAssign<Multiply>;
using DivideAssign = OperateAndAssign<Divide>;
using RemainderAssign = OperateAndAssign<Remainder>;
using LogicalAndAssign = OperateAndAssign<LogicalAnd>;
using LogicalOrAssign = OperateAndAssign<LogicalOr>;
using BitwiseAndAssign = OperateAndAssign<BitwiseAnd>;
using BitwiseOrAssign = OperateAndAssign<BitwiseOr>;
using BitwiseXorAssign = OperateAndAssign<BitwiseXor>;
using LeftShiftAssign = OperateAndAssign<LeftShift>;
using RightShiftAssign = OperateAndAssign<RightShift>;
}  // namespace OperatorFunctors

template <typename Functor>
constexpr std::string_view GetOperatorString() {
    using namespace OperatorFunctors;
    if constexpr (std::is_same_v<Functor, void>) {
        return ""sv;
    } else if constexpr (std::is_same_v<Functor, Add>) {
        return "+"sv;
    } else if constexpr (std::is_same_v<Functor, Subtract>) {
        return "-"sv;
    } else if constexpr (std::is_same_v<Functor, Negate>) {
        return "-"sv;
    } else if constexpr (std::is_same_v<Functor, Multiply>) {
        return "*"sv;
    } else if constexpr (std::is_same_v<Functor, Divide>) {
        return "/"sv;
    } else if constexpr (std::is_same_v<Functor, Remainder>) {
        return "%"sv;
    } else if constexpr (std::is_same_v<Functor, Increment>) {
        return "++"sv;
    } else if constexpr (std::is_same_v<Functor, Decrement>) {
        return "--"sv;
    } else if constexpr (std::is_same_v<Functor, Equal>) {
        return "=="sv;
    } else if constexpr (std::is_same_v<Functor, NotEqual>) {
        return "!="sv;
    } else if constexpr (std::is_same_v<Functor, LessThan>) {
        return "<"sv;
    } else if constexpr (std::is_same_v<Functor, LessEqual>) {
        return "<="sv;
    } else if constexpr (std::is_same_v<Functor, GreaterThan>) {
        return ">"sv;
    } else if constexpr (std::is_same_v<Functor, GreaterEqual>) {
        return ">="sv;
    } else if constexpr (std::is_same_v<Functor, LogicalAnd>) {
        return "and"sv;
    } else if constexpr (std::is_same_v<Functor, LogicalOr>) {
        return "or"sv;
    } else if constexpr (std::is_same_v<Functor, LogicalNot>) {
        return "not"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseAnd>) {
        return "&"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseOr>) {
        return "|"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseXor>) {
        return "^"sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseNot>) {
        return "~"sv;
    } else if constexpr (std::is_same_v<Functor, LeftShift>) {
        return "<<"sv;
    } else if constexpr (std::is_same_v<Functor, RightShift>) {
        return ">>"sv;
    } else if constexpr (std::is_same_v<Functor, Assign>) {
        return "="sv;
    } else if constexpr (std::is_same_v<Functor, AddAssign>) {
        return "+="sv;
    } else if constexpr (std::is_same_v<Functor, SubtractAssign>) {
        return "-="sv;
    } else if constexpr (std::is_same_v<Functor, MultiplyAssign>) {
        return "*="sv;
    } else if constexpr (std::is_same_v<Functor, DivideAssign>) {
        return "/="sv;
    } else if constexpr (std::is_same_v<Functor, RemainderAssign>) {
        return "%="sv;
    } else if constexpr (std::is_same_v<Functor, LogicalAndAssign>) {
        return "&&="sv;
    } else if constexpr (std::is_same_v<Functor, LogicalOrAssign>) {
        return "||="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseAndAssign>) {
        return "&="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseOrAssign>) {
        return "|="sv;
    } else if constexpr (std::is_same_v<Functor, BitwiseXorAssign>) {
        return "^="sv;
    } else if constexpr (std::is_same_v<Functor, LeftShiftAssign>) {
        return "<<="sv;
    } else if constexpr (std::is_same_v<Functor, RightShiftAssign>) {
        return ">>="sv;
    } else {
        static_assert(false, "Unsupported operator");
    }
}
