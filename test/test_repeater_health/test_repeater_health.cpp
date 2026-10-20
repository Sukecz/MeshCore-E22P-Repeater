#include <gtest/gtest.h>
#include <helpers/RepeaterHealth.h>
#include <stdint.h>

TEST(RepeaterHealth, ReceiveErrorsUseAllReadAttempts) {
  EXPECT_EQ(13, RepeaterHealth::receiveErrorPercent(305, 47));
  EXPECT_EQ(25, RepeaterHealth::receiveErrorPercent(75, 25));
  EXPECT_EQ(0, RepeaterHealth::receiveErrorPercent(0, 0));
  EXPECT_EQ(100, RepeaterHealth::receiveErrorPercent(0, 1));
}

TEST(RepeaterHealth, LargeCountersDoNotOverflow) {
  EXPECT_EQ(50, RepeaterHealth::receiveErrorPercent(UINT32_MAX, UINT32_MAX));
  EXPECT_EQ(0, RepeaterHealth::receiveErrorPercent(UINT32_MAX, 1));
  EXPECT_EQ(99, RepeaterHealth::receiveErrorPercent(1, UINT32_MAX));
}

TEST(RepeaterHealth, OrdinaryReceiveErrorsDoNotWarn) {
  EXPECT_STREQ("good", RepeaterHealth::state(false, 0, 13, 29));
  EXPECT_STREQ("good", RepeaterHealth::state(false, 7, 29, 49));
}

TEST(RepeaterHealth, HighReceiveErrorsAreAdvisoryOnly) {
  EXPECT_STREQ("warn", RepeaterHealth::state(false, 0, 30, 0));
  EXPECT_STREQ("warn", RepeaterHealth::state(false, 0, 100, 0));
}

TEST(RepeaterHealth, DisabledForwardingRemainsBad) {
  EXPECT_STREQ("bad", RepeaterHealth::state(true, 0, 0, 0));
}

TEST(RepeaterHealth, QueueThresholdsRemainProtected) {
  EXPECT_STREQ("warn", RepeaterHealth::state(false, 8, 0, 0));
  EXPECT_STREQ("warn", RepeaterHealth::state(false, 15, 0, 0));
  EXPECT_STREQ("bad", RepeaterHealth::state(false, 16, 0, 0));
  EXPECT_STREQ("bad", RepeaterHealth::state(false, 32, 100, 100));
}

TEST(RepeaterHealth, DuplicateWarningRemainsProtected) {
  EXPECT_STREQ("warn", RepeaterHealth::state(false, 0, 0, 50));
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
