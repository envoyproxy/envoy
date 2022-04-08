#pragma once

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace StatusHelpers {

// Check that a StatusOr is OK and has a value equal to its argument.
//
// For example:
//
// StatusOr<int> status(3);
// EXPECT_THAT(status, IsOkAndHolds(3));
MATCHER_P(IsOkAndHolds, expected, "") {
  if (!arg.ok()) {
    *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  if (*arg != expected) {
    *result_listener << "which has wrong value: " << *arg;
    return false;
  }
  return true;
}

// Check that a StatusOr as a status code equal to its argument.
//
// For example:
//
// StatusOr<int> status(absl::InvalidArgumentError("bad argument!"));
// EXPECT_THAT(status, StatusIs(absl::StatusCode::kInvalidArgument));
MATCHER_P(StatusIs, expected_code, "") {
  if (arg.status().code() != expected_code) {
    *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  return true;
}

class IsOkMatcher {
public:
  template <typename StatusT>
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(StatusT status, ::testing::MatchResultListener* listener) const {
    if (status.ok()) {
      return true;
    }
    *listener << "status is " << status;
    return false;
  }

  template <typename T>
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(absl::StatusOr<T> statusor, ::testing::MatchResultListener* listener) const {
    return MatchAndExplain(statusor.status(), listener);
  }
  // NOLINTNEXTLINE(readability-identifier-naming)
  void DescribeTo(::std::ostream* os) const { *os << "is OK"; }
  // NOLINTNEXTLINE(readability-identifier-naming)
  void DescribeNegationTo(::std::ostream* os) const { *os << "is not OK"; }
};

// NOLINTNEXTLINE(readability-identifier-naming)
inline ::testing::PolymorphicMatcher<IsOkMatcher> IsOk() {
  return ::testing::MakePolymorphicMatcher(IsOkMatcher());
}

#ifndef EXPECT_OK
#define EXPECT_OK(v) EXPECT_THAT((v), ::Envoy::StatusHelpers::IsOk())
#endif // EXPECT_OK

} // namespace StatusHelpers
} // namespace Envoy
