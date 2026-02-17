#include "source/extensions/filters/http/mcp/tracing_validation.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

TEST(TracingValidationTest, TraceParentValidation) {
  // Valid traceparent
  EXPECT_TRUE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Invalid sizes (must be exactly 55)
  EXPECT_FALSE(TracingValidation::isValidTraceParent(""));
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-extra"));
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "0-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Component size checks (all sum to 55 total but individual sizes are wrong)
  // 1-32-16-2 flags is wrong
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "0-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-012"));
  // 2-31-16-2 trace_id is wrong
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e473-00f067aa0ba902b7-012"));
  // 2-32-15-2 parent_id is wrong
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b-012"));
  // 2-32-16-1 flags is wrong
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-1"));

  // Invalid dash placement (sums to same length, same component count, but sizes wrong)
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "0004bf92f3577b34da6a3ce-929d0e0e4736-00f067aa0ba902b7-01"));

  // 55 chars but no dashes
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00.4bf92f3577b34da6a3ce929d0e0e4736.00f067aa0ba902b7.01"));

  // Uppercase hex is not allowed
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4BF92F3577B34DA6A3CE929D0E0E4736-00F067AA0BA902B7-01"));

  // Invalid version
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Invalid hex
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "gg-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")); // version
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01")); // traceid
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902bg-01")); // parentid
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-0g")); // flags

  // All zeros
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-00000000000000000000000000000000-00f067aa0ba902b7-01"));
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01"));

  // Wrong number of components
  EXPECT_FALSE(TracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7"));
}

TEST(TracingValidationTest, TraceStateValidation) {
  // Canonical examples
  EXPECT_TRUE(TracingValidation::isValidTraceState(""));
  EXPECT_TRUE(TracingValidation::isValidTraceState("rojo=00f067aa0ba902b7"));
  EXPECT_TRUE(TracingValidation::isValidTraceState("congo=t61rcWkgMzE,rojo=00f067aa0ba902b7"));
  // empty list-members are allowed
  EXPECT_TRUE(TracingValidation::isValidTraceState("congo=t61rcWkgMzE,,rojo=00f067aa0ba902b7"));
  EXPECT_TRUE(TracingValidation::isValidTraceState("key="));
  EXPECT_TRUE(TracingValidation::isValidTraceState("key= "));

  // simple keys

  // Allowed characters in simple keys
  EXPECT_TRUE(TracingValidation::isValidTraceState(
      "abcdefghijklmnopqrstuvwxyz0123456789_-*/=val"));
  // invalid start char (uppercase)
  EXPECT_FALSE(TracingValidation::isValidTraceState("0key=val")); // digits not allowed at start
  EXPECT_FALSE(TracingValidation::isValidTraceState("Key=val"));

  // Multi-tenant keys
  EXPECT_TRUE(TracingValidation::isValidTraceState("tenant@system=val"));
  EXPECT_TRUE(TracingValidation::isValidTraceState("tenant@system="));
  EXPECT_TRUE(TracingValidation::isValidTraceState("019az_-*/@az019_-*/=val"));

  // tenant-id
  EXPECT_FALSE(TracingValidation::isValidTraceState("Abc@system=val"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("-bc@system=val"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("@system=val")); // empty tenant
  EXPECT_TRUE(TracingValidation::isValidTraceState(absl::StrCat(std::string(241, 'a'), "@s=v")));
  EXPECT_FALSE(TracingValidation::isValidTraceState(absl::StrCat(std::string(242, 'a'), "@s=v")));
  EXPECT_TRUE(TracingValidation::isValidTraceState(absl::StrCat("t@", std::string(14, 'a'), "=v")));
  EXPECT_FALSE(TracingValidation::isValidTraceState(absl::StrCat("t@", std::string(15, 'a'), "=v")));

  // system-id
  EXPECT_FALSE(TracingValidation::isValidTraceState("tenant@=val"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("tenant@123=val"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("tenant@-abc=val"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("tenant@UPPER=val"));

  // duplicate keys are not allowed
  EXPECT_FALSE(TracingValidation::isValidTraceState("rojo=a,rojo=b"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("tenant@system=a,tenant@system=b"));

  // Oversized key/value
  EXPECT_FALSE(TracingValidation::isValidTraceState(absl::StrCat(std::string(257, 'a'), "=v")));
  EXPECT_FALSE(TracingValidation::isValidTraceState(absl::StrCat("k=", std::string(257, 'a'))));

  // value with invalid chars
  EXPECT_FALSE(TracingValidation::isValidTraceState("k=v,v"));
  EXPECT_FALSE(TracingValidation::isValidTraceState("k=v=v"));
}

TEST(TracingValidationTest, TraceStateSizeLimit) {
  const std::string long_value(255, 'a');
  std::string long_ts;
  absl::StrAppend(&long_ts, "key", 1, "=", long_value, ",");
  absl::StrAppend(&long_ts, "key", 2, "=", long_value, ",");
  absl::StrAppend(&long_ts, "key", 3, "=", long_value);
  ASSERT_LE(long_ts.size(), 1024);
  EXPECT_TRUE(TracingValidation::isValidTraceState(long_ts));
  // Append more to exceed 1024
  absl::StrAppend(&long_ts, ",key", 3, "=", long_value);
  EXPECT_FALSE(TracingValidation::isValidTraceState(long_ts));
}

TEST(TracingValidationTest, TraceStateTooManyListMembers) {
  std::string ts_too_many_members;
  for (int i = 0; i < 32; ++i) {
    absl::StrAppend(&ts_too_many_members, "k", i + 1, "=v,");
  }
  ts_too_many_members.pop_back(); // remove last comma
  EXPECT_TRUE(TracingValidation::isValidTraceState(ts_too_many_members));
  absl::StrAppend(&ts_too_many_members, ",k33=v");
  EXPECT_FALSE(TracingValidation::isValidTraceState(ts_too_many_members));
}

TEST(TracingValidationTest, BaggageValidation) {
  // Valid baggage
  EXPECT_TRUE(TracingValidation::isValidBaggage(""));
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1=val1"));
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1=val1,key2=val2"));
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1=val1;prop1=pval1"));
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1=val1;prop1=pval1;prop2=pval2"));
  EXPECT_TRUE(TracingValidation::isValidBaggage(" key1 = val1 , key2 = val2 "));
  // empty values and properties without values are allowed in baggage
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1="));
  EXPECT_TRUE(TracingValidation::isValidBaggage("key1=val1;prop1"));

  // Invalid baggage
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=val1,,key2=val2"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("invalid"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=val1;"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=val1;prop1;"));

  // Invalid characters
  EXPECT_FALSE(TracingValidation::isValidBaggage("key @=val1"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=val,"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=v al1"));

  // Invalid property value
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1=val1;prop1=v al1"));

  // Oversized baggage
  EXPECT_FALSE(TracingValidation::isValidBaggage(std::string(8193, 'a')));

  // Baggage member without equals sign
  EXPECT_FALSE(TracingValidation::isValidBaggage("key1val1"));

  // Baggage key with delimiters
  EXPECT_FALSE(TracingValidation::isValidBaggage("key(=val"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key)=val"));
  EXPECT_FALSE(TracingValidation::isValidBaggage("key[=val"));

  // Baggage property validation
  EXPECT_FALSE(TracingValidation::isValidBaggage("k=v;prop(=pv"));
  EXPECT_TRUE(TracingValidation::isValidBaggage("k=v;prop=pv ")); // Valid because of trimming
  // Control char in property value
  EXPECT_FALSE(TracingValidation::isValidBaggage("k=v;prop=pv\001"));
}

TEST(TracingValidationTest, BaggageTooManyMembers) {
  std::string too_many_members;
  for (int i = 0; i < 63; ++i) {
    absl::StrAppend(&too_many_members, "k", i+1, "=v,");
  }
  // last member cannot have a comma
  absl::StrAppend(&too_many_members, "k", 64, "=v");
  EXPECT_TRUE(TracingValidation::isValidBaggage(too_many_members));
  // With the 65th member, it's too large
  absl::StrAppend(&too_many_members, ",k", 65, "=v");
  EXPECT_FALSE(TracingValidation::isValidBaggage(too_many_members));
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
