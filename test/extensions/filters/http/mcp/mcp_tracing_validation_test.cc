#include "source/extensions/filters/http/mcp/mcp_tracing_validation.h"

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

  // Invalid hex
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "gg-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01"));  // version
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e473g-00f067aa0ba902b7-01"));  // traceid
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902bg-01"));  // parentid
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-0g"));  // flags

  // All zeros
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-00000000000000000000000000000000-00f067aa0ba902b7-01"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceParent(
      "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01"));
}

TEST(McpTracingValidationTest, TraceStateValidation) {
  EXPECT_TRUE(McpTracingValidation::isValidTraceState(""));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("rojo=00f067aa0ba902b7"));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("congo=t61rcWkgMzE,rojo=00f067aa0ba902b7"));

  // empty list-members are allowed
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("congo=t61rcWkgMzE,,rojo=00f067aa0ba902b7"));

  // empty values are allowed
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("key="));
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("key= "));

  EXPECT_FALSE(McpTracingValidation::isValidTraceState("invalid"));
  // duplicate keys are not allowed
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("rojo=a,rojo=b"));
  // Invalid values
  EXPECT_FALSE(McpTracingValidation::isValidTraceState(std::string(1025, 'a')));
  // Multi-tenant keys
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("tenant@system=val"));
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("@system=val"));  // empty tenant
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@=val"));  // empty system
  // multi-tenant system must be lower
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("tenant@UPPER=val"));
  // multi-tenant and system may start or even consist wholly of digits
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("123@456=val"));
  // special characters allowed AFTER first char in both tenant and system
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("a_-*/@a_-*/=val"));

  // special characters allowed in simple keys after first char
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("a_-*/=val"));

  // isValidTraceStateKey niche cases
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("key/with/slash=val"));  // covers slash char
  EXPECT_TRUE(McpTracingValidation::isValidTraceState("0key=val"));  // digits allowed at start
  // invalid start char (uppercase)
  EXPECT_FALSE(McpTracingValidation::isValidTraceState("Key=val"));

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

  // Multi-member with empty member
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1,,key2=val2"));

  // Invalid property value
  EXPECT_FALSE(McpTracingValidation::isValidBaggage("key1=val1;prop1=v al1"));

  // Too many members in TraceState
  std::string too_many_members;
  for (int i = 0; i < 33; ++i) {
    absl::StrAppend(&too_many_members, "k", i, "=v,");
  }
  EXPECT_FALSE(McpTracingValidation::isValidTraceState(too_many_members));

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
