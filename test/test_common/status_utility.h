#pragma once

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

} // namespace StatusHelpers
} // namespace Envoy
