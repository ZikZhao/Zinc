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
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <generator>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <memory_resource>
#include <numeric>
#include <ranges>
#include <set>
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

// IWYU pragma: end_exports

using namespace std::literals::string_view_literals;

template <typename T, typename Tuple>
struct TypeInTuple;

template <typename T, typename... Ts>
struct TypeInTuple<T, std::tuple<Ts...>> : std::disjunction<std::is_same<T, Ts>...> {};

template <typename T, typename Tuple>
inline constexpr bool TypeInTupleV = TypeInTuple<T, Tuple>::value;

template <typename Derived, typename Base>
    requires(std::has_virtual_destructor_v<Base>)
constexpr std::unique_ptr<Derived> static_unique_cast(std::unique_ptr<Base> ptr) {
    assert(ptr ? dynamic_cast<Derived*>(ptr.get()) != nullptr : true);
    return std::unique_ptr<Derived>(static_cast<Derived*>(ptr.release()));
}

template <typename Derived, typename Base, typename Deleter>
    requires(!std::is_same_v<Deleter, std::default_delete<Base>>)
constexpr std::unique_ptr<Derived, Deleter> static_unique_cast(std::unique_ptr<Base, Deleter> ptr) {
    return std::unique_ptr<Derived, Deleter>(
        static_cast<Derived*>(ptr.release()), std::move(ptr.get_deleter())
    );
}

template <typename T>
class ComparableSpan : public std::span<T> {
public:
    using std::span<T>::span;
    using std::span<T>::begin;
    using std::span<T>::end;

    std::strong_ordering operator<=>(const ComparableSpan<T>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }

    bool operator==(const ComparableSpan<T>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }

    explicit operator std::string_view() const noexcept
        requires std::is_same_v<T, char>
    {
        return std::string_view(this->data(), this->size());
    }
};

class GlobalMemory {
private:
    static std::pmr::memory_resource* global_heap() noexcept {
        // static std::pmr::monotonic_buffer_resource resource(std::pmr::new_delete_resource());
        constexpr std::pmr::pool_options pool_opts{
            .largest_required_pool_block = 4 * 1024 * 1024,  // 4 MB
        };
        static std::pmr::unsynchronized_pool_resource resource(
            pool_opts, std::pmr::new_delete_resource()
        );
        return &resource;
    }

public:
    static std::pmr::monotonic_buffer_resource* monotonic() noexcept {
        static thread_local std::pmr::monotonic_buffer_resource resource(128 * 1024, global_heap());
        return &resource;
    }

    static std::pmr::unsynchronized_pool_resource* pool() noexcept {
        constexpr std::pmr::pool_options pool_opts{
            .largest_required_pool_block = 4 * 1024,  // 4 KB
        };
        static thread_local std::pmr::unsynchronized_pool_resource resource(
            pool_opts, global_heap()
        );
        return &resource;
    }

    template <typename T>
    class Allocator : public std::pmr::polymorphic_allocator<T> {
    public:
        using std::pmr::polymorphic_allocator<T>::polymorphic_allocator;
        Allocator(std::pmr::memory_resource* r = pool()) : std::pmr::polymorphic_allocator<T>(r) {}
        Allocator(const std::pmr::polymorphic_allocator<T>& other) noexcept
            : std::pmr::polymorphic_allocator<T>(other) {}

        template <typename U>
        void destroy(U* p) {
            std::destroy_at(p);
        }
    };

    template <typename T>
    using Vector = std::vector<T, Allocator<T>>;

    using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

    /// Flat map implementation in SoA style
    template <typename K, typename V, typename C = std::less<K>>
    class Map;

    /// Flat set implementation
    template <typename K, typename C = std::less<K>>
    class Set;

    static void* alloc_raw(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
        return monotonic()->allocate(size, align);
    }

    static void dealloc_raw(
        void* ptr, std::size_t size, std::size_t align = alignof(std::max_align_t)
    ) {
        monotonic()->deallocate(ptr, size, align);
    }

    template <typename T, typename... Args>
    static constexpr T* alloc(Args&&... args) {
        void* ptr = monotonic()->allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    static constexpr ComparableSpan<T> alloc_array(std::size_t n) {
        void* ptr = monotonic()->allocate(n * sizeof(T), alignof(T));
        if constexpr (std::is_trivially_default_constructible_v<T>) {
            return ComparableSpan<T>(static_cast<T*>(ptr), n);
        } else {
            T* typed_ptr = static_cast<T*>(ptr);
            std::uninitialized_default_construct(typed_ptr, typed_ptr + n);
            return ComparableSpan<T>(typed_ptr, n);
        }
    }

    template <typename T, typename... Args>
        requires(std::is_same_v<T, Args> && ...)
    static constexpr ComparableSpan<std::decay_t<T>> pack_array(T&& first, Args&&... rest) {
        ComparableSpan span = alloc_array<std::decay_t<T>>(sizeof...(rest) + 1);
        std::size_t index = 0;
        span[0] = std::forward<T>(first);
        ((span[++index] = std::forward<Args>(rest)), ...);
        return span;
    }

private:
    template <typename T>
    class RangeCollector {
        template <std::ranges::input_range R>
        friend T operator|(R&& range, RangeCollector) {
            T result(std::from_range, std::forward<R>(range));
            return result;
        }
    };

    template <typename E>
    class RangeCollector<ComparableSpan<E>> {
        template <std::ranges::input_range R>
        friend ComparableSpan<E> operator|(R&& range, RangeCollector) {
            if constexpr (std::ranges::sized_range<R>) {
                ComparableSpan span = alloc_array<E>(std::ranges::size(range));
                std::uninitialized_copy(
                    std::ranges::begin(range), std::ranges::end(range), span.data()
                );
                return span;
            } else {
                GlobalMemory::Vector<E> temp;
                for (auto&& item : range) {
                    temp.push_back(std::forward<decltype(item)>(item));
                }
                ComparableSpan span = alloc_array<E>(temp.size());
                std::uninitialized_copy(temp.begin(), temp.end(), span.data());
                return span;
            }
        }
    };

public:
    template <typename T>
    static constexpr auto collect() {
        return RangeCollector<T>{};
    }

    template <typename... Args>
    static GlobalMemory::String format(std::format_string<Args...> fmt, Args&&... args) {
        GlobalMemory::String result;
        std::format_to(std::back_inserter(result), fmt, std::forward<Args>(args)...);
        return result;
    }

    template <typename... Args>
    static std::string_view format_view(std::format_string<Args...> fmt, Args&&... args) {
        std::size_t size = std::formatted_size(fmt, std::forward<Args>(args)...);
        ComparableSpan<char> result = alloc_array<char>(size);
        std::format_to(result.begin(), fmt, std::forward<Args>(args)...);
        return static_cast<std::string_view>(result);
    }

    static std::string_view hex_string(std::string_view input) {
        constexpr char hex_chars[] = "0123456789ABCDEF";
        ComparableSpan<char> result = alloc_array<char>(input.size() * 2 + 3);
        for (std::size_t i = 0; i < input.size(); ++i) {
            unsigned char byte = static_cast<unsigned char>(input[i]);
            result[i * 2 + 1] = hex_chars[(byte >> 4) & 0x0F];
            result[i * 2 + 2] = hex_chars[byte & 0x0F];
        }
        result[0] = '\"';
        result[input.size() * 2 + 1] = '\"';
        result[input.size() * 2 + 2] = '\0';
        return static_cast<std::string_view>(result);
    }

public:
    GlobalMemory() = delete;
};

template <typename Key, typename Value, typename Comp>
class GlobalMemory::Map {
private:
    template <bool IsConst>
    class IteratorImpl {
    private:
        using KeyType = const Key;
        using MappedType = std::conditional_t<IsConst, const Value, Value>;

        class Proxy {
        private:
            const std::pair<KeyType&, MappedType&> pair_;

        public:
            Proxy(KeyType& key, MappedType& value) : pair_(key, value) {}
            const std::pair<KeyType&, MappedType&>* operator->() { return &pair_; }
        };

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<KeyType, MappedType>;
        using pointer = value_type*;
        using reference = value_type&;

    private:
        KeyType* key_ptr_;
        MappedType* value_ptr_;

    public:
        IteratorImpl() = default;
        IteratorImpl(KeyType* key_ptr, MappedType* value_ptr)
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
            return std::pair<KeyType, MappedType>{*key_ptr_, *value_ptr_};
        }
        Proxy operator->() { return Proxy(*key_ptr_, *value_ptr_); }
    };

public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<const Key, Value>;
    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

private:
    Vector<Key> keys_;
    Vector<Value> values_;

public:
    Map() noexcept = default;
    Map(Map&& other) noexcept = default;
    Map& operator=(Map&& other) noexcept = default;
    Map(const Map& other) noexcept
        requires std::is_nothrow_copy_constructible_v<Key> &&
                     std::is_nothrow_copy_constructible_v<Value>
    = default;
    Map& operator=(const Map& other) noexcept
        requires std::is_nothrow_copy_constructible_v<Key> &&
                     std::is_nothrow_copy_constructible_v<Value>
    = default;

    Map(std::initializer_list<std::pair<Key, Value>> init) {
        keys_.reserve(init.size());
        values_.reserve(init.size());
        for (const auto& pair : init) {
            this->insert(pair);
        }
    }

    template <std::ranges::input_range R>
    Map(std::from_range_t, R&& range) {
        Vector<Key> unsorted_keys;
        Vector<Value> unsorted_values;
        if constexpr (std::ranges::sized_range<R>) {
            unsorted_keys.reserve(std::ranges::size(range));
            unsorted_values.reserve(std::ranges::size(range));
        }
        for (auto&& pair : range) {
            unsorted_keys.push_back(std::forward<decltype(pair.first)>(pair.first));
            unsorted_values.push_back(std::forward<decltype(pair.second)>(pair.second));
        }
        Vector<std::size_t> indices(unsorted_keys.size());
        std::ranges::iota(indices.begin(), indices.end(), 0);
        std::sort(indices.begin(), indices.end(), [&](std::size_t a, std::size_t b) {
            return Comp{}(unsorted_keys[a], unsorted_keys[b]);
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
    std::pair<iterator, bool> insert(std::pair<Key, Value> pair) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), pair.first, Comp{});
        if (it != keys_.end() && *it == pair.first) {
            return {
                iterator(
                    &keys_[std::distance(keys_.begin(), it)],
                    &values_[std::distance(keys_.begin(), it)]
                ),
                false
            };
        }
        std::size_t index = std::distance(keys_.begin(), it);
        keys_.insert(it, std::move(pair.first));
        values_.insert(values_.begin() + index, std::move(pair.second));
        return {iterator(&keys_[index], &values_[index]), true};
    }
    Value remove(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it != keys_.end() && *it == key) {
            std::size_t index = std::distance(keys_.begin(), it);
            keys_.erase(it);
            Value value = std::move(values_[index]);
            values_.erase(values_.begin() + index);
            return value;
        } else {
            throw std::out_of_range("Key not found in GlobalMemory::Map");
        }
    }
    iterator find(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            return this->end();
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return iterator(&keys_[index], &values_[index]);
    }
    const_iterator find(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            return this->end();
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return const_iterator(&keys_[index], &values_[index]);
    }
    bool contains(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        return it != keys_.end() && *it == key;
    }
    Value& at(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            throw std::out_of_range("Key not found in GlobalMemory::Map");
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return values_[index];
    }
    const Value& at(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            throw std::out_of_range("Key not found in GlobalMemory::Map");
        }
        std::size_t index = std::distance(keys_.begin(), it);
        return values_[index];
    }
    Value& operator[](const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            std::size_t index = std::distance(keys_.begin(), it);
            keys_.insert(it, key);
            values_.insert(values_.begin() + index, Value{});
            return values_[index];
        } else {
            std::size_t index = std::distance(keys_.begin(), it);
            return values_[index];
        }
    }
    iterator begin() noexcept { return iterator(keys_.data(), values_.data()); }
    iterator end() noexcept {
        return iterator(keys_.data() + keys_.size(), values_.data() + values_.size());
    }
    const_iterator begin() const noexcept { return const_iterator(keys_.data(), values_.data()); }
    const_iterator end() const noexcept {
        return const_iterator(keys_.data() + keys_.size(), values_.data() + values_.size());
    }
    std::strong_ordering operator<=>(const Map<Key, Value, Comp>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const Map<Key, Value, Comp>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
};

template <typename Key, typename Comp>
class GlobalMemory::Set {
public:
    using key_type = Key;
    using value_type = Key;
    using iterator = typename Vector<Key>::iterator;
    using const_iterator = typename Vector<Key>::const_iterator;

private:
    Vector<Key> keys_;

public:
    Set() noexcept = default;
    Set(Set&& other) noexcept = default;
    Set& operator=(Set&& other) noexcept = default;
    Set(const Set& other) noexcept
        requires std::is_nothrow_copy_constructible_v<Key>
    = default;
    Set& operator=(const Set& other) noexcept
        requires std::is_nothrow_copy_constructible_v<Key>
    = default;
    Set(std::from_range_t, auto&& range) {
        Vector<Key> unsorted_keys;
        if constexpr (std::ranges::sized_range<decltype(range)>) {
            unsorted_keys.reserve(std::ranges::size(range));
        }
        for (auto&& key : range) {
            unsorted_keys.push_back(std::forward<decltype(key)>(key));
        }
        std::sort(unsorted_keys.begin(), unsorted_keys.end(), Comp{});
        auto last = std::unique(unsorted_keys.begin(), unsorted_keys.end());
        keys_.assign(unsorted_keys.begin(), last);
    }
    Set(std::initializer_list<Key> init) : Set(std::from_range, init) {}

    std::pair<iterator, bool> emplace(Key&& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it != keys_.end() && *it == key) {
            return {it, false};
        }
        return {keys_.emplace(it, std::move(key)), true};
    }
    std::size_t size() const noexcept { return keys_.size(); }
    typename Vector<Key>::iterator begin() noexcept { return keys_.begin(); }
    typename Vector<Key>::const_iterator begin() const noexcept { return keys_.begin(); }
    typename Vector<Key>::iterator end() noexcept { return keys_.end(); }
    typename Vector<Key>::const_iterator end() const noexcept { return keys_.end(); }
    std::strong_ordering operator<=>(const Set<Key, Comp>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const Set<Key, Comp>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
    bool contains(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        return it != keys_.end() && *it == key;
    }
    iterator find(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            return end();
        }
        return it;
    }
    const_iterator find(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || *it != key) {
            return this->end();
        }
        return it;
    }
    bool is_superset_of(const Set<Key, Comp>& other) const noexcept {
        return std::includes(this->begin(), this->end(), other.begin(), other.end(), Comp{});
    }
    bool is_proper_superset_of(const Set<Key, Comp>& other) const noexcept {
        return this->size() > other.size() && is_superset_of(other);
    }
    bool is_subset_of(const Set<Key, Comp>& other) const noexcept {
        return other.is_superset_of(*this);
    }
    bool is_proper_subset_of(const Set<Key, Comp>& other) const noexcept {
        return this->size() < other.size() && is_subset_of(other);
    }
};

template <>
class GlobalMemory::RangeCollector<GlobalMemory::String> {
    template <std::ranges::input_range R>
    friend String operator|(R&& range, RangeCollector) {
        String result;
        std::ranges::copy(std::forward<R>(range), std::back_inserter(result));
        return result;
    }
};

class MemoryManaged {
public:
    static void* operator new(std ::size_t size) { return GlobalMemory::alloc_raw(size); }
    static void operator delete(void* ptr, std ::size_t size) {
        GlobalMemory::dealloc_raw(ptr, size);
    }

protected:
    /// Protected default constructor to prevent direct instantiation
    MemoryManaged() = default;
    /// Protected destructor to prevent deletion through base pointer
    ~MemoryManaged() = default;
};

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
