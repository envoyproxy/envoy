#include <sstream>

#include "test/test_common/status_utility.h"

namespace Envoy {
namespace StatusHelpers {

using ::testing::HasSubstr;

template <typename T, typename MatcherT>
std::string expectThatOutput(const T& value, MatcherT matcher) {
  auto m = ::testing::SafeMatcherCast<T>(matcher);
  if (m.Matches(value)) {
    return "";
  }
  ::std::stringstream ss;
  ss << "Value of: [variable_name]" << std::endl;
  ss << "Expected: ";
  m.DescribeTo(&ss);
  ss << std::endl << "Actual: " << ::testing::PrintToString(value);
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(matcher, value, &listener);
  ss << std::endl << listener.str();
  return ss.str();
}

TEST(StatusUtilityTest, ExpectOkForStatus) { EXPECT_OK(absl::OkStatus()); }

TEST(StatusUtilityTest, ExpectOkForStatusOr) { EXPECT_OK(absl::StatusOr<int>{5}); }

TEST(StatusUtilityTest, IsOkFailureForStatus) {
  auto err = expectThatOutput(absl::FailedPreconditionError("whatever"), IsOk());
  EXPECT_THAT(err, AllOf(HasSubstr("Expected: is an object whose property `code` is equal to OK"),
                         HasSubstr("Actual: FAILED_PRECONDITION: whatever"),
                         HasSubstr("whose property `code` is FAILED_PRECONDITION")));
}

TEST(StatusUtilityTest, IsOkFailureForStatusOr) {
  auto err =
      expectThatOutput(absl::StatusOr<int>{absl::FailedPreconditionError("whatever")}, IsOk());
  EXPECT_THAT(err, AllOf(HasSubstr("whose property `status` is FAILED_PRECONDITION: whatever"),
                         HasSubstr("whose property `code` is FAILED_PRECONDITION")));
}

TEST(StatusUtilityTest, HasStatusCodeSuccessForStatus) {
  EXPECT_THAT(absl::FailedPreconditionError("whatever"),
              HasStatusCode(absl::StatusCode::kFailedPrecondition));
}

TEST(StatusUtilityTest, HasStatusMessageSuccessForStatus) {
  EXPECT_THAT(absl::FailedPreconditionError("whatever"), HasStatusMessage(HasSubstr("whatever")));
}

TEST(StatusUtilityTest, HasStatusCodeFailureForStatusOr) {
  auto err = expectThatOutput(absl::StatusOr<int>{5},
                              HasStatusCode(absl::StatusCode::kFailedPrecondition));
  EXPECT_THAT(
      err,
      AllOf(
          HasSubstr("Expected: is an object whose property `code` is equal to FAILED_PRECONDITION"),
          HasSubstr("whose property `status` is OK")));
}

TEST(StatusUtilityTest, HasStatusMessageFailureForStatus) {
  auto err = expectThatOutput(absl::InvalidArgumentError("oh no"), HasStatusMessage("whatever"));
  EXPECT_THAT(err, HasSubstr("whose property `message` is \"oh no\""));
}

TEST(StatusUtilityTest, HasStatusFailureForStatusOr) {
  auto err = expectThatOutput(absl::StatusOr<int>{absl::InvalidArgumentError("oh no")},
                              HasStatus(absl::OkStatus()));
  EXPECT_THAT(err, AllOf(HasSubstr("Expected: is equal to OK"),
                         HasSubstr("whose property `status` is INVALID_ARGUMENT: oh no")));
}

TEST(StatusUtilityTest, HasStatusFailureForStatus) {
  auto err = expectThatOutput(absl::InvalidArgumentError("oh no"), HasStatus(absl::OkStatus()));
  EXPECT_THAT(err, AllOf(HasSubstr("Expected: is equal to OK"),
                         HasSubstr("Actual: INVALID_ARGUMENT: oh no")));
}

TEST(StatusUtilityTest, HasStatusSuccessForStatusOr) {
  auto status = absl::InvalidArgumentError("oh no");
  EXPECT_THAT(absl::StatusOr<int>{status}, HasStatus(status));
}

TEST(StatusUtilityTest, HasStatusSuccessForStatus) {
  auto status = absl::InvalidArgumentError("oh no");
  EXPECT_THAT(status, HasStatus(status));
}

TEST(StatusUtilityTest, HasStatusTwoParamsFailureForStatusOr) {
  auto err = expectThatOutput(absl::StatusOr<int>{absl::InvalidArgumentError("oh no")},
                              HasStatus(absl::StatusCode::kOk, HasSubstr("whatever")));
  EXPECT_THAT(
      err,
      AllOf(HasSubstr("Expected: (is an object whose property `code` is equal to OK) and (is an "
                      "object whose property `message` has substring \"whatever\")"),
            HasSubstr("whose property `status` is INVALID_ARGUMENT: oh no")));
}

TEST(StatusUtilityTest, HasStatusTwoParamsFailureForStatus) {
  auto err = expectThatOutput(absl::InvalidArgumentError("oh no"),
                              HasStatus(absl::StatusCode::kOk, HasSubstr("whatever")));
  EXPECT_THAT(
      err,
      AllOf(HasSubstr("Expected: (is an object whose property `code` is equal to OK) and (is an "
                      "object whose property `message` has substring \"whatever\")"),
            HasSubstr("Actual: INVALID_ARGUMENT: oh no"),
            HasSubstr("whose property `code` is INVALID_ARGUMENT")));
}

TEST(StatusUtilityTest, HasStatusTwoParamsSuccessForStatusOr) {
  auto err = absl::InvalidArgumentError("oh no");
  auto status_or = absl::StatusOr<int>{err};
  EXPECT_THAT(status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("oh no")));
}

TEST(StatusUtilityTest, HasStatusTwoParamsSuccessForStatus) {
  auto err = absl::InvalidArgumentError("oh no");
  EXPECT_THAT(err, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("oh no")));
}

TEST(StatusUtilityTest, IsOkAndHoldsSuccess) {
  absl::StatusOr<int> five = 5;
  EXPECT_THAT(five, IsOkAndHolds(5));
}

TEST(StatusUtilityTest, IsOkAndHoldsFailureByValue) {
  ::testing::StringMatchResultListener listener;
  EXPECT_FALSE(::testing::ExplainMatchResult(IsOkAndHolds(5), absl::StatusOr<int>{6}, &listener));
  EXPECT_EQ("which has wrong value: 6", listener.str());
}

TEST(StatusUtilityTest, IsOkAndHoldsFailureByValueMatcher) {
  ::testing::StringMatchResultListener listener;
  EXPECT_FALSE(::testing::ExplainMatchResult(IsOkAndHolds(::testing::Lt(4)), absl::StatusOr<int>{6},
                                             &listener));
  EXPECT_EQ("which has wrong value: 6", listener.str());
}

TEST(StatusUtilityTest, IsOkAndHoldsFailureByCode) {
  ::testing::StringMatchResultListener listener;
  ::testing::ExplainMatchResult(
      IsOkAndHolds(5), absl::StatusOr<int>{absl::FailedPreconditionError("oh no")}, &listener);
  EXPECT_THAT(listener.str(), HasSubstr("which has unexpected status: FAILED_PRECONDITION: oh no"));
}

} // namespace StatusHelpers
} // namespace Envoy
