#pragma once
#include <compare>
#include <map>
#include <random>
#include <type_traits>

#include "gtest/gtest.h"

enum class MapOperation {
    Insert,
    InsertOrAssign,
    TryEmplace,
    AccessOperator,
    At,
    EraseByKey,
    EraseByIterator,
    Clear,
    Contains,
    Find,
    LowerBound,
    _Size,
};

template <typename T>
class ComparableUniquePtr {
private:
    std::unique_ptr<T> ptr_;

public:
    ComparableUniquePtr() = default;
    ComparableUniquePtr(T* ptr) : ptr_(ptr) {}
    ComparableUniquePtr(const ComparableUniquePtr& other) = delete;
    ComparableUniquePtr& operator=(const ComparableUniquePtr& other) = delete;
    ComparableUniquePtr(ComparableUniquePtr&& other) noexcept = default;
    ComparableUniquePtr& operator=(ComparableUniquePtr&& other) noexcept = default;
    T& operator*() { return *ptr_; }
    const T& operator*() const { return *ptr_; }
    std::strong_ordering operator<=>(const ComparableUniquePtr& other) const {
        return *ptr_ <=> *other.ptr_;
    }
    bool operator==(const ComparableUniquePtr& other) const { return *ptr_ == *other.ptr_; }
};

template <typename Impl>
class FlatMapTest : public ::testing::Test {
protected:
    static_assert(std::ranges::range<Impl>, "Implementation must be a Range!");
    static_assert(requires(Impl m) { m.size(); }, "Implementation must have size()!");

protected:
    using Key = typename Impl::key_type;
    using Value = typename Impl::mapped_type;
    using StdMapType = std::map<Key, Value>;

protected:
    Impl f_map;
    StdMapType s_map;

protected:
    Key key(int value) {
        if constexpr (std::is_same_v<Key, int>) {
            return value;
        } else if constexpr (std::is_same_v<Key, std::string>) {
            return "key_" + std::to_string(value);
        } else if constexpr (std::is_same_v<Key, ComparableUniquePtr<int>>) {
            return ComparableUniquePtr<int>(new int(value));
        } else {
            static_assert(false);
        }
    }

    Value value(int value) {
        if constexpr (std::is_same_v<Value, int>) {
            return value * 10;
        } else if constexpr (std::is_same_v<Value, std::string>) {
            return "value_" + std::to_string(value);
        } else if constexpr (std::is_same_v<Value, ComparableUniquePtr<int>>) {
            return ComparableUniquePtr<int>(new int(value * 10));
        } else {
            static_assert(false);
        }
    }

    void check_consistency(const Impl& impl, const StdMapType& oracle) {
        if constexpr (requires { impl.size(); }) {
            ASSERT_EQ(impl.size(), oracle.size()) << "Size mismatch!";
        }
        if constexpr (requires { impl.empty(); }) {
            ASSERT_EQ(impl.empty(), oracle.empty()) << "Empty() mismatch!";
        }

        auto it_c = impl.begin();
        auto it_o = oracle.begin();
        auto end_c = impl.end();
        auto end_o = oracle.end();

        int index = 0;
        for (; it_c != end_c && it_o != end_o; ++it_c, ++it_o, ++index) {
            ASSERT_EQ(it_c->first, it_o->first) << "Key mismatch at index " << index;
            ASSERT_EQ(it_c->second, it_o->second) << "Value mismatch at key: " << it_c->first;
        }
        ASSERT_TRUE(it_c == end_c && it_o == end_o)
            << "Iterator reached end mismatch (Length differs)";
    }

    void fuzz_test(int iterations) {
        std::mt19937 gen(42);

        for (int i = 0; i < iterations; ++i) {
            MapOperation op = static_cast<MapOperation>(
                std::uniform_int_distribution<int>(0, static_cast<int>(MapOperation::_Size) - 1)(
                    gen
                )
            );
            Key k = key(i);
            Value v = value(i);

            switch (op) {
            case MapOperation::Insert:
                if constexpr (requires { f_map.insert({k, v}); }) {
                    if (!s_map.count(k)) {
                        f_map.insert({k, v});
                        s_map.insert({k, v});
                    }
                }
                break;

            case MapOperation::InsertOrAssign:
                if constexpr (requires { f_map.insert_or_assign(k, v); }) {
                    f_map.insert_or_assign(k, v);
                    s_map.insert_or_assign(k, v);
                }
                break;

            case MapOperation::TryEmplace:
                if constexpr (requires { f_map.try_emplace(k, v); }) {
                    if (!s_map.count(k)) {
                        f_map.try_emplace(k, v);
                        s_map.try_emplace(k, v);
                    }
                }
                break;

            case MapOperation::AccessOperator:
                if constexpr (requires { f_map[k]; }) {
                    if constexpr (std::is_default_constructible_v<Value> &&
                                  std::is_copy_assignable_v<Value>) {
                        f_map[k] = v;
                        s_map[k] = v;
                    }
                }
                break;

            case MapOperation::At:
                if constexpr (requires { f_map.at(k) = v; }) {
                    if (s_map.count(k)) {
                        f_map.at(k) = v;
                        s_map.at(k) = v;
                    }
                }
                break;

            case MapOperation::EraseByKey:
                if constexpr (requires { f_map.erase(k); }) {
                    if (s_map.count(k)) {
                        f_map.erase(k);
                        s_map.erase(k);
                    }
                }
                break;

            case MapOperation::EraseByIterator:
                if constexpr (requires { f_map.erase(f_map.begin()); }) {
                    if (s_map.contains(k)) {
                        auto it_f = f_map.find(k);
                        auto it_s = s_map.find(k);
                        ASSERT_NE(it_f, f_map.end());
                        ASSERT_NE(it_s, s_map.end());

                        f_map.erase(it_f);
                        s_map.erase(it_s);
                    }
                }
                break;

            case MapOperation::Clear:
                if constexpr (requires { f_map.clear(); }) {
                    if (std::uniform_int_distribution<int>(0, 50)(gen) == 0) {
                        f_map.clear();
                        s_map.clear();
                    }
                }
                break;

            case MapOperation::Contains:
                if constexpr (requires { f_map.contains(k); }) {
                    ASSERT_EQ(f_map.contains(k), s_map.contains(k));
                }
                break;

            case MapOperation::Find:
                if constexpr (requires { f_map.find(k); }) {
                    auto it_f = f_map.find(k);
                    auto it_s = s_map.find(k);
                    bool found_f = (it_f != f_map.end());
                    bool found_s = (it_s != s_map.end());
                    ASSERT_EQ(found_f, found_s);
                    if (found_f) {
                        ASSERT_EQ(it_f->second, it_s->second);
                    }
                }
                break;

            case MapOperation::LowerBound:
                if constexpr (requires { f_map.lower_bound(k); }) {
                    auto it_f = f_map.lower_bound(k);
                    auto it_s = s_map.lower_bound(k);

                    bool end_f = (it_f == f_map.end());
                    bool end_s = (it_s == s_map.end());
                    ASSERT_EQ(end_f, end_s);
                    if (!end_f) {
                        ASSERT_EQ(it_f->first, it_s->first);
                    }
                }
                break;

            case MapOperation::_Size:
                assert(false);
                break;
            }

            check_consistency(f_map, s_map);
        }
    }

    // insert(const value_type& value) -> pair<iterator, bool>
    void test_insert_basic() {
        using value_type = std::pair<const Key, Value>;
        if constexpr (!requires(Impl& m, const value_type& v) {
                          { m.insert(v) } -> std::same_as<std::pair<typename Impl::iterator, bool>>;
                      }) {
            GTEST_SKIP() << "insert(const value_type&) not implemented";
        } else {
            // Test inserting new element
            auto [it1, inserted1] = f_map.insert({key(1), value(10)});
            s_map.insert({key(1), value(10)});
            ASSERT_TRUE(inserted1);
            if constexpr (requires { it1->first; }) {
                ASSERT_EQ(it1->first, key(1));
                ASSERT_EQ(it1->second, value(10));
            }
            check_consistency(f_map, s_map);

            // Test inserting duplicate key (should fail)
            auto [it2, inserted2] = f_map.insert({key(1), value(20)});
            s_map.insert({key(1), value(20)});
            ASSERT_FALSE(inserted2);
            ASSERT_EQ(it2->second, value(10));
            check_consistency(f_map, s_map);

            // Test multiple different keys
            f_map.insert({key(2), value(20)});
            s_map.insert({key(2), value(20)});
            f_map.insert({key(3), value(30)});
            s_map.insert({key(3), value(30)});
            check_consistency(f_map, s_map);
        }
    }

    // insert_or_assign(const Key& k, M&& obj) -> pair<iterator, bool>
    void test_insert_or_assign() {
        if constexpr (!requires(Impl& m, const Key& k, Value&& v) {
                          {
                              m.insert_or_assign(k, std::move(v))
                          } -> std::same_as<std::pair<typename Impl::iterator, bool>>;
                      }) {
            GTEST_SKIP() << "insert_or_assign(const Key&, M&&) not implemented";
        } else {
            // Insert new element
            auto [it1, inserted1] = f_map.insert_or_assign(key(1), value(100));
            s_map.insert_or_assign(key(1), value(100));
            ASSERT_TRUE(inserted1);
            check_consistency(f_map, s_map);

            // Update existing element
            auto [it2, inserted2] = f_map.insert_or_assign(key(1), value(200));
            s_map.insert_or_assign(key(1), value(200));
            ASSERT_FALSE(inserted2);
            ASSERT_EQ(it2->second, value(200));  // Value should be updated
            check_consistency(f_map, s_map);

            // Test multiple updates
            f_map.insert_or_assign(key(1), value(300));
            s_map.insert_or_assign(key(1), value(300));
            f_map.insert_or_assign(key(2), value(400));
            s_map.insert_or_assign(key(2), value(400));
            check_consistency(f_map, s_map);
        }
    }

    // try_emplace(const Key& k, Args&&... args) -> pair<iterator, bool>
    void test_try_emplace() {
        if constexpr (!requires(Impl& m, const Key& k, Value&& v) {
                          {
                              m.try_emplace(k, std::move(v))
                          } -> std::same_as<std::pair<typename Impl::iterator, bool>>;
                      }) {
            GTEST_SKIP() << "try_emplace(const Key&, Args&&...) not implemented";
        } else {
            // Insert new element
            auto [it1, inserted1] = f_map.try_emplace(key(1), value(100));
            s_map.try_emplace(key(1), value(100));
            ASSERT_TRUE(inserted1);
            check_consistency(f_map, s_map);

            // Try inserting duplicate key (should fail, value unchanged)
            auto [it2, inserted2] = f_map.try_emplace(key(1), value(200));
            s_map.try_emplace(key(1), value(200));
            ASSERT_FALSE(inserted2);
            ASSERT_EQ(it2->second, value(100));  // Value should remain unchanged

            check_consistency(f_map, s_map);
        }
    }

    // operator[](const Key& k) -> mapped_type&
    void test_access_operator() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m[k] } -> std::same_as<Value&>;
                      }) {
            GTEST_SKIP() << "operator[](const Key&) not implemented";
        } else {
            // Insert new element via operator[]
            f_map[key(1)] = value(100);
            s_map[key(1)] = value(100);
            check_consistency(f_map, s_map);

            // Update existing element via operator[]
            f_map[key(1)] = value(200);
            s_map[key(1)] = value(200);
            ASSERT_EQ(f_map[key(1)], value(200));
            check_consistency(f_map, s_map);

            // Access non-existent key (creates default value)
            Value& v = f_map[key(2)];
            Value& v_std = s_map[key(2)];
            v = value(300);
            v_std = value(300);
            check_consistency(f_map, s_map);
        }
    }

    // at(const Key& k) -> mapped_type&
    void test_at_method() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m.at(k) } -> std::same_as<Value&>;
                      }) {
            GTEST_SKIP() << "at(const Key&) not implemented";
        } else {
            // 先插入一些元素
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});

                // Test accessing existing keys
                ASSERT_EQ(f_map.at(key(1)), value(100));
                ASSERT_EQ(f_map.at(key(2)), value(200));

                // Test modification
                f_map.at(key(1)) = value(150);
                s_map.at(key(1)) = value(150);
                ASSERT_EQ(f_map.at(key(1)), value(150));
                check_consistency(f_map, s_map);

                // Test accessing non-existent key (should throw exception)
                ASSERT_THROW(f_map.at(key(99)), std::out_of_range);
            }
        }
    }

    // erase(const Key& k) -> size_type
    void test_erase_by_key() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m.erase(k) } -> std::same_as<std::size_t>;
                      }) {
            GTEST_SKIP() << "erase(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Insert test data
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});
                check_consistency(f_map, s_map);

                // Erase existing key
                size_t erased1 = f_map.erase(key(2));
                size_t erased1_std = s_map.erase(key(2));
                ASSERT_EQ(erased1, erased1_std);
                ASSERT_EQ(erased1, 1);
                check_consistency(f_map, s_map);

                // Try erasing non-existent key
                size_t erased2 = f_map.erase(key(99));
                size_t erased2_std = s_map.erase(key(99));
                ASSERT_EQ(erased2, erased2_std);
                ASSERT_EQ(erased2, 0);
                check_consistency(f_map, s_map);

                // Erase remaining elements
                f_map.erase(key(1));
                s_map.erase(key(1));
                f_map.erase(key(3));
                s_map.erase(key(3));
                check_consistency(f_map, s_map);
                if constexpr (requires { f_map.empty(); }) {
                    ASSERT_TRUE(f_map.empty());
                }
            }
        }
    }

    // erase(iterator pos) -> iterator
    void test_erase_by_iterator() {
        if constexpr (!requires(Impl& m) {
                          { m.erase(m.begin()) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "erase(iterator) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Insert test data
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});

                // Erase first element
                auto it_f = f_map.begin();
                auto it_s = s_map.begin();
                f_map.erase(it_f);
                s_map.erase(it_s);
                check_consistency(f_map, s_map);

                // Erase middle element
                if constexpr (requires(Impl& m, const Key& k) { m.find(k); }) {
                    auto it2_f = f_map.find(key(2));
                    auto it2_s = s_map.find(key(2));
                    if (it2_f != f_map.end()) {
                        f_map.erase(it2_f);
                        s_map.erase(it2_s);
                        check_consistency(f_map, s_map);
                    }
                }
            }
        }
    }

    // 测试 clear() -> void
    void test_clear() {
        if constexpr (!requires(Impl& m) {
                          { m.clear() } -> std::same_as<void>;
                      }) {
            GTEST_SKIP() << "clear() not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // 插入一些元素
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});

                // 清空
                f_map.clear();
                s_map.clear();
                check_consistency(f_map, s_map);

                if constexpr (requires(const Impl& m) {
                                  { m.empty() } -> std::same_as<bool>;
                              }) {
                    ASSERT_TRUE(f_map.empty());
                }
                if constexpr (requires(const Impl& m) {
                                  { m.size() } -> std::same_as<std::size_t>;
                              }) {
                    ASSERT_EQ(f_map.size(), 0);
                }

                // 清空后再次插入
                f_map.insert({key(10), value(1000)});
                s_map.insert({key(10), value(1000)});
                check_consistency(f_map, s_map);
            }
        }
    }

    // contains(const Key& k) const -> bool
    void test_contains() {
        if constexpr (!requires(const Impl& m, const Key& k) {
                          { m.contains(k) } -> std::same_as<bool>;
                      }) {
            GTEST_SKIP() << "contains(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Find in empty map
                ASSERT_FALSE(f_map.contains(key(1)));

                // Find after insertion
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                ASSERT_TRUE(f_map.contains(key(1)));
                ASSERT_FALSE(f_map.contains(key(2)));

                // Insert more elements
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});

                ASSERT_TRUE(f_map.contains(key(1)));
                ASSERT_TRUE(f_map.contains(key(2)));
                ASSERT_TRUE(f_map.contains(key(3)));
                ASSERT_FALSE(f_map.contains(key(4)));
            }
        }
    }

    // find(const Key& k) -> iterator
    void test_find() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m.find(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "find(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Find in empty map
                auto it1 = f_map.find(key(1));
                ASSERT_EQ(it1, f_map.end());

                // Find after insertion
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});

                // Find existing key
                auto it2 = f_map.find(key(2));
                ASSERT_NE(it2, f_map.end());
                if constexpr (requires { it2->second; }) {
                    ASSERT_EQ(it2->first, key(2));
                    ASSERT_EQ(it2->second, value(200));
                }

                // Find non-existent key
                auto it3 = f_map.find(key(99));
                ASSERT_EQ(it3, f_map.end());

                // Modify value via iterator
                if constexpr (requires(typename Impl::iterator it) { it->second; }) {
                    auto it_modify = f_map.find(key(1));
                    auto it_modify_std = s_map.find(key(1));
                    if (it_modify != f_map.end()) {
                        it_modify->second = value(150);
                        it_modify_std->second = value(150);
                        check_consistency(f_map, s_map);
                    }
                }
            }
        }
    }

    // lower_bound(const Key& k) -> iterator
    void test_lower_bound() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m.lower_bound(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "lower_bound(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Insert non-consecutive keys
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(4), value(400)});
                s_map.insert({key(4), value(400)});
                f_map.insert({key(6), value(600)});
                s_map.insert({key(6), value(600)});
                f_map.insert({key(8), value(800)});
                s_map.insert({key(8), value(800)});

                // Find existing key
                auto it1 = f_map.lower_bound(key(4));
                auto it1_std = s_map.lower_bound(key(4));
                ASSERT_NE(it1, f_map.end());
                if constexpr (requires(typename Impl::iterator it) { it->first; }) {
                    ASSERT_EQ(it1->first, it1_std->first);
                    ASSERT_EQ(it1->first, key(4));
                }

                // Find non-existent key (should return next greater key)
                auto it2 = f_map.lower_bound(key(3));
                auto it2_std = s_map.lower_bound(key(3));
                ASSERT_NE(it2, f_map.end());
                if constexpr (requires(typename Impl::iterator it) { it->first; }) {
                    ASSERT_EQ(it2->first, it2_std->first);
                    ASSERT_EQ(it2->first, key(4));
                }

                // Find value smaller than all keys
                auto it3 = f_map.lower_bound(key(1));
                auto it3_std = s_map.lower_bound(key(1));
                ASSERT_NE(it3, f_map.end());
                if constexpr (requires(typename Impl::iterator it) { it->first; }) {
                    ASSERT_EQ(it3->first, it3_std->first);
                    ASSERT_EQ(it3->first, key(2));
                }

                // Find value greater than all keys
                auto it4 = f_map.lower_bound(key(10));
                auto it4_std = s_map.lower_bound(key(10));
                ASSERT_EQ(it4, f_map.end());
                ASSERT_EQ(it4_std, s_map.end());
            }
        }
    }

    // upper_bound(const Key& k) -> iterator
    void test_upper_bound() {
        if constexpr (!requires(Impl& m, const Key& k) {
                          { m.upper_bound(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "upper_bound(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& m) {
                              m.insert(std::declval<std::pair<const Key, Value>>());
                          }) {
                // Insert non-consecutive keys
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(4), value(400)});
                s_map.insert({key(4), value(400)});
                f_map.insert({key(6), value(600)});
                s_map.insert({key(6), value(600)});

                // Find existing key (should return next greater key)
                auto it1 = f_map.upper_bound(key(4));
                auto it1_std = s_map.upper_bound(key(4));
                ASSERT_NE(it1, f_map.end());
                if constexpr (requires(typename Impl::iterator it) { it->first; }) {
                    ASSERT_EQ(it1->first, it1_std->first);
                    ASSERT_EQ(it1->first, key(6));
                }

                // Find non-existent key
                auto it2 = f_map.upper_bound(key(3));
                auto it2_std = s_map.upper_bound(key(3));
                ASSERT_NE(it2, f_map.end());
                if constexpr (requires(typename Impl::iterator it) { it->first; }) {
                    ASSERT_EQ(it2->first, it2_std->first);
                    ASSERT_EQ(it2->first, key(4));
                }
            }
        }
    }

    // size() const -> size_type and empty() const -> bool
    void test_size_and_empty() {
        if constexpr (!requires(const Impl& m) {
                          { m.size() } -> std::same_as<std::size_t>;
                          { m.empty() } -> std::same_as<bool>;
                      }) {
            GTEST_SKIP() << "size() or empty() not implemented";
        } else {
            // Initial state
            ASSERT_TRUE(f_map.empty());
            ASSERT_EQ(f_map.size(), 0);

            if constexpr (requires { f_map.insert({Key{}, Value{}}); }) {
                // Insert one element
                f_map.insert({key(1), value(100)});
                s_map.insert({key(1), value(100)});
                ASSERT_FALSE(f_map.empty());
                ASSERT_EQ(f_map.size(), 1);

                // Insert more elements
                f_map.insert({key(2), value(200)});
                s_map.insert({key(2), value(200)});
                f_map.insert({key(3), value(300)});
                s_map.insert({key(3), value(300)});
                ASSERT_EQ(f_map.size(), 3);

                // Erase elements
                if constexpr (requires(Impl& m, const Key& k) { m.erase(k); }) {
                    f_map.erase(key(2));
                    s_map.erase(key(2));
                    ASSERT_EQ(f_map.size(), 2);

                    f_map.erase(key(1));
                    s_map.erase(key(1));
                    f_map.erase(key(3));
                    s_map.erase(key(3));
                    ASSERT_TRUE(f_map.empty());
                    ASSERT_EQ(f_map.size(), 0);
                }
            }
        }
    }

    // begin() -> iterator and end() -> iterator
    void test_iterator_traversal() {
        if constexpr (requires(Impl& m) {
                          m.insert(std::declval<std::pair<const Key, Value>>());
                      }) {
            // Insert ordered elements
            f_map.insert({key(1), value(100)});
            s_map.insert({key(1), value(100)});
            f_map.insert({key(2), value(200)});
            s_map.insert({key(2), value(200)});
            f_map.insert({key(3), value(300)});
            s_map.insert({key(3), value(300)});
            f_map.insert({key(4), value(400)});
            s_map.insert({key(4), value(400)});

            // Traverse using iterator
            check_consistency(f_map, s_map);

            // Count elements
            int count = 0;
            for (auto it = f_map.begin(); it != f_map.end(); ++it) {
                count++;
            }
            if constexpr (requires(const Impl& m) {
                              { m.size() } -> std::same_as<std::size_t>;
                          }) {
                ASSERT_EQ(count, f_map.size());
            }
        }
    }

    // Edge cases
    void test_edge_cases() {
        if constexpr (!requires(Impl& m) {
                          m.insert(std::declval<std::pair<const Key, Value>>());
                      }) {
            GTEST_SKIP() << "insert() not implemented, cannot test edge cases";
        } else {
            // Insert then immediately erase
            f_map.insert({key(1), value(100)});
            s_map.insert({key(1), value(100)});
            if constexpr (requires { f_map.erase(Key{}); }) {
                f_map.erase(key(1));
                s_map.erase(key(1));
                check_consistency(f_map, s_map);
            }

            // Insert same key multiple times
            f_map.insert({key(2), value(200)});
            s_map.insert({key(2), value(200)});
            f_map.insert({key(2), value(201)});
            s_map.insert({key(2), value(201)});
            f_map.insert({key(2), value(202)});
            s_map.insert({key(2), value(202)});
            check_consistency(f_map, s_map);

            // Use after clearing
            if constexpr (requires(Impl& m) {
                              { m.clear() } -> std::same_as<void>;
                          }) {
                f_map.clear();
                s_map.clear();
                f_map.insert({key(5), value(500)});
                s_map.insert({key(5), value(500)});
                check_consistency(f_map, s_map);
            }
        }
    }
};
