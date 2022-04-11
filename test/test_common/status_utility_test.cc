#include "test/test_common/status_utility.h"

namespace Envoy {
namespace StatusHelpers {

TEST(StatusUtilityTest, ExpectOkForStatus) {
  absl::Status status;
  EXPECT_OK(status);
}

TEST(StatusUtilityTest, ExpectOkForStatusOr) {
  absl::StatusOr<int> status_or = 5;
  EXPECT_OK(status_or);
}

TEST(StatusUtilityTest, IsOkFailureForStatus) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(IsOk(), absl::FailedPreconditionError("whatever"), &listener);
  EXPECT_EQ("status is FAILED_PRECONDITION: whatever", listener.str());
}

TEST(StatusUtilityTest, IsOkFailureForStatusOr) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(
      IsOk(), absl::StatusOr<int>{absl::FailedPreconditionError("whatever")}, &listener);
  EXPECT_EQ("status is FAILED_PRECONDITION: whatever", listener.str());
}

TEST(StatusUtilityTest, IsOkAndHoldsSuccess) {
  absl::StatusOr<int> five = 5;
  EXPECT_THAT(five, IsOkAndHolds(5));
}

TEST(StatusUtilityTest, IsOkAndHoldsFailureByValue) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(IsOkAndHolds(5), absl::StatusOr<int>{6}, &listener);
  EXPECT_EQ("which has wrong value: 6", listener.str());
}

TEST(StatusUtilityTest, IsOkAndHoldsFailureByCode) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(
      IsOkAndHolds(5), absl::StatusOr<int>{absl::FailedPreconditionError("oh no")}, &listener);
  EXPECT_EQ("which has unexpected status: FAILED_PRECONDITION: oh no", listener.str());
}

} // namespace StatusHelpers
} // namespace Envoy
