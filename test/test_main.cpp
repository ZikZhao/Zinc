#include "pch.hpp"
#include <benchmark/benchmark.h>
#include <gtest/gtest.h>

class FlatMapTest : public ::testing::Test {
protected:
    FlatMap<std::string, int> map;
};

TEST_F(FlatMapTest, InsertMaintainsOrder) {
    map.insert("b", 20);
    map.insert("a", 10);
    map.insert("c", 30);

    EXPECT_EQ(map.size(), 3);

    auto it = map.begin();
    EXPECT_EQ(it->first, "a");
    EXPECT_EQ(it->second, 10);

    it++;
    EXPECT_EQ(it->first, "b");

    it++;
    EXPECT_EQ(it->first, "c");
}

TEST_F(FlatMapTest, InsertDuplicateKey) {
    map.insert("key", 100);
    map.insert("key", 200);

    EXPECT_EQ(map.at("key"), 200);
}

TEST_F(FlatMapTest, FindExistingAndNonExisting) {
    map.insert("exist", 1);

    auto it = map.find("exist");
    EXPECT_NE(it, map.end());
    EXPECT_EQ(it->second, 1);

    auto it_none = map.find("404");
    EXPECT_EQ(it_none, map.end());
}

TEST(FlatMapComparison, MatchesStdMapBehavior) {
    FlatMap<int, int> my_map;
    std::map<int, int> std_map;

    std::vector<int> random_inputs = {5, 1, 9, 3, 7, 1, 5};  // 包含乱序和重复

    for (int x : random_inputs) {
        if (!my_map.contains(x)) my_map.insert(x, x * 10);
        if (std_map.find(x) == std_map.end()) std_map.insert({x, x * 10});
    }

    EXPECT_EQ(my_map.size(), std_map.size());

    auto my_it = my_map.begin();
    auto std_it = std_map.begin();

    while (my_it != my_map.end()) {
        EXPECT_EQ(my_it->first, std_it->first);
        EXPECT_EQ(my_it->second, std_it->second);
        ++my_it;
        ++std_it;
    }
}
