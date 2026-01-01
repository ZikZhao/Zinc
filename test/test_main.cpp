#include "test_map.hpp"

using TestTypes = ::testing::Types<
    FlatMap<int, int>,
    FlatMap<std::string, std::string>,
    FlatMap<int, ComparableUniquePtr<int>>,
    FlatMap<std::string, int>>;

TYPED_TEST_SUITE(FlatMapTest, TestTypes);

TYPED_TEST(FlatMapTest, FuzzyTest) { this->fuzz_test(10000); }

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

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
