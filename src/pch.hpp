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

template <typename R, typename T>
concept ForwardRange =
    std::ranges::forward_range<R> && std::convertible_to<std::ranges::range_reference_t<R>, T>;

template <typename R, typename T>
concept RandomAccessRange = std::ranges::random_access_range<R> && std::ranges::sized_range<R> &&
                            std::convertible_to<std::ranges::range_reference_t<R>, T>;

template <typename T>
constexpr auto opaque_cast(auto* ptr) {
    /// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    return reinterpret_cast<T>(ptr);
}

template <std::same_as<std::size_t>... Ts>
auto hash_combine(std::size_t first, const Ts&... rest) noexcept -> std::size_t {
    ((first ^= rest + 0x9e3779b9 + (first << 6) + (first >> 2)), ...);
    return first;
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
    static auto format_view(std::format_string<Args...> fmt, Args&&... args) -> std::string_view {
        std::size_t size = std::formatted_size(fmt, std::forward<Args>(args)...);
        std::span<char> result = alloc_array<char>(size);
        std::format_to(result.begin(), fmt, std::forward<Args>(args)...);
        return {result.data(), result.size()};
    }

    static auto hex_string(std::string_view input) -> std::string_view {
        constexpr std::string_view hex_chars = "0123456789ABCDEF";
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

    [[nodiscard]] constexpr auto size() const noexcept -> std::size_t { return entries_.size(); }
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return entries_.empty(); }

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

class BigInt {
    friend struct std::hash<BigInt>;

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

    auto is_zero() const noexcept -> bool { return digits_.size() == 1 && digits_[0] == 0; }

    // Compare absolute values: -1 if |this| < |other|, 0 if equal, 1 if |this| > |other|
    auto compare_abs(const BigInt& other) const noexcept -> int {
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
    static auto add_abs(const BigInt& a, const BigInt& b) -> BigInt {
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
    static auto sub_abs(const BigInt& a, const BigInt& b) -> BigInt {
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
    auto mul_digit(std::uint32_t d) const -> BigInt {
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
    auto shift_digits_left(std::size_t n) const -> BigInt {
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
    auto div_mod_abs(const BigInt& divisor) const -> std::pair<BigInt, BigInt> {
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
    auto to_string() const -> GlobalMemory::String {
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
    auto operator<=>(const BigInt& other) const noexcept -> std::strong_ordering {
        if (is_negative_ != other.is_negative_) {
            return is_negative_ ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        int cmp = compare_abs(other);
        if (is_negative_) cmp = -cmp;
        if (cmp < 0) return std::strong_ordering::less;
        if (cmp > 0) return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    auto operator==(const BigInt& other) const noexcept -> bool {
        return is_negative_ == other.is_negative_ && digits_ == other.digits_;
    }

    // Unary operators
    auto operator-() const -> BigInt {
        BigInt result = *this;
        if (!result.is_zero()) {
            result.is_negative_ = !result.is_negative_;
        }
        return result;
    }

    auto operator+() const -> BigInt { return *this; }

    auto abs() const -> BigInt {
        BigInt result = *this;
        result.is_negative_ = false;
        return result;
    }

    // Arithmetic operators
    auto operator+(const BigInt& other) const -> BigInt {
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
    auto operator~() const -> BigInt { return -(*this) - BigInt(1ul); }

    auto operator&=(const BigInt& other) -> BigInt& { return *this = *this & other; }
    auto operator|=(const BigInt& other) -> BigInt& { return *this = *this | other; }
    auto operator^=(const BigInt& other) -> BigInt& { return *this = *this ^ other; }

    // Utility functions
    auto is_negative() const noexcept -> bool { return is_negative_; }
    auto is_positive() const noexcept -> bool { return !is_negative_ && !is_zero(); }

    // Get the number of bits needed to represent this number
    auto bit_length() const noexcept -> std::size_t {
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
    auto test_bit(std::size_t pos) const noexcept -> bool {
        std::size_t digit_idx = pos / 32;
        std::size_t bit_idx = pos % 32;
        if (digit_idx >= digits_.size()) return false;
        return (digits_[digit_idx] >> bit_idx) & 1;
    }

    // Set a specific bit
    auto set_bit(std::size_t pos) -> BigInt& {
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
    auto clear_bit(std::size_t pos) -> BigInt& {
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
    static auto pow(const BigInt& base, std::uint64_t exp) -> BigInt {
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
    static auto gcd(BigInt a, BigInt b) -> BigInt {
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
    auto fits_in(T& out) const noexcept -> bool {
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
    auto fits_in() const noexcept -> bool {
        T dummy;
        return fits_in(dummy);
    }
};

template <>
struct std::hash<BigInt> {
    auto operator()(const BigInt& value) const noexcept -> std::size_t {
        std::size_t seed = std::hash<bool>{}(value.is_negative());
        for (std::uint32_t digit : value.digits_) {
            seed = hash_combine(seed, std::hash<std::uint32_t>{}(digit));
        }
        return seed;
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
