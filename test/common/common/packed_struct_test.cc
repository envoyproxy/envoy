#include "envoy/config/route/v3/route_components.pb.h"

#include "source/common/common/packed_struct.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
#define REDIRECT_STRING_ELEMENTS(HELPER)                                                           \
  HELPER(scheme_redirect)                                                                          \
  HELPER(host_redirect)                                                                            \
  HELPER(path_redirect)

#define TIMEOUT_ELEMENTS(HELPER)                                                                   \
  HELPER(idle_timeout)                                                                             \
  HELPER(max_grpc_timeout)                                                                         \
  HELPER(grpc_timeout_offset)
namespace {

MAKE_PACKED_STRING_STRUCT(RedirectStringsFromProto, REDIRECT_STRING_ELEMENTS);

TEST(PackedStruct, StringFromProto) {
  envoy::config::route::v3::RedirectAction redirect_action;
  TestUtility::loadFromYaml(std::string(R"EOF(
  scheme_redirect: abc
  host_redirect: def
  )EOF"),
                            redirect_action);

  Stats::TestUtil::MemoryTest memory_test;
  RedirectStringsFromProto<std::string, envoy::config::route::v3::RedirectAction> redirect_strings(
      redirect_action);
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);
  EXPECT_EQ(redirect_strings.scheme_redirect(), "abc");
  EXPECT_EQ(redirect_strings.host_redirect(), "def");
  EXPECT_FALSE(redirect_strings.has_path_redirect());
}

MAKE_PACKED_STRUCT(RedirectStrings, REDIRECT_STRING_ELEMENTS);

TEST(PackedStruct, StringStruct) {
  Stats::TestUtil::MemoryTest memory_test;
  RedirectStrings<std::string> redirect_strings(2);
  redirect_strings.set_scheme_redirect("abc");
  redirect_strings.set_path_redirect("def");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings.scheme_redirect(), "abc");
  EXPECT_EQ(redirect_strings.path_redirect(), "def");
  EXPECT_FALSE(redirect_strings.has_host_redirect());

  redirect_strings.set_host_redirect("abcd");
  EXPECT_EQ(redirect_strings.host_redirect(), "abcd");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 3 * sizeof(std::string) + 16);
}

MAKE_PACKED_MS_STRUCT(TimeoutsFromProto, TIMEOUT_ELEMENTS);

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
  Stats::TestUtil::MemoryTest memory_test;
  TimeoutsFromProto<std::chrono::milliseconds, envoy::config::route::v3::RouteAction> timeouts(
      route);
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::chrono::milliseconds));
  EXPECT_EQ(timeouts.idle_timeout().value().count(), 20);
  EXPECT_EQ(timeouts.max_grpc_timeout().value().count(), 5);
  EXPECT_FALSE(timeouts.grpc_timeout_offset().has_value());
}

MAKE_PACKED_OPTIONAL_STRUCT(Timeouts, TIMEOUT_ELEMENTS);

TEST(PackedStruct, DurationMS) {
  Stats::TestUtil::MemoryTest memory_test;
  Timeouts<std::chrono::milliseconds> timeouts(2);
  timeouts.set_idle_timeout(std::chrono::milliseconds(20));
  timeouts.set_max_grpc_timeout(std::chrono::milliseconds(5));
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::chrono::milliseconds));

  EXPECT_EQ(timeouts.idle_timeout().value().count(), 20);
  EXPECT_EQ(timeouts.max_grpc_timeout().value().count(), 5);
  EXPECT_FALSE(timeouts.grpc_timeout_offset().has_value());
}

TEST(PackedStruct, StringStructNoMacro) {
  enum class RedirectStringElement { scheme_redirect, host_redirect, path_redirect };
  using RedirectStringsPackedStruct = PackedStruct<std::string, 3, RedirectStringElement>;

  Stats::TestUtil::MemoryTest memory_test;
  // Initialize capacity to 2.
  RedirectStringsPackedStruct redirect_strings(2);
  redirect_strings.set(RedirectStringElement::scheme_redirect, "abc");
  redirect_strings.set(RedirectStringElement::path_redirect, "def");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings.size(), 2);
  EXPECT_EQ(redirect_strings.capacity(), 2);
  EXPECT_EQ(redirect_strings.get(RedirectStringElement::scheme_redirect), "abc");
  EXPECT_EQ(redirect_strings.get(RedirectStringElement::path_redirect), "def");
  EXPECT_FALSE(redirect_strings.has(RedirectStringElement::host_redirect));

  // Add a third element.
  redirect_strings.set(RedirectStringElement::host_redirect, "abcd");
  EXPECT_EQ(redirect_strings.get(RedirectStringElement::host_redirect), "abcd");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 3 * sizeof(std::string) + 16);
  EXPECT_EQ(redirect_strings.size(), 3);
  EXPECT_EQ(redirect_strings.capacity(), 3);
}

TEST(PackedStruct, StringStructMove) {
  enum class RedirectStringElement { scheme_redirect, host_redirect, path_redirect };
  using RedirectStringsPackedStruct = PackedStruct<std::string, 3, RedirectStringElement>;

  Stats::TestUtil::MemoryTest memory_test;
  // Initialize capacity to 2.
  RedirectStringsPackedStruct redirect_strings(2);
  redirect_strings.set(RedirectStringElement::scheme_redirect, "abc");
  redirect_strings.set(RedirectStringElement::path_redirect, "def");
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings.size(), 2);
  EXPECT_EQ(redirect_strings.capacity(), 2);
  EXPECT_EQ(redirect_strings.get(RedirectStringElement::scheme_redirect), "abc");
  EXPECT_EQ(redirect_strings.get(RedirectStringElement::path_redirect), "def");
  EXPECT_FALSE(redirect_strings.has(RedirectStringElement::host_redirect));

  // Invoke move constructor.
  RedirectStringsPackedStruct redirect_strings2(move(redirect_strings));
  // Ensure no memory was allocated on the heap by the move constructor.
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings2.size(), 2);
  EXPECT_EQ(redirect_strings2.capacity(), 2);
  EXPECT_EQ(redirect_strings2.get(RedirectStringElement::scheme_redirect), "abc");
  EXPECT_EQ(redirect_strings2.get(RedirectStringElement::path_redirect), "def");
  EXPECT_FALSE(redirect_strings2.has(RedirectStringElement::host_redirect));

  // Verify that redirect_strings is now empty.
  EXPECT_EQ(redirect_strings.size(), 0);
  EXPECT_EQ(redirect_strings.capacity(), 0);
  EXPECT_FALSE(redirect_strings.has(RedirectStringElement::scheme_redirect));
  EXPECT_FALSE(redirect_strings.has(RedirectStringElement::path_redirect));
  EXPECT_FALSE(redirect_strings.has(RedirectStringElement::host_redirect));

  // Invoke move assignment.
  RedirectStringsPackedStruct redirect_strings3(0);
  redirect_strings3 = move(redirect_strings2);
  // Ensure no memory was allocated on the heap by the move assignment.
  EXPECT_MEMORY_LE(memory_test.consumedBytes(), 2 * sizeof(std::string) + 16);

  EXPECT_EQ(redirect_strings3.size(), 2);
  EXPECT_EQ(redirect_strings3.capacity(), 2);
  EXPECT_EQ(redirect_strings3.get(RedirectStringElement::scheme_redirect), "abc");
  EXPECT_EQ(redirect_strings3.get(RedirectStringElement::path_redirect), "def");
  EXPECT_FALSE(redirect_strings3.has(RedirectStringElement::host_redirect));

  // Verify that redirect_strings2 is now empty.
  EXPECT_EQ(redirect_strings2.size(), 0);
  EXPECT_EQ(redirect_strings2.capacity(), 0);
  EXPECT_FALSE(redirect_strings2.has(RedirectStringElement::scheme_redirect));
  EXPECT_FALSE(redirect_strings2.has(RedirectStringElement::path_redirect));
  EXPECT_FALSE(redirect_strings2.has(RedirectStringElement::host_redirect));
}

} // namespace
} // namespace Envoy
