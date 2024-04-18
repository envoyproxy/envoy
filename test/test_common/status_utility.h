#pragma once

#include "absl/status/statusor.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace StatusHelpers {

// Check that a StatusOr is OK and has a value equal to or matching its argument.
//
// For example:
//
// StatusOr<int> status(3);
// EXPECT_THAT(status, IsOkAndHolds(3));
// EXPECT_THAT(status, IsOkAndHolds(Gt(2)));
MATCHER_P(IsOkAndHolds, expected, "") {
  if (!arg.ok()) {
    *result_listener << "which has unexpected status: " << arg.status();
    return false;
  }
  if (!::testing::Matches(expected)(*arg)) {
    *result_listener << "which has wrong value: " << ::testing::PrintToString(*arg);
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

// Check that a Status code equal to its argument.
//
// For example:
//
// Status<int> status(absl::InvalidArgumentError("bad argument!"));
// EXPECT_THAT(status, StatusCodeIs(absl::StatusCode::kInvalidArgument));
MATCHER_P(StatusCodeIs, expected_code, "") {
  if (arg.code() != expected_code) {
    *result_listener << "which has unexpected status: " << arg.code();
    return false;
  }
  return true;
}

// A polymorphic matcher class for matching absl::Status or absl::StatusOr.
// Not intended for direct use, see HasStatus, HasStatusCode, HasStatusMessage and IsOk
// below.
class StatusMatcher {
public:
  StatusMatcher(::testing::Matcher<absl::Status> matcher) : matcher_(matcher) {}

  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(absl::Status status, ::testing::MatchResultListener* listener) const {
    return matcher_.MatchAndExplain(status, listener);
  }

  template <typename T>
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool MatchAndExplain(const absl::StatusOr<T>& status_or,
                       ::testing::MatchResultListener* listener) const {
    return ::testing::ExplainMatchResult(
        ::testing::Property("status", &absl::StatusOr<T>::status, matcher_), status_or, listener);
  }
  // NOLINTNEXTLINE(readability-identifier-naming)
  void DescribeTo(::std::ostream* os) const { matcher_.DescribeTo(os); }
  // NOLINTNEXTLINE(readability-identifier-naming)
  void DescribeNegationTo(::std::ostream* os) const {
    *os << "not ";
    matcher_.DescribeTo(os);
  }

private:
  ::testing::Matcher<absl::Status> matcher_;
};

// Match status code in an absl::StatusOr or absl::Status, allowing arbitrary matchers,
// e.g.
// EXPECT_THAT(some_status_or, HasStatusCode(AnyOf(absl::StatusCode::kOk,
//                                                 absl::StatusCode::kInvalidArgument)));
template <typename InnerMatcher>
// NOLINTNEXTLINE(readability-identifier-naming)
::testing::PolymorphicMatcher<StatusMatcher> HasStatusCode(InnerMatcher m) {
  return ::testing::MakePolymorphicMatcher(StatusMatcher(::testing::SafeMatcherCast<absl::Status>(
      ::testing::Property("code", &absl::Status::code, m))));
}

// Match status message in an absl::StatusOr or absl::Status, allowing arbitrary matchers,
// e.g.
// EXPECT_THAT(some_status_or, HasStatusMessage(HasSubstr("cheese")));
template <typename InnerMatcher>
// NOLINTNEXTLINE(readability-identifier-naming)
::testing::PolymorphicMatcher<StatusMatcher> HasStatusMessage(InnerMatcher m) {
  return ::testing::MakePolymorphicMatcher(StatusMatcher(::testing::SafeMatcherCast<absl::Status>(
      ::testing::Property("message", &absl::Status::message, m))));
}

// Match the status of an absl::StatusOr or absl::Status, e.g.
// EXPECT_THAT(some_status_or, HasStatus(absl::InvalidArgumentError("oh no")));
// One may also use a Status matcher, but see the other HasStatus below for a
// more readable version of that.
template <typename InnerMatcher>
// NOLINTNEXTLINE(readability-identifier-naming)
::testing::PolymorphicMatcher<StatusMatcher> HasStatus(InnerMatcher m) {
  return ::testing::MakePolymorphicMatcher(
      StatusMatcher(::testing::SafeMatcherCast<absl::Status>(m)));
}

// Match the code and message of an absl::StatusOr or absl::Status, e.g.
// EXPECT_THAT(some_status_or, HasStatus(absl::StatusCode::kInvalidArgument, HasSubstr("cheese")));
template <typename InnerMatcherCode, typename InnerMatcherMessage>
// NOLINTNEXTLINE(readability-identifier-naming)
::testing::PolymorphicMatcher<StatusMatcher> HasStatus(InnerMatcherCode code_matcher,
                                                       InnerMatcherMessage message_matcher) {
  return ::testing::MakePolymorphicMatcher(StatusMatcher(::testing::SafeMatcherCast<absl::Status>(
      AllOf(::testing::Property("code", &absl::Status::code, code_matcher),
            ::testing::Property("message", &absl::Status::message, message_matcher)))));
}

// Check that an absl::Status or absl::StatusOr is OK.
//
// For example:
//
// StatusOr<int> status_or(absl::InvalidArgumentError("bad argument!"));
// EXPECT_THAT(status_or, IsOk());  // fails!
//
// NOLINTNEXTLINE(readability-identifier-naming)
inline ::testing::PolymorphicMatcher<StatusMatcher> IsOk() {
  return HasStatusCode(absl::StatusCode::kOk);
}

#ifndef EXPECT_OK
// Fails if an absl::Status or absl::StatusOr is not OK.
//
// For example:
//
// StatusOr<int> status_or(absl::InvalidArgumentError("bad argument!"));
// EXPECT_OK(status_or);  // fails!
// absl::Status status{absl::OkStatus()};
// EXPECT_OK(status);  // passes!
#define EXPECT_OK(v) EXPECT_THAT((v), ::Envoy::StatusHelpers::IsOk())
#endif // EXPECT_OK

#ifndef ASSERT_OK
// Asserts if an absl::Status or absl::StatusOr is not OK.
//
// For example:
//
// StatusOr<int> status_or(absl::InvalidArgumentError("bad argument!"));
// ASSERT_OK(status_or);  // asserts!
// absl::Status status{absl::OkStatus()};
// ASSERT_OK(status);  // passes!
#define ASSERT_OK(v) ASSERT_THAT((v), ::Envoy::StatusHelpers::IsOk())
#endif // ASSERT_OK

} // namespace StatusHelpers
} // namespace Envoy
