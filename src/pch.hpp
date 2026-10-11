#pragma once
// IWYU pragma: begin_exports
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <compare>
#include <concepts>
#include <expected>
#include <filesystem>
#include <flat_map>
#include <flat_set>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <generator>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <ranges>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "antlr4-runtime.h"
#include "mimalloc-new-delete.h"
#include "mimalloc.h"

#ifdef NDEBUG
#define UNREACHABLE() std::unreachable()
#else
#define UNREACHABLE()     \
    {                     \
        assert(false);    \
        std::terminate(); \
    }
#endif

#pragma clang diagnostic ignored "-Wmissing-braces"

// IWYU pragma: end_exports

using namespace std::literals::string_view_literals;

using strview = std::string_view;

template <typename T, typename Tuple>
struct TypeInTuple;

template <typename T, typename... Ts>
struct TypeInTuple<T, std::tuple<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

template <typename T, typename Tuple>
inline constexpr bool TypeInTupleV = TypeInTuple<T, Tuple>::value;

template <typename Target, typename... Ts>
    requires(std::disjunction_v<std::is_same<Target, Ts>...>)
struct IndexOfTypeInTuple;

template <typename Target, typename... Tail>
struct IndexOfTypeInTuple<Target, Target, Tail...> {
    static constexpr std::size_t value = 0;
};

template <typename Target, typename Head, typename... Tail>
struct IndexOfTypeInTuple<Target, Head, Tail...> {
    static constexpr std::size_t value = 1 + IndexOfTypeInTuple<Target, Tail...>::value;
};

template <typename Target, typename... Ts>
constexpr std::size_t IndexOfTypeInTupleV = IndexOfTypeInTuple<Target, Ts...>::value;

template <typename T>
constexpr auto holds_monostate(T&& variant) -> bool {
    return std::holds_alternative<std::monostate>(std::forward<T>(variant));
}

template <std::same_as<std::size_t>... Ts>
auto hash_combine(std::size_t first, const Ts&... rest) noexcept -> std::size_t {
    ((first ^= rest + 0x9e3779b9 + (first << 6) + (first >> 2)), ...);
    return first;
}

template <std::integral T>
constexpr auto float_in_range(auto value) -> bool {
    using Float = decltype(value);
    if (!std::isfinite(value)) {
        return false;
    }
    Float t = std::trunc(value);
    constexpr auto low = static_cast<Float>(std::numeric_limits<T>::lowest());
    constexpr auto high = static_cast<Float>(std::numeric_limits<T>::max());
    return t >= low && t <= high;
}

// Owns a fixed-length contiguous sequence. Assignment and swap replace the
// complete array. const Array provides const elements and views; constructing
// from a span is explicit and copies its elements. Views borrow the owner's
// storage. Element requirements are checked when used, allowing forward
// declarations in recursive ASTs, as with vector.
template <typename T>
class Array {
    static_assert(std::is_object_v<T> && !std::is_const_v<T> && !std::is_volatile_v<T>);

public:
    using element_type = T;
    using value_type = T;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using iterator = T*;
    using const_iterator = const T*;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    static constexpr size_type extent = std::dynamic_extent;

    Array() noexcept = default;

    explicit Array(size_type count) {
        allocate_and_construct(count, [](auto output) {
            std::uninitialized_value_construct_n(output.data(), output.size());
        });
    }

    Array(size_type count, const T& value) {
        allocate_and_construct(count, [&](auto output) {
            std::uninitialized_fill_n(output.data(), output.size(), value);
        });
    }

    Array(std::initializer_list<T> values)
        : Array(std::span<const T>(values.begin(), values.size())) {}

    template <typename U, size_type Extent>
        requires std::constructible_from<T, U&>
    explicit Array(std::span<U, Extent> values) {
        initialize_range(values.begin(), values.end(), values.size());
    }

    template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
        requires std::constructible_from<T, std::iter_reference_t<Iterator>> &&
                 (std::forward_iterator<Iterator> || std::sized_sentinel_for<Sentinel, Iterator> ||
                  std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
    Array(Iterator first, Sentinel last) {
        initialize_iterators(std::move(first), std::move(last));
    }

    template <std::ranges::input_range Range>
        requires std::constructible_from<T, std::ranges::range_reference_t<Range>> &&
                 (std::ranges::sized_range<Range> || std::ranges::forward_range<Range> ||
                  std::is_move_constructible_v<T> || std::is_copy_constructible_v<T>)
    Array(std::from_range_t, Range&& values) {
        if constexpr (std::ranges::sized_range<Range>) {
            auto count = checked_size(std::ranges::size(values));
            initialize_range(std::ranges::begin(values), std::ranges::end(values), count);
        } else {
            initialize_iterators(std::ranges::begin(values), std::ranges::end(values));
        }
    }

    Array(const Array& other) {
        static_assert(
            std::is_copy_constructible_v<T>, "Copying Array requires copy-constructible elements"
        );
        initialize_range(other.begin(), other.end(), other.size());
    }

    Array(Array&& other) noexcept
        : data_(std::exchange(other.data_, nullptr)), size_(std::exchange(other.size_, 0)) {}

    ~Array() {
        if (!empty()) {
            std::destroy_n(data(), size());
            std::allocator<T>{}.deallocate(data(), size());
        }
    }

    Array& operator=(const Array& other) {
        if (this != &other) {
            Array copy(other);
            swap(copy);
        }
        return *this;
    }

    Array& operator=(Array&& other) noexcept {
        if (this != &other) {
            Array moved(std::move(other));
            swap(moved);
        }
        return *this;
    }

    void swap(Array& other) noexcept {
        std::swap(data_, other.data_);
        std::swap(size_, other.size_);
    }
    friend void swap(Array& left, Array& right) noexcept { left.swap(right); }

    [[nodiscard]] size_type size() const noexcept { return size_; }
    [[nodiscard]] size_type size_bytes() const noexcept { return size_ * sizeof(T); }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] pointer data() noexcept { return data_; }
    [[nodiscard]] const_pointer data() const noexcept { return data_; }

    reference operator[](size_type index) noexcept {
        assert(index < size());
        return data()[index];
    }
    const_reference operator[](size_type index) const noexcept {
        assert(index < size());
        return data()[index];
    }
    reference at(size_type index) {
        if (index >= size()) throw std::out_of_range("Array::at");
        return data()[index];
    }
    const_reference at(size_type index) const {
        if (index >= size()) throw std::out_of_range("Array::at");
        return data()[index];
    }
    reference front() noexcept { return (*this)[0]; }
    const_reference front() const noexcept { return (*this)[0]; }
    reference back() noexcept { return (*this)[size() - 1]; }
    const_reference back() const noexcept { return (*this)[size() - 1]; }

    iterator begin() noexcept { return data(); }
    const_iterator begin() const noexcept { return data(); }
    const_iterator cbegin() const noexcept { return data(); }
    iterator end() noexcept { return empty() ? data() : data() + size(); }
    const_iterator end() const noexcept { return empty() ? data() : data() + size(); }
    const_iterator cend() const noexcept { return end(); }
    reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
    const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
    const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }
    reverse_iterator rend() noexcept { return reverse_iterator(begin()); }
    const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
    const_reverse_iterator crend() const noexcept { return const_reverse_iterator(begin()); }

    // The contiguous range interface also supplies implicit Array -> span
    // conversion and std::span(array) deduction without exposing span assignment.
    [[nodiscard]] std::span<T> as_span() noexcept { return {data(), size()}; }
    [[nodiscard]] std::span<const T> as_span() const noexcept { return {data(), size()}; }

    template <size_type Count>
    [[nodiscard]] auto first() noexcept {
        return as_span().template first<Count>();
    }
    template <size_type Count>
    [[nodiscard]] auto first() const noexcept {
        return as_span().template first<Count>();
    }
    [[nodiscard]] auto first(size_type count) noexcept { return as_span().first(count); }
    [[nodiscard]] auto first(size_type count) const noexcept { return as_span().first(count); }

    template <size_type Count>
    [[nodiscard]] auto last() noexcept {
        return as_span().template last<Count>();
    }
    template <size_type Count>
    [[nodiscard]] auto last() const noexcept {
        return as_span().template last<Count>();
    }
    [[nodiscard]] auto last(size_type count) noexcept { return as_span().last(count); }
    [[nodiscard]] auto last(size_type count) const noexcept { return as_span().last(count); }

    template <size_type Offset, size_type Count = std::dynamic_extent>
    [[nodiscard]] auto subspan() noexcept {
        return as_span().template subspan<Offset, Count>();
    }
    template <size_type Offset, size_type Count = std::dynamic_extent>
    [[nodiscard]] auto subspan() const noexcept {
        return as_span().template subspan<Offset, Count>();
    }
    [[nodiscard]] auto subspan(size_type offset, size_type count = std::dynamic_extent) noexcept {
        return as_span().subspan(offset, count);
    }
    [[nodiscard]] auto subspan(
        size_type offset, size_type count = std::dynamic_extent
    ) const noexcept {
        return as_span().subspan(offset, count);
    }

private:
    T* data_ = nullptr;
    size_type size_ = 0;

    template <std::integral Integer>
    static size_type checked_size(Integer count) {
        constexpr auto limit = std::numeric_limits<difference_type>::max() / sizeof(T);
        if (std::cmp_less(count, 0) || std::cmp_greater(count, limit)) {
            throw std::length_error("Array: size is too large");
        }
        return static_cast<size_type>(count);
    }

    // The uninitialized algorithms destroy any constructed prefix on failure;
    // this helper releases the allocation and commits ownership only on success.
    template <typename Initialize>
    void allocate_and_construct(size_type count, Initialize initialize) {
        checked_size(count);
        if (count == 0) return;
        auto* storage = std::allocator<T>{}.allocate(count);
        try {
            initialize(std::span<T>(storage, count));
        } catch (...) {
            std::allocator<T>{}.deallocate(storage, count);
            throw;
        }
        data_ = storage;
        size_ = count;
    }

    template <typename Iterator, typename Sentinel>
    void initialize_range(Iterator first, Sentinel last, size_type count) {
        allocate_and_construct(count, [&](auto output) {
            auto result =
                std::ranges::uninitialized_copy(first, last, output.begin(), output.end());
            if (result.out != output.end()) {
                std::destroy(output.begin(), result.out);
                throw std::length_error("Array: range is shorter than its reported size");
            }
        });
    }

    template <typename Iterator, typename Sentinel>
    void initialize_iterators(Iterator first, Sentinel last) {
        if (first == last) return;
        if constexpr (
            std::forward_iterator<Iterator> || std::sized_sentinel_for<Sentinel, Iterator>
        ) {
            auto count = checked_size(std::ranges::distance(first, last));
            initialize_range(std::move(first), std::move(last), count);
        } else {
            // Single-pass inputs cannot be traversed once to measure their size.
            std::vector<T> buffered;
            for (; first != last; ++first) buffered.emplace_back(*first);
            if constexpr (
                std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>
            ) {
                initialize_range(
                    std::make_move_iterator(buffered.begin()),
                    std::make_move_iterator(buffered.end()),
                    buffered.size()
                );
            } else {
                initialize_range(buffered.begin(), buffered.end(), buffered.size());
            }
        }
    }
};

template <typename U, std::size_t Extent>
Array(std::span<U, Extent>) -> Array<std::remove_cv_t<U>>;
template <std::input_iterator Iterator, std::sentinel_for<Iterator> Sentinel>
Array(Iterator, Sentinel) -> Array<std::iter_value_t<Iterator>>;
template <std::ranges::input_range Range>
Array(std::from_range_t, Range&&) -> Array<std::ranges::range_value_t<Range>>;

template <typename... Ts>
    requires((std::is_pointer_v<Ts>) && ...)
class PointerVariant {
private:
    static constexpr std::size_t mask = std::bit_ceil(sizeof...(Ts)) - 1;

    template <typename U>
    static constexpr bool IsCandidate = std::disjunction_v<std::is_same<U, Ts>...>;

    template <typename U>
    static constexpr bool IsConvertibleToCandidate =
        std::disjunction_v<std::is_convertible<U, Ts>...>;

    template <typename U, typename... Us>
    struct Index {};

    template <typename U, typename Head>
        requires std::is_convertible_v<U, Head>
    struct Index<U, Head> {
        static constexpr std::size_t value = 0;
    };

    template <typename U, typename Head, typename... Tail>
        requires std::is_convertible_v<U, Head>
    struct Index<U, Head, Tail...> {
        static constexpr std::size_t value = 0;
    };

    template <typename U, typename Head, typename... Tail>
    struct Index<U, Head, Tail...> {
        static constexpr std::size_t value = 1 + Index<U, Tail...>::value;
    };

    template <typename U>
    static constexpr std::size_t IndexV = Index<U, Ts...>::value;

private:
    std::uintptr_t ptr_;

public:
    PointerVariant() noexcept = default;

    template <typename T>
        requires(IsConvertibleToCandidate<T>)
    PointerVariant(T ptr) noexcept : ptr_(std::bit_cast<std::uintptr_t>(ptr) | IndexV<T>) {
        static_assert(
            alignof(T) >= sizeof...(Ts),
            "PointerVariant targets must have alignment >= sizeof...(Ts) to store the type tag."
        );
        assert(ptr);
    }

    template <typename T>
        requires(IsCandidate<T>)
    auto get() const noexcept -> T {
        constexpr std::size_t index = IndexV<T>;
        return ((ptr_ & mask) == index) ? std::bit_cast<T>(ptr_ & ~mask) : nullptr;
    }

    auto get() const noexcept -> const void* { return std::bit_cast<const void*>(ptr_ & ~mask); }
};

auto unescape_string(auto& input) -> void {
    std::size_t write_index = 0;
    for (std::size_t read_index = 0; read_index < input.size(); ++read_index) {
        if (input[read_index] == '\\' && read_index + 1 < input.size()) {
            char next_char = input[read_index + 1];
            switch (next_char) {
            case 'n':
                input[write_index++] = '\n';
                break;
            case 't':
                input[write_index++] = '\t';
                break;
            case 'r':
                input[write_index++] = '\r';
                break;
            case '\\':
                input[write_index++] = '\\';
                break;
            case '\'':
                input[write_index++] = '\'';
                break;
            case '\"':
                input[write_index++] = '\"';
                break;
            default:
                // If it's an unrecognized escape sequence, keep the backslash and the character
                input[write_index++] = '\\';
                input[write_index++] = next_char;
                break;
            }
            read_index++;  // Skip the next character since it's part of the escape sequence
        } else {
            input[write_index++] = input[read_index];
        }
    }
    input.resize(write_index);
}

inline auto escape_string(strview input) -> std::string {
    std::string result;
    result.reserve(input.size() + 24);  // Reserve extra space for escape sequences
    for (std::size_t read_index = 0; read_index < input.size(); ++read_index) {
        char c = input[read_index];
        switch (c) {
        case '\n':
            result.push_back('\\');
            result.push_back('n');
            break;
        case '\t':
            result.push_back('\\');
            result.push_back('t');
            break;
        case '\r':
            result.push_back('\\');
            result.push_back('r');
            break;
        case '\\':
            result.push_back('\\');
            result.push_back('\\');
            break;
        case '\'':
            result.push_back('\\');
            result.push_back('\'');
            break;
        case '\"':
            result.push_back('\\');
            result.push_back('\"');
            break;
        default:
            result.push_back(c);
            break;
        }
    }
    return result;
}

namespace ColourEscape {

inline constexpr const char* RESET = "\033[0m";

// Styles
inline constexpr const char* BOLD = "\033[1m";
inline constexpr const char* DIM = "\033[2m";
inline constexpr const char* ITALIC = "\033[3m";
inline constexpr const char* UNDERLINE = "\033[4m";
inline constexpr const char* BLINK = "\033[5m";
inline constexpr const char* REVERSE = "\033[7m";
inline constexpr const char* HIDDEN = "\033[8m";
inline constexpr const char* STRIKE = "\033[9m";

// Standard Foreground
inline constexpr const char* BLACK = "\033[30m";
inline constexpr const char* RED = "\033[31m";
inline constexpr const char* GREEN = "\033[32m";
inline constexpr const char* YELLOW = "\033[33m";
inline constexpr const char* BLUE = "\033[34m";
inline constexpr const char* MAGENTA = "\033[35m";
inline constexpr const char* CYAN = "\033[36m";
inline constexpr const char* WHITE = "\033[37m";

// Standard Background
inline constexpr const char* BG_BLACK = "\033[40m";
inline constexpr const char* BG_RED = "\033[41m";
inline constexpr const char* BG_GREEN = "\033[42m";
inline constexpr const char* BG_YELLOW = "\033[43m";
inline constexpr const char* BG_BLUE = "\033[44m";
inline constexpr const char* BG_MAGENTA = "\033[45m";
inline constexpr const char* BG_CYAN = "\033[46m";
inline constexpr const char* BG_WHITE = "\033[47m";

// High Intensity Foreground
inline constexpr const char* HI_BLACK = "\033[90m";
inline constexpr const char* HI_RED = "\033[91m";
inline constexpr const char* HI_GREEN = "\033[92m";
inline constexpr const char* HI_YELLOW = "\033[93m";
inline constexpr const char* HI_BLUE = "\033[94m";
inline constexpr const char* HI_MAGENTA = "\033[95m";
inline constexpr const char* HI_CYAN = "\033[96m";
inline constexpr const char* HI_WHITE = "\033[97m";

// High Intensity Background
inline constexpr const char* BG_HI_BLACK = "\033[100m";
inline constexpr const char* BG_HI_RED = "\033[101m";
inline constexpr const char* BG_HI_GREEN = "\033[102m";
inline constexpr const char* BG_HI_YELLOW = "\033[103m";
inline constexpr const char* BG_HI_BLUE = "\033[104m";
inline constexpr const char* BG_HI_MAGENTA = "\033[105m";
inline constexpr const char* BG_HI_CYAN = "\033[106m";
inline constexpr const char* BG_HI_WHITE = "\033[107m";

}  // namespace ColourEscape
