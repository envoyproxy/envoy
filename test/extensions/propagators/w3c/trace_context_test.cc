#include "source/extensions/propagators/w3c/trace_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {
namespace {

class TraceParentTest : public ::testing::Test {
protected:
  // Valid test cases from W3C specification examples
  const std::string valid_traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  const std::string valid_version = "00";
  const std::string valid_trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
  const std::string valid_parent_id = "00f067aa0ba902b7";
  const std::string valid_trace_flags = "01";
};

TEST_F(TraceParentTest, ParseValidTraceparent) {
  auto result = TraceParent::parse(valid_traceparent);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& traceparent = result.value();
  EXPECT_EQ(traceparent.version(), valid_version);
  EXPECT_EQ(traceparent.traceId(), valid_trace_id);
  EXPECT_EQ(traceparent.parentId(), valid_parent_id);
  EXPECT_EQ(traceparent.traceFlags(), valid_trace_flags);
  EXPECT_TRUE(traceparent.isSampled());
}

TEST_F(TraceParentTest, ParseInvalidLength) {
  // Too short
  auto result1 = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7");
  EXPECT_FALSE(result1.ok());
  EXPECT_EQ(result1.status().code(), absl::StatusCode::kInvalidArgument);

  // Too long
  auto result2 =
      TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01-extra");
  EXPECT_FALSE(result2.ok());
  EXPECT_EQ(result2.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TraceParentTest, ParseInvalidFormat) {
  // Wrong number of fields
  auto result1 = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7");
  EXPECT_FALSE(result1.ok());

  // Invalid field sizes
  auto result2 = TraceParent::parse("0-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
  EXPECT_FALSE(result2.ok());

  auto result3 = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e473-00f067aa0ba902b7-01");
  EXPECT_FALSE(result3.ok());

  auto result4 = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b-01");
  EXPECT_FALSE(result4.ok());

  auto result5 = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-1");
  EXPECT_FALSE(result5.ok());
}

TEST_F(TraceParentTest, ParseInvalidHex) {
  auto result = TraceParent::parse("0g-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TraceParentTest, ParseAllZerosTraceId) {
  auto result = TraceParent::parse("00-00000000000000000000000000000000-00f067aa0ba902b7-01");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TraceParentTest, ParseAllZerosParentId) {
  auto result = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(TraceParentTest, ToStringRoundTrip) {
  auto parsed = TraceParent::parse(valid_traceparent);
  ASSERT_TRUE(parsed.ok());

  std::string serialized = parsed.value().toString();
  EXPECT_EQ(serialized, valid_traceparent);
}

TEST_F(TraceParentTest, SampledFlag) {
  // Test sampled flag set
  auto sampled = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01");
  ASSERT_TRUE(sampled.ok());
  EXPECT_TRUE(sampled.value().isSampled());

  // Test sampled flag unset
  auto not_sampled = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00");
  ASSERT_TRUE(not_sampled.ok());
  EXPECT_FALSE(not_sampled.value().isSampled());
}

TEST_F(TraceParentTest, SetSampledFlag) {
  auto traceparent = TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00");
  ASSERT_TRUE(traceparent.ok());

  auto mutable_traceparent = traceparent.value();
  EXPECT_FALSE(mutable_traceparent.isSampled());

  mutable_traceparent.setSampled(true);
  EXPECT_TRUE(mutable_traceparent.isSampled());
  EXPECT_EQ(mutable_traceparent.traceFlags(), "01");

  mutable_traceparent.setSampled(false);
  EXPECT_FALSE(mutable_traceparent.isSampled());
  EXPECT_EQ(mutable_traceparent.traceFlags(), "00");
}

class TraceStateTest : public ::testing::Test {
protected:
  const std::string valid_tracestate = "congo=t61rcWkgMzE,rojo=00f067aa0ba902b7";
  const std::string single_entry = "congo=t61rcWkgMzE";
};

TEST_F(TraceStateTest, ParseEmpty) {
  auto result = TraceState::parse("");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());
}

TEST_F(TraceStateTest, ParseSingleEntry) {
  auto result = TraceState::parse(single_entry);
  ASSERT_TRUE(result.ok());

  const auto& tracestate = result.value();
  EXPECT_FALSE(tracestate.empty());

  auto congo_value = tracestate.get("congo");
  ASSERT_TRUE(congo_value.has_value());
  EXPECT_EQ(congo_value.value(), "t61rcWkgMzE");
}

TEST_F(TraceStateTest, ParseMultipleEntries) {
  auto result = TraceState::parse(valid_tracestate);
  ASSERT_TRUE(result.ok());

  const auto& tracestate = result.value();
  EXPECT_FALSE(tracestate.empty());

  auto congo_value = tracestate.get("congo");
  ASSERT_TRUE(congo_value.has_value());
  EXPECT_EQ(congo_value.value(), "t61rcWkgMzE");

  auto rojo_value = tracestate.get("rojo");
  ASSERT_TRUE(rojo_value.has_value());
  EXPECT_EQ(rojo_value.value(), "00f067aa0ba902b7");
}

TEST_F(TraceStateTest, ToStringRoundTrip) {
  auto parsed = TraceState::parse(valid_tracestate);
  ASSERT_TRUE(parsed.ok());

  std::string serialized = parsed.value().toString();
  EXPECT_EQ(serialized, valid_tracestate);
}

TEST_F(TraceStateTest, SetAndGet) {
  TraceState tracestate;
  EXPECT_TRUE(tracestate.empty());

  tracestate.set("test", "value123");
  EXPECT_FALSE(tracestate.empty());

  auto value = tracestate.get("test");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value123");
}

TEST_F(TraceStateTest, RemoveEntry) {
  auto tracestate = TraceState::parse(valid_tracestate).value();

  auto congo_value = tracestate.get("congo");
  ASSERT_TRUE(congo_value.has_value());

  tracestate.remove("congo");

  auto congo_value_after = tracestate.get("congo");
  EXPECT_FALSE(congo_value_after.has_value());

  // rojo should still be there
  auto rojo_value = tracestate.get("rojo");
  ASSERT_TRUE(rojo_value.has_value());
  EXPECT_EQ(rojo_value.value(), "00f067aa0ba902b7");
}

TEST_F(TraceStateTest, OverwriteEntry) {
  TraceState tracestate;
  tracestate.set("test", "value1");

  auto value1 = tracestate.get("test");
  ASSERT_TRUE(value1.has_value());
  EXPECT_EQ(value1.value(), "value1");

  tracestate.set("test", "value2");

  auto value2 = tracestate.get("test");
  ASSERT_TRUE(value2.has_value());
  EXPECT_EQ(value2.value(), "value2");
}

class TraceContextTest : public ::testing::Test {
protected:
  TraceParent sample_traceparent{
      TraceParent::parse("00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01").value()};
  TraceState sample_tracestate{
      TraceState::parse("congo=t61rcWkgMzE,rojo=00f067aa0ba902b7").value()};
};

TEST_F(TraceContextTest, ConstructWithTraceparentOnly) {
  TraceContext context(sample_traceparent);

  EXPECT_EQ(context.traceParent().toString(), sample_traceparent.toString());
  EXPECT_FALSE(context.hasTraceState());
  EXPECT_TRUE(context.traceState().empty());
}

TEST_F(TraceContextTest, ConstructWithBoth) {
  TraceContext context(sample_traceparent, sample_tracestate);

  EXPECT_EQ(context.traceParent().toString(), sample_traceparent.toString());
  EXPECT_TRUE(context.hasTraceState());
  EXPECT_EQ(context.traceState().toString(), sample_tracestate.toString());
}

TEST_F(TraceContextTest, MutableAccess) {
  TraceContext context(sample_traceparent);

  // Modify traceparent
  context.traceParent().setSampled(false);
  EXPECT_FALSE(context.traceParent().isSampled());

  // Modify tracestate
  context.traceState().set("test", "value");
  EXPECT_TRUE(context.hasTraceState());

  auto value = context.traceState().get("test");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value");
}

// BaggageMember Tests
class BaggageMemberTest : public ::testing::Test {
protected:
  const std::string valid_member = "key1=value1";
  const std::string member_with_properties = "key1=value1;prop1=propvalue1;prop2=propvalue2";
  const std::string url_encoded_member = "my%20key=my%20value";
};

TEST_F(BaggageMemberTest, ParseValidMember) {
  auto result = BaggageMember::parse(valid_member);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& member = result.value();
  EXPECT_EQ(member.key(), "key1");
  EXPECT_EQ(member.value(), "value1");
  EXPECT_TRUE(member.properties().empty());
}

TEST_F(BaggageMemberTest, ParseMemberWithProperties) {
  auto result = BaggageMember::parse(member_with_properties);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& member = result.value();
  EXPECT_EQ(member.key(), "key1");
  EXPECT_EQ(member.value(), "value1");
  EXPECT_EQ(member.properties().size(), 2);

  auto prop1 = member.getProperty("prop1");
  ASSERT_TRUE(prop1.has_value());
  EXPECT_EQ(prop1.value(), "propvalue1");

  auto prop2 = member.getProperty("prop2");
  ASSERT_TRUE(prop2.has_value());
  EXPECT_EQ(prop2.value(), "propvalue2");
}

TEST_F(BaggageMemberTest, ParseUrlEncodedMember) {
  auto result = BaggageMember::parse(url_encoded_member);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& member = result.value();
  EXPECT_EQ(member.key(), "my key");
  EXPECT_EQ(member.value(), "my value");
}

TEST_F(BaggageMemberTest, RoundTripSerialization) {
  auto original = BaggageMember::parse(member_with_properties);
  ASSERT_TRUE(original.ok());

  std::string serialized = original.value().toString();
  auto reparsed = BaggageMember::parse(serialized);
  ASSERT_TRUE(reparsed.ok());

  EXPECT_EQ(original.value().key(), reparsed.value().key());
  EXPECT_EQ(original.value().value(), reparsed.value().value());
  EXPECT_EQ(original.value().properties().size(), reparsed.value().properties().size());
}

TEST_F(BaggageMemberTest, InvalidMembers) {
  EXPECT_FALSE(BaggageMember::parse("").ok());
  EXPECT_FALSE(BaggageMember::parse("no_equals").ok());
  EXPECT_FALSE(BaggageMember::parse("=no_key").ok());
  EXPECT_FALSE(
      BaggageMember::parse("key=").ok()); // Empty value is allowed but let's keep it simple
}

// Baggage Tests
class BaggageTest : public ::testing::Test {
protected:
  const std::string simple_baggage = "key1=value1,key2=value2";
  const std::string complex_baggage = "key1=value1;prop1=val1,key2=value2;prop2=val2;prop3=val3";
  const std::string url_encoded_baggage = "my%20key=my%20value,other%20key=other%20value";
};

TEST_F(BaggageTest, ParseSimpleBaggage) {
  auto result = Baggage::parse(simple_baggage);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& baggage = result.value();
  EXPECT_EQ(baggage.getMembers().size(), 2);

  auto value1 = baggage.get("key1");
  ASSERT_TRUE(value1.has_value());
  EXPECT_EQ(value1.value(), "value1");

  auto value2 = baggage.get("key2");
  ASSERT_TRUE(value2.has_value());
  EXPECT_EQ(value2.value(), "value2");
}

TEST_F(BaggageTest, ParseComplexBaggage) {
  auto result = Baggage::parse(complex_baggage);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& baggage = result.value();
  EXPECT_EQ(baggage.getMembers().size(), 2);

  const auto& members = baggage.getMembers();
  EXPECT_EQ(members[0].properties().size(), 1);
  EXPECT_EQ(members[1].properties().size(), 2);
}

TEST_F(BaggageTest, SetAndGetBaggage) {
  Baggage baggage;
  EXPECT_TRUE(baggage.empty());

  EXPECT_TRUE(baggage.set("key1", "value1"));
  EXPECT_FALSE(baggage.empty());

  auto value = baggage.get("key1");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value1");
}

TEST_F(BaggageTest, UpdateExistingKey) {
  Baggage baggage;
  EXPECT_TRUE(baggage.set("key1", "value1"));
  EXPECT_TRUE(baggage.set("key1", "updated_value"));

  auto value = baggage.get("key1");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "updated_value");
  EXPECT_EQ(baggage.getMembers().size(), 1);
}

TEST_F(BaggageTest, RemoveBaggage) {
  auto baggage_result = Baggage::parse(simple_baggage);
  ASSERT_TRUE(baggage_result.ok());

  auto baggage = std::move(baggage_result.value());
  EXPECT_EQ(baggage.getMembers().size(), 2);

  baggage.remove("key1");
  EXPECT_EQ(baggage.getMembers().size(), 1);
  EXPECT_FALSE(baggage.get("key1").has_value());
  EXPECT_TRUE(baggage.get("key2").has_value());
}

TEST_F(BaggageTest, RoundTripSerialization) {
  auto original = Baggage::parse(complex_baggage);
  ASSERT_TRUE(original.ok());

  std::string serialized = original.value().toString();
  auto reparsed = Baggage::parse(serialized);
  ASSERT_TRUE(reparsed.ok());

  EXPECT_EQ(original.value().getMembers().size(), reparsed.value().getMembers().size());

  for (const auto& member : original.value().getMembers()) {
    auto value = reparsed.value().get(member.key());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), member.value());
  }
}

TEST_F(BaggageTest, EmptyBaggage) {
  auto result = Baggage::parse("");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());

  result = Baggage::parse("   ");
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());
}

// TraceContext with Baggage Tests
class TraceContextBaggageTest : public ::testing::Test {
protected:
  TraceParent createValidTraceParent() {
    return TraceParent("00", "4bf92f3577b34da6a3ce929d0e0e4736", "00f067aa0ba902b7", "01");
  }

  Baggage createValidBaggage() {
    auto result = Baggage::parse("key1=value1,key2=value2");
    return std::move(result.value());
  }
};

TEST_F(TraceContextBaggageTest, TraceContextWithBaggage) {
  TraceParent traceparent = createValidTraceParent();
  TraceState tracestate;
  Baggage baggage = createValidBaggage();

  TraceContext context(std::move(traceparent), std::move(tracestate), std::move(baggage));

  EXPECT_TRUE(context.hasBaggage());
  EXPECT_EQ(context.baggage().getMembers().size(), 2);

  auto value = context.baggage().get("key1");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "value1");
}

TEST_F(TraceContextBaggageTest, ModifyBaggage) {
  TraceParent traceparent = createValidTraceParent();
  TraceContext context(std::move(traceparent));

  EXPECT_FALSE(context.hasBaggage());

  context.baggage().set("new_key", "new_value");
  EXPECT_TRUE(context.hasBaggage());

  auto value = context.baggage().get("new_key");
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(value.value(), "new_value");
}

} // namespace
} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
