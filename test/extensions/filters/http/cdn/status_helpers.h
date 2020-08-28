#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cdn {
namespace StatusHelpers {

MATCHER_P(IsOkAndHolds, expected, "") {
  if (!arg) {
    *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  if (*arg != expected) {
    *result_listener << "which has wrong value: " << *arg;
    return false;
  }
  return true;
}

MATCHER_P(StatusIs, expected_code, "") {
  if (arg.status().code() != expected_code) {
    *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  return true;
}

} // namespace StatusHelpers
} // namespace Cdn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
