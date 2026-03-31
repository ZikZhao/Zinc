#include <random>
#include <set>
#include <type_traits>
#include <vector>

#include "gtest/gtest.h"

enum class SetOperation {
    Insert,
    Emplace,
    EraseByKey,
    EraseByIterator,
    Clear,
    Contains,
    Find,
    LowerBound,
    UpperBound,
    _Size,
};

template <typename Impl>
class FlatSetTest : public ::testing::Test {
protected:
    static_assert(std::ranges::range<Impl>, "Implementation must be a Range!");
    static_assert(requires(Impl s) { s.size(); }, "Implementation must have size()!");

protected:
    using Key = typename Impl::key_type;
    using Value = typename Impl::value_type;
    using StdSetType = std::set<Key>;

protected:
    Impl f_set;
    StdSetType s_set;

protected:
    auto key(int seed) -> Key {
        if constexpr (std::is_same_v<Key, int>) {
            return seed;
        } else if constexpr (std::is_same_v<Key, std::string>) {
            return "key_" + std::to_string(seed);
        } else if constexpr (requires { Key(std::declval<int*>()); }) {
            return Key(new int(seed));
        } else {
            static_assert(false);
        }
    }

    void check_consistency(const Impl& impl, const StdSetType& oracle) {
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
            ASSERT_EQ(*it_c, *it_o) << "Value mismatch at index " << index;
        }
        ASSERT_TRUE(it_c == end_c && it_o == end_o)
            << "Iterator reached end mismatch (Length differs)";
    }

    // insert(const value_type& value) -> pair<iterator, bool>
    void test_insert_basic() {
        if constexpr (!requires(Impl& s, const Key& k) {
                          { s.insert(k) } -> std::same_as<std::pair<typename Impl::iterator, bool>>;
                      }) {
            GTEST_SKIP() << "insert(const value_type&) not implemented";
        } else {
            // Test inserting new element
            auto [it1, inserted1] = f_set.insert(key(1));
            s_set.insert(key(1));
            ASSERT_TRUE(inserted1);
            if constexpr (requires { *it1; }) {
                ASSERT_EQ(*it1, key(1));
            }
            check_consistency(f_set, s_set);

            // Test inserting duplicate key (should fail)
            auto [it2, inserted2] = f_set.insert(key(1));
            s_set.insert(key(1));
            ASSERT_FALSE(inserted2);
            ASSERT_EQ(*it2, key(1));
            check_consistency(f_set, s_set);

            // Test multiple different keys
            f_set.insert(key(2));
            s_set.insert(key(2));
            f_set.insert(key(3));
            s_set.insert(key(3));
            check_consistency(f_set, s_set);
        }
    }

    // emplace(Args&&... args) -> pair<iterator, bool>
    void test_emplace() {
        if constexpr (!requires(Impl& s, Key&& k) {
                          {
                              s.emplace(std::move(k))
                          } -> std::same_as<std::pair<typename Impl::iterator, bool>>;
                      }) {
            GTEST_SKIP() << "emplace(Args&&...) not implemented";
        } else {
            // Insert new element
            auto [it1, inserted1] = f_set.emplace(key(1));
            s_set.emplace(key(1));
            ASSERT_TRUE(inserted1);
            check_consistency(f_set, s_set);

            // Try inserting duplicate key (should fail)
            auto [it2, inserted2] = f_set.emplace(key(1));
            s_set.emplace(key(1));
            ASSERT_FALSE(inserted2);
            ASSERT_EQ(*it2, key(1));
            check_consistency(f_set, s_set);

            // Test multiple different keys
            f_set.emplace(key(2));
            s_set.emplace(key(2));
            f_set.emplace(key(3));
            s_set.emplace(key(3));
            check_consistency(f_set, s_set);
        }
    }

    // insert(InputIt first, InputIt last) -> void
    void test_insert_range() {
        std::vector<Key> values = {key(3), key(1), key(2), key(2), key(1)};

        if constexpr (!requires(Impl& s, const std::vector<Key>& v) {
                          s.insert(v.begin(), v.end());
                      }) {
            GTEST_SKIP() << "insert(first, last) not implemented";
        } else {
            f_set.insert(values.begin(), values.end());
            s_set.insert(values.begin(), values.end());
            check_consistency(f_set, s_set);

            std::vector<Key> more_values = {key(4), key(0), key(4)};
            f_set.insert(more_values.begin(), more_values.end());
            s_set.insert(more_values.begin(), more_values.end());
            check_consistency(f_set, s_set);
        }
    }

    // erase(const Key& k) -> size_type
    void test_erase_by_key() {
        if constexpr (!requires(Impl& s, const Key& k) {
                          { s.erase(k) } -> std::same_as<std::size_t>;
                      }) {
            GTEST_SKIP() << "erase(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Insert test data
                f_set.insert(key(1));
                s_set.insert(key(1));
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));
                check_consistency(f_set, s_set);

                // Erase existing key
                size_t erased1 = f_set.erase(key(2));
                size_t erased1_std = s_set.erase(key(2));
                ASSERT_EQ(erased1, erased1_std);
                ASSERT_EQ(erased1, 1);
                check_consistency(f_set, s_set);

                // Try erasing non-existent key
                size_t erased2 = f_set.erase(key(99));
                size_t erased2_std = s_set.erase(key(99));
                ASSERT_EQ(erased2, erased2_std);
                ASSERT_EQ(erased2, 0);
                check_consistency(f_set, s_set);

                // Erase remaining elements
                f_set.erase(key(1));
                s_set.erase(key(1));
                f_set.erase(key(3));
                s_set.erase(key(3));
                check_consistency(f_set, s_set);
                if constexpr (requires { f_set.empty(); }) {
                    ASSERT_TRUE(f_set.empty());
                }
            }
        }
    }

    // erase(iterator pos) -> iterator
    void test_erase_by_iterator() {
        if constexpr (!requires(Impl& s) {
                          { s.erase(s.begin()) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "erase(iterator) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Insert test data
                f_set.insert(key(1));
                s_set.insert(key(1));
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));

                // Erase first element
                auto it_f = f_set.begin();
                auto it_s = s_set.begin();
                f_set.erase(it_f);
                s_set.erase(it_s);
                check_consistency(f_set, s_set);

                // Erase middle element
                if constexpr (requires(Impl& s, const Key& k) { s.find(k); }) {
                    auto it2_f = f_set.find(key(2));
                    auto it2_s = s_set.find(key(2));
                    if (it2_f != f_set.end()) {
                        f_set.erase(it2_f);
                        s_set.erase(it2_s);
                        check_consistency(f_set, s_set);
                    }
                }
            }
        }
    }

    // clear() -> void
    void test_clear() {
        if constexpr (!requires(Impl& s) {
                          { s.clear() } -> std::same_as<void>;
                      }) {
            GTEST_SKIP() << "clear() not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Insert some elements
                f_set.insert(key(1));
                s_set.insert(key(1));
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));

                // Clear
                f_set.clear();
                s_set.clear();
                check_consistency(f_set, s_set);

                if constexpr (requires(const Impl& s) {
                                  { s.empty() } -> std::same_as<bool>;
                              }) {
                    ASSERT_TRUE(f_set.empty());
                }
                if constexpr (requires(const Impl& s) {
                                  { s.size() } -> std::same_as<std::size_t>;
                              }) {
                    ASSERT_EQ(f_set.size(), 0);
                }

                // Insert after clearing
                f_set.insert(key(10));
                s_set.insert(key(10));
                check_consistency(f_set, s_set);
            }
        }
    }

    // contains(const Key& k) const -> bool
    void test_contains() {
        if constexpr (!requires(const Impl& s, const Key& k) {
                          { s.contains(k) } -> std::same_as<bool>;
                      }) {
            GTEST_SKIP() << "contains(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Find in empty set
                ASSERT_FALSE(f_set.contains(key(1)));

                // Find after insertion
                f_set.insert(key(1));
                s_set.insert(key(1));
                ASSERT_TRUE(f_set.contains(key(1)));
                ASSERT_FALSE(f_set.contains(key(2)));

                // Insert more elements
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));

                ASSERT_TRUE(f_set.contains(key(1)));
                ASSERT_TRUE(f_set.contains(key(2)));
                ASSERT_TRUE(f_set.contains(key(3)));
                ASSERT_FALSE(f_set.contains(key(4)));
            }
        }
    }

    // find(const Key& k) -> iterator
    void test_find() {
        if constexpr (!requires(Impl& s, const Key& k) {
                          { s.find(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "find(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Find in empty set
                auto it1 = f_set.find(key(1));
                ASSERT_EQ(it1, f_set.end());

                // Find after insertion
                f_set.insert(key(1));
                s_set.insert(key(1));
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));

                // Find existing key
                auto it2 = f_set.find(key(2));
                ASSERT_NE(it2, f_set.end());
                if constexpr (requires { *it2; }) {
                    ASSERT_EQ(*it2, key(2));
                }

                // Find non-existent key
                auto it3 = f_set.find(key(99));
                ASSERT_EQ(it3, f_set.end());
            }
        }
    }

    // lower_bound(const Key& k) -> iterator
    void test_lower_bound() {
        if constexpr (!requires(Impl& s, const Key& k) {
                          { s.lower_bound(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "lower_bound(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Insert non-consecutive keys
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(4));
                s_set.insert(key(4));
                f_set.insert(key(6));
                s_set.insert(key(6));
                f_set.insert(key(8));
                s_set.insert(key(8));

                // Find existing key
                auto it1 = f_set.lower_bound(key(4));
                auto it1_std = s_set.lower_bound(key(4));
                ASSERT_NE(it1, f_set.end());
                if constexpr (requires(typename Impl::iterator it) { *it; }) {
                    ASSERT_EQ(*it1, *it1_std);
                    ASSERT_EQ(*it1, key(4));
                }

                // Find non-existent key (should return next greater key)
                auto it2 = f_set.lower_bound(key(3));
                auto it2_std = s_set.lower_bound(key(3));
                ASSERT_NE(it2, f_set.end());
                if constexpr (requires(typename Impl::iterator it) { *it; }) {
                    ASSERT_EQ(*it2, *it2_std);
                    ASSERT_EQ(*it2, key(4));
                }

                // Find value smaller than all keys
                auto it3 = f_set.lower_bound(key(1));
                auto it3_std = s_set.lower_bound(key(1));
                ASSERT_NE(it3, f_set.end());
                if constexpr (requires(typename Impl::iterator it) { *it; }) {
                    ASSERT_EQ(*it3, *it3_std);
                    ASSERT_EQ(*it3, key(2));
                }

                // Find value greater than all keys
                auto it4 = f_set.lower_bound(key(10));
                auto it4_std = s_set.lower_bound(key(10));
                ASSERT_EQ(it4, f_set.end());
                ASSERT_EQ(it4_std, s_set.end());
            }
        }
    }

    // upper_bound(const Key& k) -> iterator
    void test_upper_bound() {
        if constexpr (!requires(Impl& s, const Key& k) {
                          { s.upper_bound(k) } -> std::same_as<typename Impl::iterator>;
                      }) {
            GTEST_SKIP() << "upper_bound(const Key&) not implemented";
        } else {
            if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
                // Insert non-consecutive keys
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(4));
                s_set.insert(key(4));
                f_set.insert(key(6));
                s_set.insert(key(6));

                // Find existing key (should return next greater key)
                auto it1 = f_set.upper_bound(key(4));
                auto it1_std = s_set.upper_bound(key(4));
                ASSERT_NE(it1, f_set.end());
                if constexpr (requires(typename Impl::iterator it) { *it; }) {
                    ASSERT_EQ(*it1, *it1_std);
                    ASSERT_EQ(*it1, key(6));
                }

                // Find non-existent key
                auto it2 = f_set.upper_bound(key(3));
                auto it2_std = s_set.upper_bound(key(3));
                ASSERT_NE(it2, f_set.end());
                if constexpr (requires(typename Impl::iterator it) { *it; }) {
                    ASSERT_EQ(*it2, *it2_std);
                    ASSERT_EQ(*it2, key(4));
                }
            }
        }
    }

    // size() const -> size_type and empty() const -> bool
    void test_size_and_empty() {
        if constexpr (!requires(const Impl& s) {
                          { s.size() } -> std::same_as<std::size_t>;
                          { s.empty() } -> std::same_as<bool>;
                      }) {
            GTEST_SKIP() << "size() or empty() not implemented";
        } else {
            // Initial state
            ASSERT_TRUE(f_set.empty());
            ASSERT_EQ(f_set.size(), 0);

            if constexpr (requires { f_set.insert(Key{}); }) {
                // Insert one element
                f_set.insert(key(1));
                s_set.insert(key(1));
                ASSERT_FALSE(f_set.empty());
                ASSERT_EQ(f_set.size(), 1);

                // Insert more elements
                f_set.insert(key(2));
                s_set.insert(key(2));
                f_set.insert(key(3));
                s_set.insert(key(3));
                ASSERT_EQ(f_set.size(), 3);

                // Erase elements
                if constexpr (requires(Impl& s, const Key& k) { s.erase(k); }) {
                    f_set.erase(key(2));
                    s_set.erase(key(2));
                    ASSERT_EQ(f_set.size(), 2);

                    f_set.erase(key(1));
                    s_set.erase(key(1));
                    f_set.erase(key(3));
                    s_set.erase(key(3));
                    ASSERT_TRUE(f_set.empty());
                    ASSERT_EQ(f_set.size(), 0);
                }
            }
        }
    }

    // begin() -> iterator and end() -> iterator
    void test_iterator_traversal() {
        if constexpr (requires(Impl& s) { s.insert(std::declval<Key>()); }) {
            // Insert ordered elements
            f_set.insert(key(1));
            s_set.insert(key(1));
            f_set.insert(key(2));
            s_set.insert(key(2));
            f_set.insert(key(3));
            s_set.insert(key(3));
            f_set.insert(key(4));
            s_set.insert(key(4));

            // Traverse using iterator
            check_consistency(f_set, s_set);

            // Count elements
            int count = 0;
            for (auto it = f_set.begin(); it != f_set.end(); ++it) {
                count++;
            }
            if constexpr (requires(const Impl& s) {
                              { s.size() } -> std::same_as<std::size_t>;
                          }) {
                ASSERT_EQ(count, f_set.size());
            }
        }
    }

    // Edge cases
    void test_edge_cases() {
        if constexpr (!requires(Impl& s) { s.insert(std::declval<Key>()); }) {
            GTEST_SKIP() << "insert() not implemented, cannot test edge cases";
        } else {
            // Insert then immediately erase
            f_set.insert(key(1));
            s_set.insert(key(1));
            if constexpr (requires { f_set.erase(Key{}); }) {
                f_set.erase(key(1));
                s_set.erase(key(1));
                check_consistency(f_set, s_set);
            }

            // Insert same key multiple times
            f_set.insert(key(2));
            s_set.insert(key(2));
            f_set.insert(key(2));
            s_set.insert(key(2));
            f_set.insert(key(2));
            s_set.insert(key(2));
            check_consistency(f_set, s_set);

            // Use after clearing
            if constexpr (requires(Impl& s) {
                              { s.clear() } -> std::same_as<void>;
                          }) {
                f_set.clear();
                s_set.clear();
                f_set.insert(key(5));
                s_set.insert(key(5));
                check_consistency(f_set, s_set);
            }
        }
    }

    void fuzz_test(int iterations) {
        std::mt19937 gen(42);

        for (int i = 0; i < iterations; ++i) {
            SetOperation op = static_cast<SetOperation>(
                std::uniform_int_distribution<int>(0, static_cast<int>(SetOperation::_Size) - 1)(
                    gen
                )
            );
            Key k = key(i);

            switch (op) {
            case SetOperation::Insert:
                if constexpr (requires { f_set.insert(k); }) {
                    if (!s_set.count(k)) {
                        f_set.insert(k);
                        s_set.insert(k);
                    }
                }
                break;

            case SetOperation::Emplace:
                if constexpr (requires { f_set.emplace(k); }) {
                    if (!s_set.count(k)) {
                        f_set.emplace(k);
                        s_set.emplace(k);
                    }
                }
                break;

            case SetOperation::EraseByKey:
                if constexpr (requires { f_set.erase(k); }) {
                    if (s_set.count(k)) {
                        f_set.erase(k);
                        s_set.erase(k);
                    }
                }
                break;

            case SetOperation::EraseByIterator:
                if constexpr (requires { f_set.erase(f_set.begin()); }) {
                    if (s_set.contains(k)) {
                        auto it_f = f_set.find(k);
                        auto it_s = s_set.find(k);
                        ASSERT_NE(it_f, f_set.end());
                        ASSERT_NE(it_s, s_set.end());

                        f_set.erase(it_f);
                        s_set.erase(it_s);
                    }
                }
                break;

            case SetOperation::Clear:
                if constexpr (requires { f_set.clear(); }) {
                    if (std::uniform_int_distribution<int>(0, 50)(gen) == 0) {
                        f_set.clear();
                        s_set.clear();
                    }
                }
                break;

            case SetOperation::Contains:
                if constexpr (requires { f_set.contains(k); }) {
                    ASSERT_EQ(f_set.contains(k), s_set.contains(k));
                }
                break;

            case SetOperation::Find:
                if constexpr (requires { f_set.find(k); }) {
                    auto it_f = f_set.find(k);
                    auto it_s = s_set.find(k);
                    bool found_f = (it_f != f_set.end());
                    bool found_s = (it_s != s_set.end());
                    ASSERT_EQ(found_f, found_s);
                    if (found_f) {
                        ASSERT_EQ(*it_f, *it_s);
                    }
                }
                break;

            case SetOperation::LowerBound:
                if constexpr (requires { f_set.lower_bound(k); }) {
                    auto it_f = f_set.lower_bound(k);
                    auto it_s = s_set.lower_bound(k);

                    bool end_f = (it_f == f_set.end());
                    bool end_s = (it_s == s_set.end());
                    ASSERT_EQ(end_f, end_s);
                    if (!end_f) {
                        ASSERT_EQ(*it_f, *it_s);
                    }
                }
                break;

            case SetOperation::UpperBound:
                if constexpr (requires { f_set.upper_bound(k); }) {
                    auto it_f = f_set.upper_bound(k);
                    auto it_s = s_set.upper_bound(k);

                    bool end_f = (it_f == f_set.end());
                    bool end_s = (it_s == s_set.end());
                    ASSERT_EQ(end_f, end_s);
                    if (!end_f) {
                        ASSERT_EQ(*it_f, *it_s);
                    }
                }
                break;

            case SetOperation::_Size:
                assert(false);
                break;
            }

            check_consistency(f_set, s_set);
        }
    }
};

template <typename T>
class ComparableUniquePtr : public std::unique_ptr<T> {
public:
    using std::unique_ptr<T>::unique_ptr;
    using std::unique_ptr<T>::operator=;
    auto operator<=>(const ComparableUniquePtr& other) const -> std::strong_ordering {
        return *this->get() <=> *other.get();
    }
    auto operator==(const ComparableUniquePtr& other) const -> bool {
        return *this->get() == *other.get();
    }
};

using SetTestTypes = ::testing::Types<
    GlobalMemory::FlatSet<int>,
    GlobalMemory::FlatSet<std::string>/*,
    GlobalMemory::FlatSet<ComparableUniquePtr<int>>*/>;

TYPED_TEST_SUITE(FlatSetTest, SetTestTypes);

TYPED_TEST(FlatSetTest, InsertBasic) { this->test_insert_basic(); }
TYPED_TEST(FlatSetTest, Emplace) { this->test_emplace(); }
TYPED_TEST(FlatSetTest, InsertRange) { this->test_insert_range(); }
TYPED_TEST(FlatSetTest, EraseByKey) { this->test_erase_by_key(); }
TYPED_TEST(FlatSetTest, EraseByIterator) { this->test_erase_by_iterator(); }
TYPED_TEST(FlatSetTest, Clear) { this->test_clear(); }
TYPED_TEST(FlatSetTest, Contains) { this->test_contains(); }
TYPED_TEST(FlatSetTest, Find) { this->test_find(); }
TYPED_TEST(FlatSetTest, LowerBound) { this->test_lower_bound(); }
TYPED_TEST(FlatSetTest, UpperBound) { this->test_upper_bound(); }
TYPED_TEST(FlatSetTest, SizeAndEmpty) { this->test_size_and_empty(); }
TYPED_TEST(FlatSetTest, IteratorTraversal) { this->test_iterator_traversal(); }
TYPED_TEST(FlatSetTest, EdgeCases) { this->test_edge_cases(); }

TYPED_TEST(FlatSetTest, FuzzyTest) { this->fuzz_test(10000); }
