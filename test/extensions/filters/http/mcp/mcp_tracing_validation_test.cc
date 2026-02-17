#include "source/extensions/filters/http/mcp/mcp_tracing_validation.h"

#include <string>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Mcp {
namespace {

TEST(McpTracingValidationTest, TraceParentValidation) {
  // Valid traceparent
  EXPECT_TRUE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Invalid sizes (must be exactly 55)
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(""));
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-extra"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "0-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Component size checks (all sum to 55 total but individual sizes are wrong)
  // 1-32-16-2 flags is wrong
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "0-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-012"));
  // 2-31-16-2 trace_id is wrong
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e473-00f067aa0ba902b7-012"));
  // 2-32-15-2 parent_id is wrong
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b-012"));
  // 2-32-16-1 flags is wrong
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-1"));

  // Invalid dash placement (sums to same length, same component count, but sizes wrong)
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "0004bf92f3577b34da6a3ce-929d0e0e4736-00f067aa0ba902b7-01"));

  // 55 chars but no dashes
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00.4bf92f3577b34da6a3ce929d0e0e4736.00f067aa0ba902b7.01"));

  // Invalid version
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));

  // Invalid hex
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "gg-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01")); // version
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01")); // traceid
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902bg-01")); // parentid
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-0g")); // flags

  // All zeros
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-00000000000000000000000000000000-00f067aa0ba902b7-01"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01"));
}

TEST(McpTracingValidationTest, TraceStateValidation) {
  // Canonical examples
  EXPECT_TRUE(McpTracingValidation::isValidTraceState(""));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("rojo=00f067aa0ba902b7"));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("congo=t61rcWkgMzE,rojo=00f067aa0ba902b7"));
  // empty list-members are allowed
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("congo=t61rcWkgMzE,,rojo=00f067aa0ba902b7"));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("key="));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("key= "));

  // simple keys

  // Allowed characters in simple keys
  EXPECT_TRUE(McpTracingValidation::isValidTraceState(
      "abcdefghijklmnopqrstuvwxyz0123456789_-*/=val"));
  // invalid start char (uppercase)
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("0key=val")); // digits not allowed at start
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("Key=val"));

  // Multi-tenant keys
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("tenant@system=val"));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("tenant@system="));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("019az_-*/@az019_-*/=val"));

  // tenant-id
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("Abc@system=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("-bc@system=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("@system=val")); // empty tenant

  // system-id
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@123=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@-abc=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@UPPER=val"));

  // duplicate keys are not allowed
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("rojo=a,rojo=b"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@system=a,tenant@system=b"));

  // no = sign
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("invalid"));
  // Oversized key/value
  EXPECT_FALSE(McpTracingValidation::isValidTraceState(absl::StrCat(std::string(257, 'a'), "=v")));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState(absl::StrCat("k=", std::string(257, 'a'))));
}

TEST(McpTracingValidationTest, BaggageValidation) {
  // Valid baggage
  EXPECT_TRUE(McpTracingValidation::isValidBaggage(""));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1=val1"));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1=val1,key2=val2"));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1=val1;prop1=pval1"));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1=val1;prop1=pval1;prop2=pval2"));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage(" key1 = val1 , key2 = val2 "));
  // empty values and properties without values are allowed in baggage
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1="));
  EXPECT_TRUE(McpTracingValidation::isValidBaggage("key1=val1;prop1"));

  // Invalid baggage
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1,,key2=val2"));
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("invalid"));
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1;"));
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1;prop1;"));

  // Invalid characters
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key @=val1"));
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val,"));
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=v al1"));

  // Invalid property value
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1;prop1=v al1"));

  // Too many members in TraceState
  std::string too_many_members;
  for (int i = 0; i < 65; ++i) {
    absl::StrAppend(&too_many_members, "k", i, "=v,");
  }
  EXPECT_FALSE(McpTracingValidation::isValidBaggage(too_many_members));

  // Oversized baggage
  EXPECT_FALSE(McpTracingValidation::isValidBaggage(std::string(8193, 'a')));

  // Baggage member without equals sign
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1val1"));
}

} // namespace
} // namespace Mcp
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
