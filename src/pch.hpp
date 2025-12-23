#pragma once
// IWYU pragma: begin_exports
#include <array>
#include <cassert>
#include <cmath>
#include <compare>
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

template <typename T>
class ComparableSpan : public std::span<T> {
public:
    using std::span<T>::span;
    std::strong_ordering operator<=>(const ComparableSpan<T>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const ComparableSpan<T>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
};

namespace GlobalMemory {
inline std::pmr::memory_resource* memory_resource() {
    constexpr std::size_t buffer_size = 1024 * 1024;  // 1 MB
    alignas(std::max_align_t) static std::array<std::byte, buffer_size> buffer;
    static std::pmr::monotonic_buffer_resource resource(
        buffer.data(), buffer.size(), std::pmr::new_delete_resource()
    );
    return &resource;
};

template <typename T, typename... Args>
constexpr T* allocate(Args&&... args) {
    void* ptr = memory_resource()->allocate(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
}

template <typename T>
constexpr ComparableSpan<T> allocate_array(std::size_t n) {
    void* ptr = memory_resource()->allocate(n * sizeof(T), alignof(T));
    if constexpr (std::is_trivially_default_constructible_v<T>) {
        return ComparableSpan<T>(static_cast<T*>(ptr), n);
    } else {
        T* typed_ptr = static_cast<T*>(ptr);
        std::uninitialized_default_construct(typed_ptr, typed_ptr + n);
        return ComparableSpan<T>(typed_ptr, n);
    }
}

template <typename T, std::ranges::input_range R>
constexpr ComparableSpan<T> collect_range(R&& range) {
    if constexpr (std::ranges::sized_range<R>) {
        ComparableSpan<T> span = allocate_array<T>(std::ranges::size(range));
        std::uninitialized_copy(std::ranges::begin(range), std::ranges::end(range), span.data());
        return span;
    } else {
        std::vector<T> temp;
        for (auto&& item : range) {
            temp.push_back(std::forward<decltype(item)>(item));
        }
        ComparableSpan<T> span = allocate_array<T>(temp.size());
        std::uninitialized_copy(temp.begin(), temp.end(), span.data());
        return span;
    }
}
}  // namespace GlobalMemory

template <typename T, typename Tuple>
struct TypeInTuple;

template <typename T, typename... Ts>
struct TypeInTuple<T, std::tuple<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

template <typename T, typename Tuple>
inline constexpr bool TypeInTupleV = TypeInTuple<T, Tuple>::value;

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

template <typename Key, typename Value, typename Comp = std::less<Key>>
class FlatMap {
private:
    template <bool IsConst>
    class IteratorImpl {
    private:
        using KeyType = const Key;
        using ValueType = std::conditional_t<IsConst, const Value, Value>;
        KeyType* key_ptr_;
        ValueType* value_ptr_;

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<KeyType, ValueType>;
        using pointer = value_type*;
        using reference = value_type&;
        IteratorImpl(KeyType* key_ptr, ValueType* value_ptr)
            : key_ptr_(key_ptr), value_ptr_(value_ptr) {}
        IteratorImpl& operator++() {
            ++key_ptr_;
            ++value_ptr_;
            return *this;
        }
        IteratorImpl operator++(int) {
            IteratorImpl temp = *this;
            ++(*this);
            return temp;
        }
        bool operator==(const IteratorImpl& other) const noexcept {
            return key_ptr_ == other.key_ptr_;
        }
        bool operator!=(const IteratorImpl& other) const noexcept { return !(*this == other); }
        value_type operator*() const {
            return std::pair<KeyType, ValueType>{*key_ptr_, *value_ptr_};
        }
    };

public:
    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

private:
    std::vector<Key> keys_;
    std::vector<Value> values_;

public:
    template <std::ranges::input_range R>
    FlatMap(std::from_range_t, R&& range) {
        std::vector<Key> unsorted_keys;
        std::vector<Value> unsorted_values;
        if constexpr (std::ranges::sized_range<R>) {
            unsorted_keys.reserve(std::ranges::size(range));
            unsorted_values.reserve(std::ranges::size(range));
        }
        for (auto&& pair : range) {
            unsorted_keys.push_back(std::forward<decltype(pair.first)>(pair.first));
            unsorted_values.push_back(std::forward<decltype(pair.second)>(pair.second));
        }
        std::vector<std::size_t> indices(unsorted_keys.size());
        std::ranges::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
            return Comp()(unsorted_keys[a], unsorted_keys[b]);
        });
        std::unique(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
            return unsorted_keys[a] == unsorted_keys[b];
        });
        keys_.reserve(unsorted_keys.size());
        values_.reserve(unsorted_values.size());
        for (std::size_t index : indices) {
            keys_.push_back(std::move(unsorted_keys[index]));
            values_.push_back(std::move(unsorted_values[index]));
        }
    }
    constexpr std::size_t size() const noexcept { return keys_.size(); }
    void emplace(std::pair<Key, Value> pair) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), pair.first, Comp());
        std::size_t index = std::distance(keys_.begin(), it);
        keys_.insert(it, std::move(pair.first));
        values_.insert(values_.begin() + index, std::move(pair.second));
    }
    Value remove(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp());
        if (it != keys_.end() && *it == key) {
            std::size_t index = std::distance(keys_.begin(), it);
            keys_.erase(it);
            Value value = std::move(values_[index]);
            values_.erase(values_.begin() + index);
            return value;
        } else {
            throw std::out_of_range("Key not found in FlatMap");
        }
    }
    iterator find(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp());
        if (it == keys_.end() || *it != key) {
            return this->end();
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return iterator(&keys_[index], &values_[index]);
    }
    const_iterator find(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp());
        if (it == keys_.end() || *it != key) {
            return this->end();
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return const_iterator(&keys_[index], &values_[index]);
    }
    iterator begin() noexcept { return iterator(keys_.data(), values_.data()); }
    iterator end() noexcept {
        return iterator(keys_.data() + keys_.size(), values_.data() + values_.size());
    }
    const_iterator begin() const noexcept { return const_iterator(keys_.data(), values_.data()); }
    const_iterator end() const noexcept {
        return const_iterator(keys_.data() + keys_.size(), values_.data() + values_.size());
    }
    std::strong_ordering operator<=>(const FlatMap<Key, Value, Comp>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const FlatMap<Key, Value, Comp>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
};

template <typename Key, typename Comp = std::less<Key>>
class FlatSet {
private:
    std::vector<Key> keys_;

public:
    Key& try_emplace(Key key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp());
        if (it == keys_.end() || *it != key) {
            return *keys_.emplace(it, std::move(key));
        }
        return *it;
    }
    std::size_t size() const noexcept { return keys_.size(); }
    typename std::vector<Key>::const_iterator begin() const noexcept { return keys_.begin(); }
    typename std::vector<Key>::const_iterator end() const noexcept { return keys_.end(); }
    std::strong_ordering operator<=>(const FlatSet<Key, Comp>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const FlatSet<Key, Comp>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
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
