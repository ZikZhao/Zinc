#include "gtest/gtest.h"

auto main(int argc, char** argv) -> int {
    std::vector<int> test_vector = {1, 2, 3, 4, 5};
    test_vector.push_back(6);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
