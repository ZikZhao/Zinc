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

#ifdef NDEBUG
#define UNREACHABLE() std::unreachable()
#else
#define UNREACHABLE()     \
    do {                  \
        assert(false);    \
        std::terminate(); \
    } while (0)
#endif

// IWYU pragma: end_exports

using namespace std::literals::string_view_literals;

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

template <typename... Ts>
    requires(std::is_pointer_v<Ts> && ...)
class PointerVariant {
private:
    static constexpr std::size_t mask = std::bit_ceil(sizeof...(Ts)) - 1;

    template <typename U>
    static constexpr bool IsCandidate = TypeInTupleV<U, std::tuple<Ts...>>;

private:
    std::uintptr_t ptr_;

public:
    PointerVariant() noexcept = default;

    template <typename T>
        requires(IsCandidate<T>)
    PointerVariant(T ptr) noexcept
        : ptr_(reinterpret_cast<std::uintptr_t>(ptr) | IndexOfTypeInTupleV<T, Ts...>) {
        static_assert(
            alignof(T) >= sizeof...(Ts),
            "PointerVariant targets must have alignment >= sizeof...(Ts) to store the type tag."
        );
        assert(ptr);
    }

    template <typename T>
        requires(IsCandidate<T>)
    T get() noexcept {
        constexpr std::size_t index = IndexOfTypeInTupleV<T, Ts...>;
        return ((ptr_ & mask) == index) ? reinterpret_cast<T>(ptr_ & ~mask) : nullptr;
    }
};

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

    /// Multi-map implementation in SoA style
    template <typename K, typename V, typename C = std::less<K>>
    class MultiMap;

    static void* alloc_raw(std::size_t size, std::size_t align = alignof(std::max_align_t)) {
        return monotonic()->allocate(size, align);
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
            if constexpr (requires { T(std::from_range, std::forward<R>(range)); }) {
                return T(std::from_range, std::forward<R>(range));
            } else {
                auto common = range | std::views::common;
                return T(common.begin(), common.end());
            }
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
    class MemoryManaged {
    public:
        static void* operator new(std ::size_t size) { return GlobalMemory::alloc_raw(size); }
        static void operator delete(void* ptr, std ::size_t size) {}

    protected:
        /// Protected default constructor to prevent direct instantiation
        MemoryManaged() = default;
        /// Protected destructor to prevent deletion through base pointer
        ~MemoryManaged() = default;
    };

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
    Map(const Map& other) noexcept(
        noexcept(std::declval<Vector<Key>&>() = std::declval<const Vector<Key>&>()) &&
        noexcept(std::declval<Vector<Value>&>() = std::declval<const Vector<Value>&>())
    ) = default;
    Map& operator=(const Map& other) noexcept(
        noexcept(std::declval<Vector<Key>&>() = std::declval<const Vector<Key>&>()) &&
        noexcept(std::declval<Vector<Value>&>() = std::declval<const Vector<Value>&>())
    ) = default;
    Map(Map&& other) noexcept = default;
    Map& operator=(Map&& other) noexcept = default;

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

    typename Vector<Key>::iterator begin() noexcept { return keys_.begin(); }
    typename Vector<Key>::const_iterator begin() const noexcept { return keys_.begin(); }
    typename Vector<Key>::iterator end() noexcept { return keys_.end(); }
    typename Vector<Key>::const_iterator end() const noexcept { return keys_.end(); }

    bool empty() const noexcept { return keys_.empty(); }
    std::size_t size() const noexcept { return keys_.size(); }

    std::pair<iterator, bool> insert(const Key& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it != keys_.end() && *it == key) {
            return {it, false};
        }
        return {keys_.emplace(it, key), true};
    }
    std::pair<iterator, bool> emplace(auto&&... args) {
        Key key = Key(std::forward<decltype(args)>(args)...);
        return this->insert(std::move(key));
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
    bool contains(const Key& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        return it != keys_.end() && *it == key;
    }

    std::strong_ordering operator<=>(const Set<Key, Comp>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const Set<Key, Comp>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
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

template <typename K, typename V, typename C>
class GlobalMemory::MultiMap {
private:
    template <bool IsConst>
    class IteratorImpl {
    private:
        using KeyIterator = std::conditional_t<
            IsConst,
            typename Vector<K>::const_iterator,
            typename Vector<K>::iterator>;
        using ValueIterator = std::conditional_t<
            IsConst,
            typename Vector<V>::const_iterator,
            typename Vector<V>::iterator>;
        using KeyRef = const K&;
        using ValueRef = std::conditional_t<IsConst, const V&, V&>;

        class Proxy {
        private:
            const std::pair<KeyRef, ValueRef> pair_;

        public:
            Proxy(KeyRef key, ValueRef value) : pair_(key, value) {}
            const std::pair<KeyRef, ValueRef>* operator->() { return &pair_; }
        };

    public:
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = std::pair<const K&, ValueRef>;
        using pointer = value_type*;
        using reference = value_type&;

    private:
        KeyIterator key_it_;
        ValueIterator value_it_;

    public:
        IteratorImpl() = default;
        IteratorImpl(KeyIterator key_it, ValueIterator value_it)
            : key_it_(key_it), value_it_(value_it) {}
        IteratorImpl& operator++() {
            ++key_it_;
            ++value_it_;
            return *this;
        }
        IteratorImpl operator++(int) {
            IteratorImpl temp = *this;
            ++(*this);
            return temp;
        }
        bool operator==(const IteratorImpl& other) const noexcept {
            return key_it_ == other.key_it_;
        }
        bool operator!=(const IteratorImpl& other) const noexcept { return !(*this == other); }
        value_type operator*() const { return value_type{*key_it_, *value_it_}; }
        Proxy operator->() { return Proxy(*key_it_, *value_it_); }

        KeyIterator key_iter() const noexcept { return key_it_; }
        ValueIterator value_iter() const noexcept { return value_it_; }
    };

public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<const K, V>;
    using iterator = IteratorImpl<false>;
    using const_iterator = IteratorImpl<true>;

private:
    Vector<K> keys_;
    Vector<V> values_;

public:
    MultiMap() noexcept = default;
    MultiMap(MultiMap&& other) noexcept = default;
    MultiMap& operator=(MultiMap&& other) noexcept = default;
    MultiMap(const MultiMap& other) noexcept
        requires std::is_nothrow_copy_constructible_v<K> && std::is_nothrow_copy_constructible_v<V>
    = default;
    MultiMap& operator=(const MultiMap& other) noexcept
        requires std::is_nothrow_copy_constructible_v<K> && std::is_nothrow_copy_constructible_v<V>
    = default;

    MultiMap(std::initializer_list<std::pair<K, V>> init) {
        keys_.reserve(init.size());
        values_.reserve(init.size());
        for (const auto& pair : init) {
            this->insert(pair);
        }
    }

    constexpr std::size_t size() const noexcept { return keys_.size(); }
    constexpr bool empty() const noexcept { return keys_.empty(); }

    iterator insert(std::pair<K, V> pair) {
        auto it = std::upper_bound(keys_.begin(), keys_.end(), pair.first, C{});
        std::size_t index = std::distance(keys_.begin(), it);
        keys_.insert(it, std::move(pair.first));
        values_.insert(values_.begin() + index, std::move(pair.second));
        return iterator(&keys_[index], &values_[index]);
    }

    template <typename... Args>
    iterator emplace(const K& key, Args&&... args) {
        auto it = std::upper_bound(keys_.begin(), keys_.end(), key, C{});
        std::size_t index = std::distance(keys_.begin(), it);
        keys_.insert(it, key);
        values_.emplace(values_.begin() + index, std::forward<Args>(args)...);
        return iterator(&keys_[index], &values_[index]);
    }

    iterator find(const K& key) {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, C{});
        if (it == keys_.end() || C{}(key, *it) || C{}(*it, key)) {
            return end();
        }
        return iterator(it, values_.begin() + std::distance(keys_.begin(), it));
    }

    const_iterator find(const K& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, C{});
        if (it == keys_.end() || C{}(key, *it) || C{}(*it, key)) {
            return end();
        }
        return const_iterator(it, values_.begin() + std::distance(keys_.begin(), it));
    }

    std::pair<iterator, iterator> equal_range(const K& key) {
        auto lower = std::lower_bound(keys_.begin(), keys_.end(), key, C{});
        auto upper = std::upper_bound(keys_.begin(), keys_.end(), key, C{});
        return {
            iterator(lower, values_.begin() + std::distance(keys_.begin(), lower)),
            iterator(upper, values_.begin() + std::distance(keys_.begin(), upper))
        };
    }

    std::pair<const_iterator, const_iterator> equal_range(const K& key) const {
        auto lower = std::lower_bound(keys_.begin(), keys_.end(), key, C{});
        auto upper = std::upper_bound(keys_.begin(), keys_.end(), key, C{});
        return {
            const_iterator(lower, values_.begin() + std::distance(keys_.begin(), lower)),
            const_iterator(upper, values_.begin() + std::distance(keys_.begin(), upper))
        };
    }

    std::size_t count(const K& key) const {
        auto [lower, upper] = std::equal_range(keys_.begin(), keys_.end(), key, C{});
        return std::distance(lower, upper);
    }

    bool contains(const K& key) const {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, C{});
        return it != keys_.end() && !C{}(key, *it) && !C{}(*it, key);
    }

    iterator erase(iterator pos) {
        auto key_it = keys_.erase(pos.key_iter());
        auto value_it = values_.erase(pos.value_iter());
        return iterator(key_it, value_it);
    }

    std::size_t erase(const K& key) {
        auto [lower, upper] = std::equal_range(keys_.begin(), keys_.end(), key, C{});
        std::size_t count = std::distance(lower, upper);
        if (count > 0) {
            std::size_t lower_idx = std::distance(keys_.begin(), lower);
            keys_.erase(lower, upper);
            values_.erase(values_.begin() + lower_idx, values_.begin() + lower_idx + count);
        }
        return count;
    }

    void clear() noexcept {
        keys_.clear();
        values_.clear();
    }

    iterator begin() noexcept { return iterator(keys_.begin(), values_.begin()); }
    iterator end() noexcept { return iterator(keys_.end(), values_.end()); }
    const_iterator begin() const noexcept { return const_iterator(keys_.begin(), values_.begin()); }
    const_iterator end() const noexcept { return const_iterator(keys_.end(), values_.end()); }

    std::strong_ordering operator<=>(const MultiMap<K, V, C>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const MultiMap<K, V, C>& other) const noexcept {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
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

class BigInt {
private:
    GlobalMemory::Vector<std::uint32_t>
        digits_;  // Little-endian representation (least significant first)
    bool is_negative_;

    // Internal helpers
    void normalize() noexcept {
        while (digits_.size() > 1 && digits_.back() == 0) {
            digits_.pop_back();
        }
        if (digits_.size() == 1 && digits_[0] == 0) {
            is_negative_ = false;
        }
    }

    bool is_zero() const noexcept { return digits_.size() == 1 && digits_[0] == 0; }

    // Compare absolute values: -1 if |this| < |other|, 0 if equal, 1 if |this| > |other|
    int compare_abs(const BigInt& other) const noexcept {
        if (digits_.size() != other.digits_.size()) {
            return digits_.size() < other.digits_.size() ? -1 : 1;
        }
        for (std::size_t i = digits_.size(); i > 0; --i) {
            if (digits_[i - 1] != other.digits_[i - 1]) {
                return digits_[i - 1] < other.digits_[i - 1] ? -1 : 1;
            }
        }
        return 0;
    }

    // Add absolute values (ignores signs)
    static BigInt add_abs(const BigInt& a, const BigInt& b) {
        BigInt result;
        result.digits_.clear();
        const std::size_t max_size = std::max(a.digits_.size(), b.digits_.size());
        result.digits_.reserve(max_size + 1);

        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < max_size || carry; ++i) {
            std::uint64_t sum = carry;
            if (i < a.digits_.size()) sum += a.digits_[i];
            if (i < b.digits_.size()) sum += b.digits_[i];
            result.digits_.push_back(static_cast<std::uint32_t>(sum));
            carry = sum >> 32;
        }
        result.normalize();
        return result;
    }

    // Subtract absolute values: |a| - |b|, assumes |a| >= |b|
    static BigInt sub_abs(const BigInt& a, const BigInt& b) {
        BigInt result;
        result.digits_.clear();
        result.digits_.reserve(a.digits_.size());

        std::int64_t borrow = 0;
        for (std::size_t i = 0; i < a.digits_.size(); ++i) {
            std::int64_t diff = static_cast<std::int64_t>(a.digits_[i]) - borrow;
            if (i < b.digits_.size()) diff -= b.digits_[i];
            if (diff < 0) {
                diff += (1LL << 32);
                borrow = 1;
            } else {
                borrow = 0;
            }
            result.digits_.push_back(static_cast<std::uint32_t>(diff));
        }
        result.normalize();
        return result;
    }

    // Multiply by a single digit
    BigInt mul_digit(std::uint32_t d) const {
        if (d == 0 || is_zero()) return BigInt(0ul);
        BigInt result;
        result.digits_.clear();
        result.digits_.reserve(digits_.size() + 1);
        result.is_negative_ = is_negative_;

        std::uint64_t carry = 0;
        for (std::size_t i = 0; i < digits_.size() || carry; ++i) {
            std::uint64_t prod = carry;
            if (i < digits_.size()) prod += static_cast<std::uint64_t>(digits_[i]) * d;
            result.digits_.push_back(static_cast<std::uint32_t>(prod));
            carry = prod >> 32;
        }
        result.normalize();
        return result;
    }

    // Shift left by n digits (multiply by 2^(32*n))
    BigInt shift_digits_left(std::size_t n) const {
        if (is_zero()) return *this;
        BigInt result;
        result.digits_.clear();
        result.digits_.reserve(digits_.size() + n);
        result.is_negative_ = is_negative_;
        for (std::size_t i = 0; i < n; ++i) {
            result.digits_.push_back(0);
        }
        for (std::uint32_t d : digits_) {
            result.digits_.push_back(d);
        }
        return result;
    }

    // Division helper: divides |this| by |divisor|, returns {quotient, remainder}
    std::pair<BigInt, BigInt> div_mod_abs(const BigInt& divisor) const {
        if (divisor.is_zero()) {
            throw std::domain_error("Division by zero");
        }

        int cmp = compare_abs(divisor);
        if (cmp < 0) {
            return {BigInt(0ul), *this};
        }
        if (cmp == 0) {
            return {BigInt(1ul), BigInt(0ul)};
        }

        // Binary long division
        BigInt quotient(0ul);
        BigInt remainder(0ul);
        remainder.is_negative_ = false;

        // Process bits from most significant to least significant
        for (std::size_t i = digits_.size(); i > 0; --i) {
            for (int bit = 31; bit >= 0; --bit) {
                remainder = remainder << 1;
                if ((digits_[i - 1] >> bit) & 1) {
                    remainder.digits_[0] |= 1;
                }

                quotient = quotient << 1;
                if (remainder.compare_abs(divisor) >= 0) {
                    remainder = sub_abs(remainder, divisor);
                    quotient.digits_[0] |= 1;
                }
            }
        }

        quotient.normalize();
        remainder.normalize();
        return {quotient, remainder};
    }

public:
    // Constructors
    BigInt() : is_negative_(false) { digits_.push_back(0); }

    BigInt(std::int64_t value) : is_negative_(value < 0) {
        std::uint64_t abs_val =
            value < 0 ? static_cast<std::uint64_t>(-value) : static_cast<std::uint64_t>(value);
        if (abs_val == 0) {
            digits_.push_back(0);
        } else {
            while (abs_val > 0) {
                digits_.push_back(static_cast<std::uint32_t>(abs_val));
                abs_val >>= 32;
            }
        }
    }

    BigInt(std::uint64_t value) : is_negative_(false) {
        if (value == 0) {
            digits_.push_back(0);
        } else {
            while (value > 0) {
                digits_.push_back(static_cast<std::uint32_t>(value));
                value >>= 32;
            }
        }
    }

    BigInt(const BigInt& other) = default;
    BigInt& operator=(const BigInt& other) = default;
    BigInt(BigInt&& other) noexcept = default;
    BigInt& operator=(BigInt&& other) noexcept = default;

    explicit BigInt(std::string_view str) : is_negative_(false) {
        if (str.empty()) {
            digits_.push_back(0);
            return;
        }

        std::size_t start = 0;
        if (str[0] == '-') {
            is_negative_ = true;
            start = 1;
        } else if (str[0] == '+') {
            start = 1;
        }

        digits_.push_back(0);
        for (std::size_t i = start; i < str.size(); ++i) {
            char ch = str[i];
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                throw std::invalid_argument("Invalid character in BigInt string");
            }
            *this = mul_digit(10) + BigInt(static_cast<std::uint64_t>(ch - '0'));
        }
        normalize();
    }

    // Convert to string representation
    GlobalMemory::String to_string() const {
        if (is_zero()) return GlobalMemory::String("0");

        GlobalMemory::String result;
        BigInt temp = *this;
        temp.is_negative_ = false;

        while (!temp.is_zero()) {
            auto [q, r] = temp.div_mod_abs(BigInt(10ul));
            result.push_back(static_cast<char>('0' + r.digits_[0]));
            temp = std::move(q);
        }

        if (is_negative_) result.push_back('-');
        std::ranges::reverse(result);
        return result;
    }

    // Comparison operators
    std::strong_ordering operator<=>(const BigInt& other) const noexcept {
        if (is_negative_ != other.is_negative_) {
            return is_negative_ ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        int cmp = compare_abs(other);
        if (is_negative_) cmp = -cmp;
        if (cmp < 0) return std::strong_ordering::less;
        if (cmp > 0) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    bool operator==(const BigInt& other) const noexcept {
        return is_negative_ == other.is_negative_ && digits_ == other.digits_;
    }

    // Unary operators
    BigInt operator-() const {
        BigInt result = *this;
        if (!result.is_zero()) {
            result.is_negative_ = !result.is_negative_;
        }
        return result;
    }

    BigInt operator+() const { return *this; }

    BigInt abs() const {
        BigInt result = *this;
        result.is_negative_ = false;
        return result;
    }

    // Arithmetic operators
    BigInt operator+(const BigInt& other) const {
        if (is_negative_ == other.is_negative_) {
            BigInt result = add_abs(*this, other);
            result.is_negative_ = is_negative_;
            return result;
        }
        // Different signs: subtract smaller from larger
        int cmp = compare_abs(other);
        if (cmp == 0) return BigInt(0ul);
        if (cmp > 0) {
            BigInt result = sub_abs(*this, other);
            result.is_negative_ = is_negative_;
            return result;
        } else {
            BigInt result = sub_abs(other, *this);
            result.is_negative_ = other.is_negative_;
            return result;
        }
    }

    BigInt operator-(const BigInt& other) const { return *this + (-other); }

    BigInt operator*(const BigInt& other) const {
        if (is_zero() || other.is_zero()) return BigInt(0ul);

        BigInt result(0ul);
        for (std::size_t i = 0; i < other.digits_.size(); ++i) {
            BigInt partial = mul_digit(other.digits_[i]).shift_digits_left(i);
            result = add_abs(result, partial);
        }
        result.is_negative_ = is_negative_ != other.is_negative_;
        result.normalize();
        return result;
    }

    BigInt operator/(const BigInt& other) const {
        auto [q, r] = div_mod_abs(other);
        q.is_negative_ = is_negative_ != other.is_negative_;
        q.normalize();
        return q;
    }

    BigInt operator%(const BigInt& other) const {
        auto [q, r] = div_mod_abs(other);
        r.is_negative_ = is_negative_;
        r.normalize();
        return r;
    }

    // Compound assignment operators
    BigInt& operator+=(const BigInt& other) { return *this = *this + other; }
    BigInt& operator-=(const BigInt& other) { return *this = *this - other; }
    BigInt& operator*=(const BigInt& other) { return *this = *this * other; }
    BigInt& operator/=(const BigInt& other) { return *this = *this / other; }
    BigInt& operator%=(const BigInt& other) { return *this = *this % other; }

    // Increment/Decrement
    BigInt& operator++() { return *this += BigInt(1ul); }
    BigInt operator++(int) {
        BigInt tmp = *this;
        ++*this;
        return tmp;
    }
    BigInt& operator--() { return *this -= BigInt(1ul); }
    BigInt operator--(int) {
        BigInt tmp = *this;
        --*this;
        return tmp;
    }

    // Bitwise operators (work on two's complement representation conceptually)
    BigInt operator<<(std::size_t shift) const {
        if (is_zero() || shift == 0) return *this;

        std::size_t digit_shift = shift / 32;
        std::size_t bit_shift = shift % 32;

        BigInt result;
        result.digits_.clear();
        result.digits_.reserve(digits_.size() + digit_shift + 1);
        result.is_negative_ = is_negative_;

        for (std::size_t i = 0; i < digit_shift; ++i) {
            result.digits_.push_back(0);
        }

        std::uint32_t carry = 0;
        for (std::size_t i = 0; i < digits_.size(); ++i) {
            std::uint64_t val = (static_cast<std::uint64_t>(digits_[i]) << bit_shift) | carry;
            result.digits_.push_back(static_cast<std::uint32_t>(val));
            carry = static_cast<std::uint32_t>(val >> 32);
        }
        if (carry > 0) {
            result.digits_.push_back(carry);
        }
        result.normalize();
        return result;
    }

    BigInt operator>>(std::size_t shift) const {
        if (is_zero() || shift == 0) return *this;

        std::size_t digit_shift = shift / 32;
        std::size_t bit_shift = shift % 32;

        if (digit_shift >= digits_.size()) {
            return is_negative_ ? BigInt(-1ul) : BigInt(0ul);
        }

        BigInt result;
        result.digits_.clear();
        result.digits_.reserve(digits_.size() - digit_shift);
        result.is_negative_ = is_negative_;

        for (std::size_t i = digit_shift; i < digits_.size(); ++i) {
            std::uint32_t val = digits_[i] >> bit_shift;
            if (bit_shift > 0 && i + 1 < digits_.size()) {
                val |= digits_[i + 1] << (32 - bit_shift);
            }
            result.digits_.push_back(val);
        }
        result.normalize();

        // For negative numbers, floor division semantics
        if (is_negative_ && !result.is_zero()) {
            // Check if any bits were shifted out
            bool has_remainder = false;
            for (std::size_t i = 0; i < digit_shift && !has_remainder; ++i) {
                if (digits_[i] != 0) has_remainder = true;
            }
            if (!has_remainder && bit_shift > 0 && digit_shift < digits_.size()) {
                if ((digits_[digit_shift] & ((1 << bit_shift) - 1)) != 0) {
                    has_remainder = true;
                }
            }
            if (has_remainder) {
                result -= BigInt(1ul);
            }
        }
        return result;
    }

    BigInt& operator<<=(std::size_t shift) { return *this = *this << shift; }
    BigInt& operator>>=(std::size_t shift) { return *this = *this >> shift; }

    // Bitwise AND (for non-negative numbers)
    BigInt operator&(const BigInt& other) const {
        // For simplicity, bitwise ops work on magnitude only for non-negative
        if (is_negative_ || other.is_negative_) {
            throw std::domain_error("Bitwise AND requires non-negative operands");
        }

        BigInt result;
        result.digits_.clear();
        std::size_t min_size = std::min(digits_.size(), other.digits_.size());
        result.digits_.reserve(min_size);

        for (std::size_t i = 0; i < min_size; ++i) {
            result.digits_.push_back(digits_[i] & other.digits_[i]);
        }
        if (result.digits_.empty()) result.digits_.push_back(0);
        result.normalize();
        return result;
    }

    // Bitwise OR (for non-negative numbers)
    BigInt operator|(const BigInt& other) const {
        if (is_negative_ || other.is_negative_) {
            throw std::domain_error("Bitwise OR requires non-negative operands");
        }

        BigInt result;
        result.digits_.clear();
        std::size_t max_size = std::max(digits_.size(), other.digits_.size());
        result.digits_.reserve(max_size);

        for (std::size_t i = 0; i < max_size; ++i) {
            std::uint32_t a = i < digits_.size() ? digits_[i] : 0;
            std::uint32_t b = i < other.digits_.size() ? other.digits_[i] : 0;
            result.digits_.push_back(a | b);
        }
        result.normalize();
        return result;
    }

    // Bitwise XOR (for non-negative numbers)
    BigInt operator^(const BigInt& other) const {
        if (is_negative_ || other.is_negative_) {
            throw std::domain_error("Bitwise XOR requires non-negative operands");
        }

        BigInt result;
        result.digits_.clear();
        std::size_t max_size = std::max(digits_.size(), other.digits_.size());
        result.digits_.reserve(max_size);

        for (std::size_t i = 0; i < max_size; ++i) {
            std::uint32_t a = i < digits_.size() ? digits_[i] : 0;
            std::uint32_t b = i < other.digits_.size() ? other.digits_[i] : 0;
            result.digits_.push_back(a ^ b);
        }
        if (result.digits_.empty()) result.digits_.push_back(0);
        result.normalize();
        return result;
    }

    // Bitwise NOT (returns -(n+1) for mathematical consistency)
    BigInt operator~() const { return -(*this) - BigInt(1ul); }

    BigInt& operator&=(const BigInt& other) { return *this = *this & other; }
    BigInt& operator|=(const BigInt& other) { return *this = *this | other; }
    BigInt& operator^=(const BigInt& other) { return *this = *this ^ other; }

    // Utility functions
    bool is_negative() const noexcept { return is_negative_; }
    bool is_positive() const noexcept { return !is_negative_ && !is_zero(); }

    // Get the number of bits needed to represent this number
    std::size_t bit_length() const noexcept {
        if (is_zero()) return 0;
        std::size_t bits = (digits_.size() - 1) * 32;
        std::uint32_t top = digits_.back();
        while (top > 0) {
            ++bits;
            top >>= 1;
        }
        return bits;
    }

    // Test if a specific bit is set (0-indexed from LSB)
    bool test_bit(std::size_t pos) const noexcept {
        std::size_t digit_idx = pos / 32;
        std::size_t bit_idx = pos % 32;
        if (digit_idx >= digits_.size()) return false;
        return (digits_[digit_idx] >> bit_idx) & 1;
    }

    // Set a specific bit
    BigInt& set_bit(std::size_t pos) {
        if (is_negative_) {
            throw std::domain_error("set_bit requires non-negative number");
        }
        std::size_t digit_idx = pos / 32;
        std::size_t bit_idx = pos % 32;
        while (digits_.size() <= digit_idx) {
            digits_.push_back(0);
        }
        digits_[digit_idx] |= (1u << bit_idx);
        return *this;
    }

    // Clear a specific bit
    BigInt& clear_bit(std::size_t pos) {
        if (is_negative_) {
            throw std::domain_error("clear_bit requires non-negative number");
        }
        std::size_t digit_idx = pos / 32;
        std::size_t bit_idx = pos % 32;
        if (digit_idx < digits_.size()) {
            digits_[digit_idx] &= ~(1u << bit_idx);
            normalize();
        }
        return *this;
    }

    // Power function
    static BigInt pow(const BigInt& base, std::uint64_t exp) {
        if (exp == 0) return BigInt(1ul);
        BigInt result(1ul);
        BigInt b = base;
        while (exp > 0) {
            if (exp & 1) result *= b;
            b *= b;
            exp >>= 1;
        }
        return result;
    }

    // GCD using binary GCD algorithm
    static BigInt gcd(BigInt a, BigInt b) {
        a.is_negative_ = false;
        b.is_negative_ = false;

        if (a.is_zero()) return b;
        if (b.is_zero()) return a;

        // Find common factors of 2
        std::size_t shift = 0;
        while (!a.test_bit(0) && !b.test_bit(0)) {
            a >>= 1;
            b >>= 1;
            ++shift;
        }

        while (!a.test_bit(0)) a >>= 1;

        do {
            while (!b.test_bit(0)) b >>= 1;
            if (a > b) std::swap(a, b);
            b -= a;
        } while (!b.is_zero());

        return a << shift;
    }

    // Explicit conversion to built-in types (may overflow)
    explicit operator std::int64_t() const {
        std::int64_t result = 0;
        for (std::size_t i = 0; i < std::min(digits_.size(), std::size_t(2)); ++i) {
            result |= static_cast<std::int64_t>(digits_[i]) << (i * 32);
        }
        return is_negative_ ? -result : result;
    }

    explicit operator std::uint64_t() const {
        if (is_negative_) throw std::domain_error("Cannot convert negative BigInt to unsigned");
        std::uint64_t result = 0;
        for (std::size_t i = 0; i < std::min(digits_.size(), std::size_t(2)); ++i) {
            result |= static_cast<std::uint64_t>(digits_[i]) << (i * 32);
        }
        return result;
    }

    explicit operator bool() const noexcept { return !is_zero(); }

    // Check if the value can fit into the given integer type and optionally write to it
    // Returns true if the value fits, false otherwise
    // Asserts that sign compatibility is checked by caller (negative BigInt cannot go into
    // unsigned)
    template <std::integral T>
    bool fits_in(T& out) const noexcept {
        if constexpr (std::is_unsigned_v<T>) {
            constexpr std::size_t target_bits = sizeof(T) * 8;
            if (is_negative_ || (bit_length() > target_bits)) return false;

            // Build the value and check
            T value = 0;
            constexpr std::size_t digits_needed = (target_bits + 31) / 32;
            for (std::size_t i = 0; i < std::min(digits_.size(), digits_needed); ++i) {
                if constexpr (sizeof(T) <= 4) {
                    value = static_cast<T>(digits_[0]);
                } else {
                    value |= static_cast<T>(digits_[i]) << (i * 32);
                }
            }
            out = value;
            return true;
        } else {
            // Signed type
            constexpr std::size_t target_bits = sizeof(T) * 8;
            // For signed, we need one less bit for magnitude (sign bit)
            std::size_t magnitude_bits = bit_length();

            if (is_negative_) {
                // For negative: can represent down to -(2^(n-1))
                // -128 for int8_t needs 7 bits of magnitude but is valid
                // Check: magnitude <= 2^(n-1), i.e. magnitude_bits <= n-1,
                // OR magnitude == 2^(n-1) exactly (the min value case)
                if (magnitude_bits > target_bits - 1) {
                    // Could still be the minimum value case
                    if (magnitude_bits == target_bits) {
                        // Check if it's exactly 2^(n-1): only top bit set in top digit
                        bool is_min_value = true;
                        for (std::size_t i = 0; i < digits_.size() - 1; ++i) {
                            if (digits_[i] != 0) {
                                is_min_value = false;
                                break;
                            }
                        }
                        std::uint32_t top = digits_.back();
                        std::uint32_t expected_top_bit = 1u << ((target_bits - 1) % 32);
                        if (top != expected_top_bit) is_min_value = false;
                        if (!is_min_value) return false;
                    } else {
                        return false;
                    }
                }
            } else {
                // For positive: can represent up to 2^(n-1) - 1
                if (magnitude_bits >= target_bits) return false;
            }

            // Build the value
            using UnsignedT = std::make_unsigned_t<T>;
            UnsignedT unsigned_val = 0;
            constexpr std::size_t digits_needed = (target_bits + 31) / 32;
            for (std::size_t i = 0; i < std::min(digits_.size(), digits_needed); ++i) {
                if constexpr (sizeof(T) <= 4) {
                    unsigned_val = static_cast<UnsignedT>(digits_[0]);
                } else {
                    unsigned_val |= static_cast<UnsignedT>(digits_[i]) << (i * 32);
                }
            }

            if (is_negative_) {
                out = static_cast<T>(-static_cast<T>(unsigned_val));
            } else {
                out = static_cast<T>(unsigned_val);
            }
            return true;
        }
    }

    // Overload that just checks without writing
    template <std::integral T>
    bool fits_in() const noexcept {
        T dummy;
        return fits_in(dummy);
    }
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
