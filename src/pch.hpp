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

#include "StainlessParser.h"
#include "antlr4-runtime.h"
// IWYU pragma: end_exports

using namespace std::literals::string_literals;
using namespace std::literals::string_view_literals;

template <typename T, typename Tuple>
struct TypeInTuple;

template <typename T, typename... Ts>
struct TypeInTuple<T, std::tuple<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

template <typename T, typename... Ts>
inline constexpr bool TypeInTupleV = TypeInTuple<T, std::tuple<Ts...>>::value;

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
