#include "pch.hpp"
#include <ostream>

#include "gtest/gtest.h"
#include "test_map.hpp"
#include "test_set.hpp"

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
    friend std::ostream& operator<<(std::ostream& os, const ComparableUniquePtr& cup) {
        os << *cup.ptr_;
        return os;
    }
};

using MapTestTypes = ::testing::Types<
    GlobalMemory::Map<int, int>,
    GlobalMemory::Map<std::string, std::string>,
    GlobalMemory::Map<int, ComparableUniquePtr<int>>,
    GlobalMemory::Map<std::string, int>>;

TYPED_TEST_SUITE(FlatMapTest, MapTestTypes);

TYPED_TEST(FlatMapTest, InsertBasic) { this->test_insert_basic(); }
TYPED_TEST(FlatMapTest, InsertOrAssign) { this->test_insert_or_assign(); }
TYPED_TEST(FlatMapTest, TryEmplace) { this->test_try_emplace(); }
TYPED_TEST(FlatMapTest, AccessOperator) { this->test_access_operator(); }
TYPED_TEST(FlatMapTest, AtMethod) { this->test_at_method(); }
TYPED_TEST(FlatMapTest, EraseByKey) { this->test_erase_by_key(); }
TYPED_TEST(FlatMapTest, EraseByIterator) { this->test_erase_by_iterator(); }
TYPED_TEST(FlatMapTest, Clear) { this->test_clear(); }
TYPED_TEST(FlatMapTest, Contains) { this->test_contains(); }
TYPED_TEST(FlatMapTest, Find) { this->test_find(); }
TYPED_TEST(FlatMapTest, LowerBound) { this->test_lower_bound(); }
TYPED_TEST(FlatMapTest, UpperBound) { this->test_upper_bound(); }
TYPED_TEST(FlatMapTest, SizeAndEmpty) { this->test_size_and_empty(); }
TYPED_TEST(FlatMapTest, IteratorTraversal) { this->test_iterator_traversal(); }
TYPED_TEST(FlatMapTest, EdgeCases) { this->test_edge_cases(); }

TYPED_TEST(FlatMapTest, FuzzyTest) { this->fuzz_test(10000); }

using SetTestTypes = ::testing::Types<
    GlobalMemory::Set<int>,
    GlobalMemory::Set<std::string>,
    GlobalMemory::Set<ComparableUniquePtr<int>>>;

TYPED_TEST_SUITE(FlatSetTest, SetTestTypes);

TYPED_TEST(FlatSetTest, InsertBasic) { this->test_insert_basic(); }
TYPED_TEST(FlatSetTest, Emplace) { this->test_emplace(); }
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

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
