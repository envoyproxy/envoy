#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/packed_struct.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
#define REDIRECT_STRING_ELEMENTS(HELPER)                                                           \
  HELPER(scheme_redirect)                                                                          \
  HELPER(host_redirect)                                                                            \
  HELPER(path_redirect)

MAKE_PACKED_STRING_STRUCT(RedirectStringsFromProto, REDIRECT_STRING_ELEMENTS);
MAKE_PACKED_STRUCT(RedirectStrings, REDIRECT_STRING_ELEMENTS);

#define TIMEOUT_ELEMENTS(HELPER)                                                                   \
  HELPER(idle_timeout)                                                                             \
  HELPER(max_grpc_timeout)                                                                         \
  HELPER(grpc_timeout_offset)
MAKE_PACKED_MS_STRUCT(TimeoutsFromProto, TIMEOUT_ELEMENTS);
MAKE_PACKED_OPTIONAL_STRUCT(Timeouts, TIMEOUT_ELEMENTS);

namespace {

TEST(PackedStruct, StringFromProto) {
  envoy::config::route::v3::RedirectAction redirect_action;
  TestUtility::loadFromYaml(std::string(R"EOF(
  scheme_redirect: abc
  host_redirect: def
  )EOF"),
                            redirect_action);

  RedirectStringsFromProto<std::string, envoy::config::route::v3::RedirectAction> redirect_strings(
      redirect_action);

  EXPECT_EQ(redirect_strings.scheme_redirect(), "abc");
  EXPECT_EQ(redirect_strings.host_redirect(), "def");
  EXPECT_FALSE(redirect_strings.has_path_redirect());
}

TEST(PackedStruct, StringStruct) {
  RedirectStrings<std::string> redirect_strings(2);
  redirect_strings.set_scheme_redirect("abc");
  redirect_strings.set_path_redirect("def");

  EXPECT_EQ(redirect_strings.scheme_redirect(), "abc");
  EXPECT_EQ(redirect_strings.path_redirect(), "def");
  EXPECT_FALSE(redirect_strings.has_host_redirect());
}

TEST(PackedStruct, DurationMSFromProto) {
  envoy::config::route::v3::RouteAction route;
  TestUtility::loadFromYaml(std::string(R"EOF(
  idle_timeout:
    seconds: 0
    nanos: 20000000
  max_grpc_timeout:
    seconds: 0
    nanos: 5000000
  )EOF"),
                            route);
  TimeoutsFromProto<std::chrono::milliseconds, envoy::config::route::v3::RouteAction> timeouts(
      route);
  EXPECT_EQ(timeouts.idle_timeout().value().count(), 20);
  EXPECT_EQ(timeouts.max_grpc_timeout().value().count(), 5);
  EXPECT_FALSE(timeouts.grpc_timeout_offset().has_value());
}

TEST(PackedStruct, DurationMS) {
  Timeouts<std::chrono::milliseconds> timeouts(2);
  timeouts.set_idle_timeout(std::chrono::milliseconds(20));
  timeouts.set_max_grpc_timeout(std::chrono::milliseconds(5));

  EXPECT_EQ(timeouts.idle_timeout().value().count(), 20);
  EXPECT_EQ(timeouts.max_grpc_timeout().value().count(), 5);
  EXPECT_FALSE(timeouts.grpc_timeout_offset().has_value());
}
} // namespace
} // namespace Envoy
