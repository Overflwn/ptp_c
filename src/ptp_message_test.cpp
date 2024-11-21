#include <gtest/gtest.h>

TEST(PtpMessageTest, BasicAssertions) {
    EXPECT_STRNE("hello", "world");
    EXPECT_EQ(7*6, 42);
}
