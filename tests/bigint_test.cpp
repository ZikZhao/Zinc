#include "pch.hpp"
#include "gtest/gtest.h"

TEST(BigIntShiftTest, LeftShiftByBigInt) {
    const BigInt value(3ul);
    const BigInt shift(5ul);
    EXPECT_EQ((value << shift).to_string(), "96");
}

TEST(BigIntShiftTest, RightShiftByBigInt) {
    const BigInt value(96ul);
    const BigInt shift(5ul);
    EXPECT_EQ((value >> shift).to_string(), "3");
}

TEST(BigIntShiftTest, NegativeRightShiftByBigIntUsesArithmeticShift) {
    const BigInt value(static_cast<std::int64_t>(-3));
    const BigInt shift(1ul);
    EXPECT_EQ((value >> shift).to_string(), "-2");
}

TEST(BigIntShiftTest, NegativeShiftCountThrows) {
    const BigInt value(1ul);
    const BigInt negative_shift(static_cast<std::int64_t>(-1));
    EXPECT_THROW({ (void)(value << negative_shift); }, std::domain_error);
    EXPECT_THROW({ (void)(value >> negative_shift); }, std::domain_error);
}
