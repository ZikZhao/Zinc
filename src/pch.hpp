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

template <typename... Ts>
    requires(std::is_pointer_v<Ts> && ...)
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

class GlobalMemory {
private:
    /// Used by TTP deduction for range collectors only
    template <typename ElementType>
    using DynamicSpan = std::span<ElementType, std::dynamic_extent>;

private:
    static auto global_pool() noexcept -> std::pmr::memory_resource* {
        constexpr std::pmr::pool_options pool_opts{
            .largest_required_pool_block = 4uz * 1024 * 1024,  // 4 MB
        };
        static std::pmr::unsynchronized_pool_resource resource(
            pool_opts, std::pmr::new_delete_resource()
        );
        return &resource;
    }

public:
    static auto monotonic() noexcept -> std::pmr::monotonic_buffer_resource* {
        static thread_local std::pmr::monotonic_buffer_resource resource(
            128uz * 1024, global_pool()
        );
        return &resource;
    }

    static auto local_pool() noexcept -> std::pmr::unsynchronized_pool_resource* {
        constexpr std::pmr::pool_options pool_opts{
            .largest_required_pool_block = 4uz * 1024,  // 4 KB
        };
        static thread_local std::pmr::unsynchronized_pool_resource resource(
            pool_opts, global_pool()
        );
        return &resource;
    }

    template <typename T>
    class Allocator {
    public:
        using value_type = T;

    public:
        Allocator() noexcept = default;

        template <typename U>
        Allocator(const Allocator<U>&) noexcept {}

        auto allocate(std::size_t n) -> T* {
            return static_cast<T*>(local_pool()->allocate(n * sizeof(T), alignof(T)));
        }

        void deallocate(T* p, std::size_t n) noexcept {
            local_pool()->deallocate(p, n * sizeof(T), alignof(T));
        }

        template <typename U>
        struct rebind {
            using other = Allocator<U>;
        };

        template <typename U>
        auto operator==(const Allocator<U>&) const noexcept -> bool {
            return true;
        }

        template <typename U>
        auto operator!=(const Allocator<U>&) const noexcept -> bool {
            return false;
        }
    };

    struct MonotonicAllocated {
        static auto operator new(std::size_t size) noexcept -> void* {
            return GlobalMemory::alloc_raw(size);
        }
        static void operator delete(void* ptr, std::size_t size) {}
    };

    template <typename T>
    using Vector = std::vector<T, Allocator<T>>;

    template <typename K, typename C = std::less<K>>
    using Set = std::set<K, C, Allocator<K>>;

    template <typename K, typename V, typename C = std::less<K>>
    using Map = std::map<K, V, C, Allocator<std::pair<const K, V>>>;

    template <typename K, typename V, typename H = std::hash<K>, typename P = std::equal_to<K>>
    using UnorderedMap = std::unordered_map<K, V, H, P, Allocator<std::pair<const K, V>>>;

    using String = std::basic_string<char, std::char_traits<char>, Allocator<char>>;

    template <typename K, typename V, typename C = std::less<K>>
    class FlatMap;

    template <typename K, typename C = std::less<K>>
    class FlatSet;

    template <typename K, typename V, typename H = std::hash<K>, typename P = std::equal_to<K>>
    using UnorderedMultiMap = std::unordered_multimap<K, V, H, P, Allocator<std::pair<const K, V>>>;

    template <typename K, typename V, typename C = std::less<K>>
    class FlatMultiMap;

    static auto alloc_raw(std::size_t size, std::size_t align = alignof(std::max_align_t))
        -> void* {
        return monotonic()->allocate(size, align);
    }

    template <typename T>
    static auto alloc_raw(std::type_identity<T>) -> T* {
        return static_cast<T*>(alloc_raw(sizeof(T), alignof(T)));
    }

    template <typename T, typename... Args>
    static constexpr auto alloc(Args&&... args) -> T* {
        void* ptr = monotonic()->allocate(sizeof(T), alignof(T));
        return new (ptr) T(std::forward<Args>(args)...);
    }

    template <typename T>
    static constexpr auto alloc_array(std::size_t n) -> std::span<T> {
        void* ptr = monotonic()->allocate(n * sizeof(T), alignof(T));
        return std::span<T>(static_cast<T*>(ptr), n);
    }

    template <typename T, typename... Args>
        requires(std::is_same_v<T, Args> && ...)
    static constexpr auto pack_array(T&& first, Args&&... rest) -> std::span<std::decay_t<T>> {
        std::span span = alloc_array<std::decay_t<T>>(sizeof...(rest) + 1);
        std::size_t index = 0;
        span[0] = std::forward<T>(first);
        ((span[++index] = std::forward<Args>(rest)), ...);
        return span;
    }

    static constexpr auto persist(const GlobalMemory::String& str) -> strview {
        std::span<char> ptr = alloc_array<char>(str.size() + 1);
        std::ranges::copy(str, ptr.begin());
        return {ptr.data(), str.size()};
    }

private:
    template <typename Container>
        requires requires { typename Container::value_type; }
    class RangeCollector {
        template <std::ranges::input_range Range>
        friend auto operator|(Range&& range, RangeCollector) -> Container {
            if constexpr (requires { Container(std::from_range, std::forward<Range>(range)); }) {
                return Container(std::from_range, std::forward<Range>(range));
            } else {
                auto common = std::forward<Range>(range) | std::views::common;
                return Container(common.begin(), common.end());
            }
        }
    };

    template <template <typename...> typename ContainerTemplate>
    class DeducingRangeCollector {
        template <std::ranges::input_range Range>
        friend auto operator|(Range&& range, DeducingRangeCollector<ContainerTemplate>) {
            using ElementType = std::ranges::range_value_t<Range>;
            if constexpr (requires { typename ContainerTemplate<int, int>::mapped_type; }) {
                static_assert(requires { typename ElementType::first_type; });
                return std::forward<Range>(range) | RangeCollector<ContainerTemplate<
                                                        typename ElementType::first_type,
                                                        typename ElementType::second_type>>{};
            } else {
                return std::forward<Range>(range) |
                       RangeCollector<ContainerTemplate<ElementType>>{};
            }
        }
    };

public:
    template <typename Container>
    static constexpr auto collect() {
        return RangeCollector<Container>{};
    }

    template <template <typename> typename ContainerTemplate>
    static constexpr auto collect() {
        return DeducingRangeCollector<ContainerTemplate>{};
    }

    template <template <typename, typename> typename ContainerTemplate>
    static constexpr auto collect() {
        return DeducingRangeCollector<ContainerTemplate>{};
    }

    template <typename... Args>
    static auto format(std::format_string<Args...> fmt, Args&&... args) -> GlobalMemory::String {
        GlobalMemory::String result;
        std::format_to(std::back_inserter(result), fmt, std::forward<Args>(args)...);
        return result;
    }

    template <typename... Args>
    static auto format_view(std::format_string<Args...> fmt, Args&&... args) -> strview {
        std::size_t size = std::formatted_size(fmt, std::forward<Args>(args)...);
        std::span<char> result = alloc_array<char>(size);
        std::format_to(result.begin(), fmt, std::forward<Args>(args)...);
        return {result.data(), result.size()};
    }

    static auto hex_string(strview input) -> strview {
        constexpr strview hex_chars = "0123456789ABCDEF";
        std::span<char> result = alloc_array<char>(input.size() * 2 + 3);
        for (std::size_t i = 0; i < input.size(); ++i) {
            auto byte = static_cast<unsigned char>(input[i]);
            result[i * 2 + 1] = hex_chars[(byte >> 4) & 0x0F];
            result[i * 2 + 2] = hex_chars[byte & 0x0F];
        }
        result[0] = '\"';
        result[input.size() * 2 + 1] = '\"';
        result[input.size() * 2 + 2] = '\0';
        return {result.data(), input.size() * 2 + 3};
    }

    static auto persist_string(strview str) -> strview {
        std::span<char> ptr = alloc_array<char>(str.size() + 1);
        std::ranges::copy(str, ptr.begin());
        return {ptr.data(), str.size()};
    }

public:
    GlobalMemory() = delete;
};

template <typename ElementType>
class GlobalMemory::RangeCollector<std::span<ElementType>> {
    template <std::ranges::input_range Range>
    friend auto operator|(Range&& range, RangeCollector) -> std::span<ElementType> {
        if constexpr (std::ranges::sized_range<Range>) {
            std::span span = alloc_array<ElementType>(std::ranges::size(range));
            std::ranges::uninitialized_copy(range, span);
            return span;
        } else {
            GlobalMemory::Vector<ElementType> temp;
            std::ranges::copy(std::forward<Range>(range), std::back_inserter(temp));
            std::span span = alloc_array<ElementType>(temp.size());
            std::ranges::uninitialized_move(temp, span);
            return span;
        }
    }
};

template <>
constexpr auto GlobalMemory::collect<std::span>() {
    return DeducingRangeCollector<DynamicSpan>{};
}

template <typename Key, typename Value, typename Comp>
class GlobalMemory::FlatMap {
public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using size_type = std::size_t;
    using iterator = typename Vector<value_type>::iterator;
    using const_iterator = typename Vector<value_type>::const_iterator;

private:
    Vector<value_type> data_;

    struct CompareFirst {
        bool operator()(const value_type& a, const value_type& b) const {
            return Comp{}(a.first, b.first);
        }
        bool operator()(const value_type& a, const Key& b) const { return Comp{}(a.first, b); }
        bool operator()(const Key& a, const value_type& b) const { return Comp{}(a, b.first); }
    };

public:
    FlatMap() noexcept = default;
    FlatMap(const FlatMap& other) noexcept = default;
    FlatMap& operator=(const FlatMap& other) noexcept = default;
    FlatMap(FlatMap&& other) noexcept = default;
    FlatMap& operator=(FlatMap&& other) noexcept = default;

    FlatMap(std::initializer_list<std::pair<Key, Value>> init) {
        data_.reserve(init.size());
        for (const auto& pair : init) {
            this->insert(pair);
        }
    }

    template <std::ranges::input_range R>
    FlatMap(std::from_range_t, R&& range) {
        Vector<value_type> unsorted;
        if constexpr (std::ranges::sized_range<R>) {
            unsorted.reserve(std::ranges::size(range));
        }
        for (auto&& pair : std::forward<R>(range)) {
            unsorted.push_back(std::forward<decltype(pair)>(pair));
        }
        std::sort(unsorted.begin(), unsorted.end(), CompareFirst{});
        auto last = std::unique(unsorted.begin(), unsorted.end(), [](const auto& a, const auto& b) {
            return a.first == b.first;
        });
        unsorted.erase(last, unsorted.end());
        data_ = std::move(unsorted);
    }

    constexpr auto size() const noexcept -> std::size_t { return data_.size(); }

    constexpr auto empty() const noexcept -> bool { return data_.empty(); }

    constexpr auto clear() noexcept -> void { data_.clear(); }

    auto insert(std::pair<Key, Value> pair) -> std::pair<iterator, bool> {
        auto it = std::lower_bound(data_.begin(), data_.end(), pair.first, CompareFirst{});
        if (it != data_.end() && !Comp{}(pair.first, it->first)) {
            return {it, false};
        }
        return {data_.insert(it, std::move(pair)), true};
    }

    template <typename... Args>
    auto emplace(Args&&... args) -> std::pair<iterator, bool> {
        return insert(std::pair<Key, Value>(std::forward<Args>(args)...));
    }

    auto erase(iterator pos) -> iterator { return data_.erase(pos); }

    auto erase(const Key& key) -> size_type {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it != data_.end() && !Comp{}(key, it->first)) {
            data_.erase(it);
            return 1;
        }
        return 0;
    }

    auto find(const Key& key) -> iterator {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it == data_.end() || Comp{}(key, it->first)) {
            return this->end();
        }
        return it;
    }

    auto find(const Key& key) const -> const_iterator {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it == data_.end() || Comp{}(key, it->first)) {
            return this->end();
        }
        return it;
    }

    auto contains(const Key& key) const -> bool {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        return it != data_.end() && !Comp{}(key, it->first);
    }

    auto at(const Key& key) -> Value& {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it == data_.end() || Comp{}(key, it->first)) {
            throw std::out_of_range("Key not found in GlobalMemory::FlatMap");
        }
        return it->second;
    }

    auto at(const Key& key) const -> const Value& {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it == data_.end() || Comp{}(key, it->first)) {
            throw std::out_of_range("Key not found in GlobalMemory::FlatMap");
        }
        return it->second;
    }

    auto operator[](const Key& key) -> Value& {
        auto it = std::lower_bound(data_.begin(), data_.end(), key, CompareFirst{});
        if (it == data_.end() || Comp{}(key, it->first)) {
            it = data_.insert(it, value_type{key, Value{}});
        }
        return it->second;
    }

    auto begin() noexcept -> iterator { return data_.begin(); }
    auto end() noexcept -> iterator { return data_.end(); }
    auto begin() const noexcept -> const_iterator { return data_.begin(); }
    auto end() const noexcept -> const_iterator { return data_.end(); }

    auto operator<=>(const FlatMap<Key, Value, Comp>& other) const noexcept
        -> std::strong_ordering {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }

    auto operator==(const FlatMap<Key, Value, Comp>& other) const noexcept -> bool {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }
};

template <typename Key, typename Comp>
class GlobalMemory::FlatSet {
public:
    using key_type = Key;
    using value_type = Key;
    using iterator = typename Vector<Key>::iterator;
    using const_iterator = typename Vector<Key>::const_iterator;

private:
    Vector<Key> keys_;

public:
    FlatSet() noexcept = default;
    FlatSet(std::from_range_t, auto&& range) {
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
    FlatSet(std::initializer_list<Key> init) : FlatSet(std::from_range, init) {}

    auto begin() noexcept -> typename Vector<Key>::iterator { return keys_.begin(); }
    auto begin() const noexcept -> typename Vector<Key>::const_iterator { return keys_.begin(); }
    auto end() noexcept -> typename Vector<Key>::iterator { return keys_.end(); }
    auto end() const noexcept -> typename Vector<Key>::const_iterator { return keys_.end(); }

    auto empty() const noexcept -> bool { return keys_.empty(); }
    auto size() const noexcept -> std::size_t { return keys_.size(); }

    auto insert(const Key& key) -> std::pair<iterator, bool> {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it != keys_.end() && !Comp{}(key, *it)) {
            return {it, false};
        }
        return {keys_.emplace(it, key), true};
    }
    template <std::input_iterator It, std::sentinel_for<It> Sent>
    auto insert(It first, Sent last) -> void {
        for (; first != last; ++first) {
            this->insert(*first);
        }
    }
    auto emplace(auto&&... args) -> std::pair<iterator, bool> {
        Key key = Key(std::forward<decltype(args)>(args)...);
        return this->insert(std::move(key));
    }

    auto find(const Key& key) -> iterator {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || Comp{}(key, *it)) {
            return end();
        }
        return it;
    }
    auto find(const Key& key) const -> const_iterator {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || Comp{}(key, *it)) {
            return this->end();
        }
        return it;
    }
    auto contains(const Key& key) const -> bool {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        return it != keys_.end() && !Comp{}(key, *it);
    }

    // Erase by key - returns number of elements removed (0 or 1)
    auto erase(const Key& key) -> std::size_t {
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key, Comp{});
        if (it == keys_.end() || Comp{}(key, *it)) {
            return 0;
        }
        keys_.erase(it);
        return 1;
    }

    // Erase single element by iterator - returns iterator to next element
    auto erase(const_iterator pos) -> iterator { return keys_.erase(pos); }

    // Erase range [first, last) - returns iterator to next element after erased range
    auto erase(const_iterator first, const_iterator last) -> iterator {
        return keys_.erase(first, last);
    }

    // Clear all elements
    void clear() noexcept { keys_.clear(); }

    auto operator<=>(const FlatSet<Key, Comp>& other) const noexcept -> std::strong_ordering {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    auto operator==(const FlatSet<Key, Comp>& other) const noexcept -> bool {
        return std::equal(this->begin(), this->end(), other.begin(), other.end());
    }

    auto is_superset_of(const FlatSet<Key, Comp>& other) const noexcept -> bool {
        return std::includes(this->begin(), this->end(), other.begin(), other.end(), Comp{});
    }
    auto is_proper_superset_of(const FlatSet<Key, Comp>& other) const noexcept -> bool {
        return this->size() > other.size() && is_superset_of(other);
    }
    auto is_subset_of(const FlatSet<Key, Comp>& other) const noexcept -> bool {
        return other.is_superset_of(*this);
    }
    auto is_proper_subset_of(const FlatSet<Key, Comp>& other) const noexcept -> bool {
        return this->size() < other.size() && is_subset_of(other);
    }
};

template <typename K, typename V, typename C>
class GlobalMemory::FlatMultiMap {
public:
    using key_type = K;
    using mapped_type = V;
    using value_type = std::pair<K, V>;
    using iterator = typename Vector<value_type>::iterator;
    using const_iterator = typename Vector<value_type>::const_iterator;

private:
    Vector<value_type> entries_;

public:
    FlatMultiMap() noexcept = default;
    FlatMultiMap(std::initializer_list<std::pair<K, V>> init) {
        entries_.reserve(init.size());
        for (const auto& pair : init) {
            this->insert(pair);
        }
    }

    constexpr auto size() const noexcept -> std::size_t { return entries_.size(); }
    constexpr auto empty() const noexcept -> bool { return entries_.empty(); }

    auto insert(std::pair<K, V> pair) -> iterator {
        auto compare = [](const value_type& a, const value_type& b) {
            return C{}(a.first, b.first);
        };
        auto it = std::upper_bound(entries_.begin(), entries_.end(), pair, compare);
        return entries_.insert(it, std::move(pair));
    }

    template <typename... Args>
    auto emplace(const K& key, Args&&... args) -> iterator {
        return insert(std::pair<K, V>(key, V(std::forward<Args>(args)...)));
    }

    auto find(const K& key) -> iterator {
        auto compare = [&key](const value_type& entry) { return C{}(entry.first, key); };
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), key, [](const value_type& entry, const K& k) {
                return C{}(entry.first, k);
            }
        );
        if (it == entries_.end() || C{}(key, it->first)) {
            return end();
        }
        return it;
    }

    auto find(const K& key) const -> const_iterator {
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), key, [](const value_type& entry, const K& k) {
                return C{}(entry.first, k);
            }
        );
        if (it == entries_.end() || C{}(key, it->first)) {
            return end();
        }
        return it;
    }

    auto equal_range(const K& key) -> std::pair<iterator, iterator> {
        auto lower_compare = [](const value_type& entry, const K& k) {
            return C{}(entry.first, k);
        };
        auto upper_compare = [](const K& k, const value_type& entry) {
            return C{}(k, entry.first);
        };
        auto lower = std::lower_bound(entries_.begin(), entries_.end(), key, lower_compare);
        auto upper = std::upper_bound(entries_.begin(), entries_.end(), key, upper_compare);
        return {lower, upper};
    }

    auto equal_range(const K& key) const -> std::pair<const_iterator, const_iterator> {
        auto lower_compare = [](const value_type& entry, const K& k) {
            return C{}(entry.first, k);
        };
        auto upper_compare = [](const K& k, const value_type& entry) {
            return C{}(k, entry.first);
        };
        auto lower = std::lower_bound(entries_.begin(), entries_.end(), key, lower_compare);
        auto upper = std::upper_bound(entries_.begin(), entries_.end(), key, upper_compare);
        return {lower, upper};
    }

    auto count(const K& key) const -> std::size_t {
        auto [lower, upper] = equal_range(key);
        return std::distance(lower, upper);
    }

    auto contains(const K& key) const -> bool {
        auto it = std::lower_bound(
            entries_.begin(), entries_.end(), key, [](const value_type& entry, const K& k) {
                return C{}(entry.first, k);
            }
        );
        return it != entries_.end() && !C{}(key, it->first);
    }

    auto erase(iterator pos) -> iterator { return entries_.erase(pos); }

    auto erase(const K& key) -> std::size_t {
        auto [lower, upper] = equal_range(key);
        std::size_t count = std::distance(lower, upper);
        if (count > 0) {
            entries_.erase(lower, upper);
        }
        return count;
    }

    void clear() noexcept { entries_.clear(); }

    auto begin() noexcept -> iterator { return entries_.begin(); }
    auto end() noexcept -> iterator { return entries_.end(); }
    auto begin() const noexcept -> const_iterator { return entries_.begin(); }
    auto end() const noexcept -> const_iterator { return entries_.end(); }

    auto operator<=>(const FlatMultiMap<K, V, C>& other) const noexcept -> std::strong_ordering {
        return std::lexicographical_compare_three_way(
            this->begin(), this->end(), other.begin(), other.end()
        );
    }
    bool operator==(const FlatMultiMap<K, V, C>& other) const noexcept {
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

auto escape_string(strview input) -> GlobalMemory::String {
    GlobalMemory::String result;
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
