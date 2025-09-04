#include "source/common/http/header_map_impl.h"
#include "source/common/tracing/trace_context_impl.h"
#include "source/extensions/propagators/w3c/propagator.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Propagators {
namespace W3C {
namespace {

class PropagatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    headers_ = Http::RequestHeaderMapImpl::create();
    trace_context_ = std::make_unique<Tracing::TraceContextImpl>(*headers_);
  }

  Http::RequestHeaderMapPtr headers_;
  std::unique_ptr<Tracing::TraceContextImpl> trace_context_;

  const std::string valid_traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  const std::string valid_tracestate = "congo=t61rcWkgMzE,rojo=00f067aa0ba902b7";
};

TEST_F(PropagatorTest, IsNotPresentWhenEmpty) {
  EXPECT_FALSE(Propagator::isPresent(*trace_context_));
}

TEST_F(PropagatorTest, IsPresentWithTraceparent) {
  headers_->addCopy("traceparent", valid_traceparent);
  EXPECT_TRUE(Propagator::isPresent(*trace_context_));
}

TEST_F(PropagatorTest, ExtractWithTraceparentOnly) {
  headers_->addCopy("traceparent", valid_traceparent);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_EQ(context.traceParent().toString(), valid_traceparent);
  EXPECT_FALSE(context.hasTraceState());
}

TEST_F(PropagatorTest, ExtractWithBothHeaders) {
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", valid_tracestate);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_EQ(context.traceParent().toString(), valid_traceparent);
  EXPECT_TRUE(context.hasTraceState());
  EXPECT_EQ(context.traceState().toString(), valid_tracestate);
}

TEST_F(PropagatorTest, ExtractWithMultipleTracestateHeaders) {
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", "congo=t61rcWkgMzE");
  headers_->addCopy("tracestate", "rojo=00f067aa0ba902b7");

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_TRUE(context.hasTraceState());

  // Should have both entries (combined)
  auto congo_value = context.traceState().get("congo");
  ASSERT_TRUE(congo_value.has_value());
  EXPECT_EQ(congo_value.value(), "t61rcWkgMzE");

  auto rojo_value = context.traceState().get("rojo");
  ASSERT_TRUE(rojo_value.has_value());
  EXPECT_EQ(rojo_value.value(), "00f067aa0ba902b7");
}

TEST_F(PropagatorTest, ExtractFailsWithoutTraceparent) {
  headers_->addCopy("tracestate", valid_tracestate);

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PropagatorTest, ExtractFailsWithInvalidTraceparent) {
  headers_->addCopy("traceparent", "invalid-traceparent");

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PropagatorTest, InjectTraceparentOnly) {
  auto traceparent = TraceParent::parse(valid_traceparent).value();
  TraceContext context(traceparent);

  Propagator::inject(context, *trace_context_);

  auto traceparent_header = headers_->get(Http::LowerCaseString("traceparent"));
  ASSERT_FALSE(traceparent_header.empty());
  EXPECT_EQ(traceparent_header[0]->value().getStringView(), valid_traceparent);

  auto tracestate_header = headers_->get(Http::LowerCaseString("tracestate"));
  EXPECT_TRUE(tracestate_header.empty());
}

TEST_F(PropagatorTest, InjectBothHeaders) {
  auto traceparent = TraceParent::parse(valid_traceparent).value();
  auto tracestate = TraceState::parse(valid_tracestate).value();
  TraceContext context(traceparent, tracestate);

  Propagator::inject(context, *trace_context_);

  auto traceparent_header = headers_->get(Http::LowerCaseString("traceparent"));
  ASSERT_FALSE(traceparent_header.empty());
  EXPECT_EQ(traceparent_header[0]->value().getStringView(), valid_traceparent);

  auto tracestate_header = headers_->get(Http::LowerCaseString("tracestate"));
  ASSERT_FALSE(tracestate_header.empty());
  EXPECT_EQ(tracestate_header[0]->value().getStringView(), valid_tracestate);
}

TEST_F(PropagatorTest, CreateChild) {
  auto traceparent = TraceParent::parse(valid_traceparent).value();
  auto tracestate = TraceState::parse(valid_tracestate).value();
  TraceContext parent_context(traceparent, tracestate);

  std::string new_span_id = "b7ad6b7169203331";
  auto result = Propagator::createChild(parent_context, new_span_id);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& child_context = result.value();

  // Should have same trace ID but new span ID
  EXPECT_EQ(child_context.traceParent().traceId(), traceparent.traceId());
  EXPECT_EQ(child_context.traceParent().parentId(), new_span_id);
  EXPECT_EQ(child_context.traceParent().version(), traceparent.version());
  EXPECT_EQ(child_context.traceParent().traceFlags(), traceparent.traceFlags());

  // Should inherit tracestate
  EXPECT_TRUE(child_context.hasTraceState());
  EXPECT_EQ(child_context.traceState().toString(), tracestate.toString());
}

TEST_F(PropagatorTest, CreateChildInvalidSpanId) {
  auto traceparent = TraceParent::parse(valid_traceparent).value();
  TraceContext parent_context(traceparent);

  // Invalid span ID (wrong length)
  auto result = Propagator::createChild(parent_context, "invalid");
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PropagatorTest, CreateRoot) {
  std::string trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
  std::string span_id = "00f067aa0ba902b7";

  auto result = Propagator::createRoot(trace_id, span_id, true);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& root_context = result.value();

  EXPECT_EQ(root_context.traceParent().version(), "00");
  EXPECT_EQ(root_context.traceParent().traceId(), trace_id);
  EXPECT_EQ(root_context.traceParent().parentId(), span_id);
  EXPECT_EQ(root_context.traceParent().traceFlags(), "01");
  EXPECT_TRUE(root_context.traceParent().isSampled());
  EXPECT_FALSE(root_context.hasTraceState());
}

TEST_F(PropagatorTest, CreateRootNotSampled) {
  std::string trace_id = "4bf92f3577b34da6a3ce929d0e0e4736";
  std::string span_id = "00f067aa0ba902b7";

  auto result = Propagator::createRoot(trace_id, span_id, false);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& root_context = result.value();

  EXPECT_EQ(root_context.traceParent().traceFlags(), "00");
  EXPECT_FALSE(root_context.traceParent().isSampled());
}

TEST_F(PropagatorTest, CreateRootInvalidInputs) {
  // Invalid trace ID
  auto result1 = Propagator::createRoot("invalid", "00f067aa0ba902b7", true);
  EXPECT_FALSE(result1.ok());

  // Invalid span ID
  auto result2 = Propagator::createRoot("4bf92f3577b34da6a3ce929d0e0e4736", "invalid", true);
  EXPECT_FALSE(result2.ok());
}

class TracingHelperTest : public ::testing::Test {
protected:
  void SetUp() override {
    headers_ = Http::RequestHeaderMapImpl::create();
    trace_context_ = std::make_unique<Tracing::TraceContextImpl>(*headers_);
  }

  Http::RequestHeaderMapPtr headers_;
  std::unique_ptr<Tracing::TraceContextImpl> trace_context_;

  const std::string valid_traceparent = "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  const std::string valid_tracestate = "congo=t61rcWkgMzE,rojo=00f067aa0ba902b7";
};

TEST_F(TracingHelperTest, ExtractForTracer) {
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", valid_tracestate);

  auto result = TracingHelper::extractForTracer(*trace_context_);
  ASSERT_TRUE(result.has_value());

  const auto& extracted = result.value();
  EXPECT_EQ(extracted.version, "00");
  EXPECT_EQ(extracted.trace_id, "4bf92f3577b34da6a3ce929d0e0e4736");
  EXPECT_EQ(extracted.span_id, "00f067aa0ba902b7");
  EXPECT_EQ(extracted.trace_flags, "01");
  EXPECT_TRUE(extracted.sampled);
  EXPECT_EQ(extracted.tracestate, valid_tracestate);
}

TEST_F(TracingHelperTest, ExtractForTracerNoHeaders) {
  auto result = TracingHelper::extractForTracer(*trace_context_);
  EXPECT_FALSE(result.has_value());
}

TEST_F(TracingHelperTest, TraceparentPresent) {
  EXPECT_FALSE(TracingHelper::traceparentPresent(*trace_context_));

  headers_->addCopy("traceparent", valid_traceparent);
  EXPECT_TRUE(TracingHelper::traceparentPresent(*trace_context_));
}

TEST_F(TracingHelperTest, RoundTripThroughPropagator) {
  // Inject a context
  auto traceparent = TraceParent::parse(valid_traceparent).value();
  auto tracestate = TraceState::parse(valid_tracestate).value();
  TraceContext original_context(traceparent, tracestate);

  Propagator::inject(original_context, *trace_context_);

  // Extract it back
  auto extracted_result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(extracted_result.ok());

  const auto& extracted_context = extracted_result.value();
  EXPECT_EQ(extracted_context.traceParent().toString(), original_context.traceParent().toString());
  EXPECT_EQ(extracted_context.traceState().toString(), original_context.traceState().toString());
}

// Baggage Tests
class BaggagePropagatorTest : public ::testing::Test {
protected:
  void SetUp() override {
    headers_ = Http::RequestHeaderMapImpl::create();
    trace_context_ = std::make_unique<Tracing::TraceContextImpl>(*headers_);
  }

  Http::RequestHeaderMapPtr headers_;
  std::unique_ptr<Tracing::TraceContextImpl> trace_context_;

  const std::string valid_baggage = "key1=value1,key2=value2;prop1=propvalue1";
  const std::string simple_baggage = "userId=alice,sessionId=xyz";
};

TEST_F(BaggagePropagatorTest, IsBaggageNotPresentWhenEmpty) {
  EXPECT_FALSE(Propagator::isBaggagePresent(*trace_context_));
}

TEST_F(BaggagePropagatorTest, IsBaggagePresentWhenHeaderExists) {
  headers_->addCopy(Http::LowerCaseString("baggage"), valid_baggage);
  EXPECT_TRUE(Propagator::isBaggagePresent(*trace_context_));
}

TEST_F(BaggagePropagatorTest, ExtractValidBaggage) {
  headers_->addCopy(Http::LowerCaseString("baggage"), valid_baggage);

  auto result = Propagator::extractBaggage(*trace_context_);
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

TEST_F(BaggagePropagatorTest, ExtractEmptyWhenNoBaggageHeader) {
  auto result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());
}

TEST_F(BaggagePropagatorTest, InjectBaggage) {
  auto baggage_result = Baggage::parse(simple_baggage);
  ASSERT_TRUE(baggage_result.ok());

  Propagator::injectBaggage(baggage_result.value(), *trace_context_);

  // Check that the header was set
  EXPECT_TRUE(Propagator::isBaggagePresent(*trace_context_));

  // Extract and verify
  auto extracted = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(extracted.ok());

  auto userId = extracted.value().get("userId");
  ASSERT_TRUE(userId.has_value());
  EXPECT_EQ(userId.value(), "alice");

  auto sessionId = extracted.value().get("sessionId");
  ASSERT_TRUE(sessionId.has_value());
  EXPECT_EQ(sessionId.value(), "xyz");
}

TEST_F(BaggagePropagatorTest, BaggageRoundTrip) {
  // Create original baggage
  auto original_baggage = Baggage::parse(valid_baggage);
  ASSERT_TRUE(original_baggage.ok());

  // Inject it
  Propagator::injectBaggage(original_baggage.value(), *trace_context_);

  // Extract it back
  auto extracted_baggage = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(extracted_baggage.ok());

  // Compare
  EXPECT_EQ(original_baggage.value().getMembers().size(),
            extracted_baggage.value().getMembers().size());

  for (const auto& member : original_baggage.value().getMembers()) {
    auto value = extracted_baggage.value().get(member.key());
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(value.value(), member.value());
  }
}

// BaggageHelper Tests
class BaggageHelperTest : public ::testing::Test {
protected:
  void SetUp() override {
    headers_ = Http::RequestHeaderMapImpl::create();
    trace_context_ = std::make_unique<Tracing::TraceContextImpl>(*headers_);
  }

  Http::RequestHeaderMapPtr headers_;
  std::unique_ptr<Tracing::TraceContextImpl> trace_context_;
};

TEST_F(BaggageHelperTest, GetBaggageValueWhenPresent) {
  headers_->addCopy(Http::LowerCaseString("baggage"), "key1=value1,key2=value2");

  std::string value1 = BaggageHelper::getBaggageValue(*trace_context_, "key1");
  EXPECT_EQ(value1, "value1");

  std::string value2 = BaggageHelper::getBaggageValue(*trace_context_, "key2");
  EXPECT_EQ(value2, "value2");
}

TEST_F(BaggageHelperTest, GetBaggageValueWhenNotPresent) {
  std::string value = BaggageHelper::getBaggageValue(*trace_context_, "nonexistent");
  EXPECT_EQ(value, "");
}

TEST_F(BaggageHelperTest, SetBaggageValue) {
  EXPECT_TRUE(BaggageHelper::setBaggageValue(*trace_context_, "testKey", "testValue"));

  std::string retrieved = BaggageHelper::getBaggageValue(*trace_context_, "testKey");
  EXPECT_EQ(retrieved, "testValue");
}

TEST_F(BaggageHelperTest, GetAllBaggage) {
  headers_->addCopy(Http::LowerCaseString("baggage"), "key1=value1,key2=value2,key3=value3");

  auto all_baggage = BaggageHelper::getAllBaggage(*trace_context_);
  EXPECT_EQ(all_baggage.size(), 3);
  EXPECT_EQ(all_baggage["key1"], "value1");
  EXPECT_EQ(all_baggage["key2"], "value2");
  EXPECT_EQ(all_baggage["key3"], "value3");
}

TEST_F(BaggageHelperTest, HasBaggage) {
  EXPECT_FALSE(BaggageHelper::hasBaggage(*trace_context_));

  headers_->addCopy(Http::LowerCaseString("baggage"), "key=value");
  EXPECT_TRUE(BaggageHelper::hasBaggage(*trace_context_));
}

// Integration test: Complete W3C context with baggage
TEST_F(PropagatorTest, ExtractCompleteW3CContextWithBaggage) {
  // Set all W3C headers
  headers_->addCopy(Http::LowerCaseString("traceparent"), valid_traceparent);
  headers_->addCopy(Http::LowerCaseString("tracestate"), valid_tracestate);
  headers_->addCopy(Http::LowerCaseString("baggage"), "userId=alice,sessionId=xyz123");

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();

  // Check traceparent
  EXPECT_EQ(context.traceParent().toString(), valid_traceparent);

  // Check tracestate
  EXPECT_TRUE(context.hasTraceState());
  auto congo_value = context.traceState().get("congo");
  ASSERT_TRUE(congo_value.has_value());
  EXPECT_EQ(congo_value.value(), "t61rcWkgMzE");

  // Check baggage
  EXPECT_TRUE(context.hasBaggage());
  auto user_id = context.baggage().get("userId");
  ASSERT_TRUE(user_id.has_value());
  EXPECT_EQ(user_id.value(), "alice");

  auto session_id = context.baggage().get("sessionId");
  ASSERT_TRUE(session_id.has_value());
  EXPECT_EQ(session_id.value(), "xyz123");
}

TEST_F(PropagatorTest, InjectCompleteW3CContextWithBaggage) {
  // Create a complete W3C context
  auto traceparent = TraceParent::parse(valid_traceparent);
  ASSERT_TRUE(traceparent.ok());

  auto tracestate = TraceState::parse(valid_tracestate);
  ASSERT_TRUE(tracestate.ok());

  auto baggage = Baggage::parse("userId=bob,sessionId=abc123");
  ASSERT_TRUE(baggage.ok());

  TraceContext context(std::move(traceparent.value()), std::move(tracestate.value()),
                       std::move(baggage.value()));

  // Inject it
  Propagator::inject(context, *trace_context_);

  // Verify all headers were set
  EXPECT_TRUE(Propagator::isPresent(*trace_context_));
  EXPECT_TRUE(Propagator::isBaggagePresent(*trace_context_));

  // Extract and verify
  auto extracted = Propagator::extract(*trace_context_);
  ASSERT_TRUE(extracted.ok());

  EXPECT_EQ(extracted.value().traceParent().toString(), context.traceParent().toString());
  EXPECT_TRUE(extracted.value().hasTraceState());
  EXPECT_TRUE(extracted.value().hasBaggage());

  auto extracted_user = extracted.value().baggage().get("userId");
  ASSERT_TRUE(extracted_user.has_value());
  EXPECT_EQ(extracted_user.value(), "bob");
}

// Additional W3C Specification Compliance Tests
class W3CSpecificationComplianceTest : public ::testing::Test {
protected:
  void SetUp() override {
    headers_ = Http::RequestHeaderMapImpl::create();
    trace_context_ = std::make_unique<Tracing::TraceContextImpl>(*headers_);
  }

  Http::RequestHeaderMapPtr headers_;
  std::unique_ptr<Tracing::TraceContextImpl> trace_context_;
};

// Test header case insensitivity (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, HeaderCaseInsensitivity) {
  // Test various case combinations
  headers_->addCopy("TraceParent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");
  headers_->addCopy("TraceState", "vendor=value");
  headers_->addCopy("Baggage", "key=value");

  EXPECT_TRUE(Propagator::isPresent(*trace_context_));
  EXPECT_TRUE(Propagator::isBaggagePresent(*trace_context_));

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  headers_->clear();
  headers_->addCopy("TRACEPARENT", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  EXPECT_TRUE(Propagator::isPresent(*trace_context_));
}

// Test future version compatibility (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, FutureVersionCompatibility) {
  // Future version should be accepted but treated conservatively
  headers_->addCopy("traceparent", "ff-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_EQ(context.traceParent().version(), "ff");
  EXPECT_EQ(context.traceParent().traceId(), "1234567890abcdef1234567890abcdef");
  EXPECT_EQ(context.traceParent().parentId(), "fedcba0987654321");
}

// Test traceparent format validation edge cases
TEST_F(W3CSpecificationComplianceTest, TraceparentFormatValidation) {
  // Test exact length requirement
  headers_->addCopy("traceparent",
                    "00-1234567890abcdef1234567890abcdef-fedcba0987654321-0"); // Too short
  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());

  headers_->clear();
  headers_->addCopy("traceparent",
                    "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01-extra"); // Too long
  result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());

  // Test invalid characters
  headers_->clear();
  headers_->addCopy("traceparent",
                    "00-1234567890abcdef1234567890abcdef-fedcba0987654321-0g"); // Invalid hex
  result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());
}

// Test all-zero trace ID rejection (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, RejectZeroTraceId) {
  headers_->addCopy("traceparent", "00-00000000000000000000000000000000-fedcba0987654321-01");

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("zero"));
}

// Test all-zero span ID rejection (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, RejectZeroSpanId) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-0000000000000000-01");

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());
  EXPECT_THAT(result.status().message(), testing::HasSubstr("zero"));
}

// Test tracestate validation and handling
TEST_F(W3CSpecificationComplianceTest, TracestateValidation) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  // Valid tracestate
  headers_->addCopy("tracestate", "vendor1=value1,vendor2=value2");
  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok());

  // Test maximum length handling (should not fail but may truncate)
  headers_->clear();
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");
  std::string long_tracestate(600, 'a'); // Very long tracestate
  headers_->addCopy("tracestate", long_tracestate);
  result = Propagator::extract(*trace_context_);
  // Should still work (implementation may truncate)
  EXPECT_TRUE(result.ok());
}

// Test multiple tracestate headers concatenation (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, MultiplTracestateHeadersConcatenation) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");
  headers_->addCopy("tracestate", "vendor1=value1");
  headers_->addCopy("tracestate", "vendor2=value2");
  headers_->addCopy("tracestate", "vendor3=value3");

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_TRUE(context.hasTraceState());

  // All vendors should be present
  EXPECT_TRUE(context.traceState().get("vendor1").has_value());
  EXPECT_TRUE(context.traceState().get("vendor2").has_value());
  EXPECT_TRUE(context.traceState().get("vendor3").has_value());
}

// Test baggage size limits (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, BaggageSizeLimits) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  // Create baggage that exceeds size limit
  std::string large_baggage = "key1=";
  large_baggage.append(9000, 'a'); // Exceed 8KB limit
  headers_->addCopy("baggage", large_baggage);

  auto result = Propagator::extractBaggage(*trace_context_);
  // Should either succeed with truncated baggage or fail gracefully
  if (result.ok()) {
    // If parsing succeeds, it should respect size limits
    const auto& baggage = result.value();
    EXPECT_LE(baggage.toString().size(), 8192); // 8KB limit
  } else {
    // Failing due to size limits is also acceptable
    EXPECT_THAT(result.status().message(), testing::HasSubstr("size"));
  }
}

// Test baggage URL encoding/decoding (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, BaggageUrlEncoding) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  // Test URL encoded baggage values
  headers_->addCopy("baggage", "user%20name=john%20doe,email=user%40example.com");

  auto result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& baggage = result.value();
  auto user_name = baggage.get("user name"); // Should be URL decoded
  ASSERT_TRUE(user_name.has_value());
  EXPECT_EQ(user_name.value(), "john doe");

  auto email = baggage.get("email");
  ASSERT_TRUE(email.has_value());
  EXPECT_EQ(email.value(), "user@example.com");
}

// Test baggage properties handling (W3C spec feature)
TEST_F(W3CSpecificationComplianceTest, BaggageProperties) {
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");

  // Baggage with properties
  headers_->addCopy("baggage", "key1=value1;prop1=propvalue1;prop2=propvalue2,key2=value2");

  auto result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& baggage = result.value();
  EXPECT_FALSE(baggage.empty());

  // Should handle baggage with properties (even if properties are not exposed in API)
  auto value1 = baggage.get("key1");
  ASSERT_TRUE(value1.has_value());
  EXPECT_EQ(value1.value(), "value1");

  auto value2 = baggage.get("key2");
  ASSERT_TRUE(value2.has_value());
  EXPECT_EQ(value2.value(), "value2");
}

// Test malformed header graceful handling
TEST_F(W3CSpecificationComplianceTest, MalformedHeaderHandling) {
  // Test various malformed headers that should be rejected gracefully

  // Empty traceparent
  headers_->addCopy("traceparent", "");
  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok());

  // Malformed tracestate (should not prevent traceparent extraction)
  headers_->clear();
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");
  headers_->addCopy("tracestate", "invalid=format=value");
  result = Propagator::extract(*trace_context_);
  EXPECT_TRUE(result.ok()); // Should still work without tracestate

  // Malformed baggage (should not prevent other extraction)
  headers_->clear();
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-01");
  headers_->addCopy("baggage", "invalid format");
  result = Propagator::extract(*trace_context_);
  EXPECT_TRUE(result.ok()); // Should still work without baggage
}

// Test preservation of unknown trace flags (W3C spec requirement)
TEST_F(W3CSpecificationComplianceTest, PreserveUnknownTraceFlags) {
  // Test flags with unknown bits set
  headers_->addCopy("traceparent", "00-1234567890abcdef1234567890abcdef-fedcba0987654321-ff");

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << result.status().message();

  const auto& context = result.value();
  EXPECT_EQ(context.traceParent().traceFlags(), "ff");
  EXPECT_TRUE(context.traceParent().isSampled()); // Bit 0 is set

  // Inject and verify flags are preserved
  Propagator::inject(context, *trace_context_);

  auto re_extracted = Propagator::extract(*trace_context_);
  ASSERT_TRUE(re_extracted.ok());
  EXPECT_EQ(re_extracted.value().traceParent().traceFlags(), "ff");
}

// Additional W3C Specification Compliance Tests

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_CaseInsensitiveHeaders) {
  // Test case insensitive header names per HTTP specification
  headers_->addCopy("TRACEPARENT", valid_traceparent);
  headers_->addCopy("TRACESTATE", valid_tracestate);

  EXPECT_TRUE(Propagator::isPresent(*trace_context_));

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().traceParent().toString(), valid_traceparent);
  EXPECT_TRUE(result.value().hasTraceState());
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_MixedCaseHeaders) {
  headers_->addCopy("TraceParent", valid_traceparent);
  headers_->addCopy("TraceState", valid_tracestate);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().traceParent().toString(), valid_traceparent);
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_FutureVersionCompatibility) {
  // Test future version handling (version > 00)
  std::string future_version_traceparent =
      "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01";
  headers_->addCopy("traceparent", future_version_traceparent);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << "Future versions should be accepted per W3C spec";
  EXPECT_EQ(result.value().traceParent().version(), "ff");
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_ZeroTraceIdRejection) {
  // Zero trace ID should be rejected per W3C specification
  std::string zero_trace_id = "00-00000000000000000000000000000000-00f067aa0ba902b7-01";
  headers_->addCopy("traceparent", zero_trace_id);

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok()) << "Zero trace ID should be rejected per W3C specification";
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_ZeroSpanIdRejection) {
  // Zero span ID should be rejected per W3C specification
  std::string zero_span_id = "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-01";
  headers_->addCopy("traceparent", zero_span_id);

  auto result = Propagator::extract(*trace_context_);
  EXPECT_FALSE(result.ok()) << "Zero span ID should be rejected per W3C specification";
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_TracestateOrderPreservation) {
  // Tracestate order must be preserved per W3C specification
  std::string ordered_tracestate = "rojo=00f067aa0ba902b7,congo=t61rcWkgMzE";
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", ordered_tracestate);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(result.value().traceState().toString(), ordered_tracestate);
}

TEST_F(PropagatorTest, W3CTraceContextSpecCompliance_TracestateMaxLength) {
  // Test tracestate length limits per W3C specification (512 chars per vendor)
  std::string long_tracestate =
      "vendor1=" + std::string(500, 'a') + ",vendor2=" + std::string(500, 'b');
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", long_tracestate);

  auto result = Propagator::extract(*trace_context_);
  ASSERT_TRUE(result.ok()) << "Long tracestate should be accepted within limits";
}

TEST_F(PropagatorTest, W3CBaggageSpecCompliance_URLEncodingDecoding) {
  // Test URL encoding/decoding per W3C Baggage specification
  std::string encoded_baggage = "user%20id=alice%20smith,session%3Did=abc%2Bdef";
  headers_->addCopy("baggage", encoded_baggage);

  auto baggage_result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(baggage_result.ok());

  auto user_id = baggage_result.value().get("user id");
  ASSERT_TRUE(user_id.has_value());
  EXPECT_EQ(user_id.value(), "alice smith");

  auto session_id = baggage_result.value().get("session:id");
  ASSERT_TRUE(session_id.has_value());
  EXPECT_EQ(session_id.value(), "abc+def");
}

TEST_F(PropagatorTest, W3CBaggageSpecCompliance_SizeLimits) {
  // Test 8KB size limit enforcement per W3C specification
  std::string large_value(8000, 'x'); // Large but within limit
  std::string oversized_baggage = "key1=" + large_value + ",key2=value2";
  headers_->addCopy("baggage", oversized_baggage);

  auto result = Propagator::extractBaggage(*trace_context_);
  // Should handle size limits gracefully (implementation dependent)
  if (!result.ok()) {
    EXPECT_THAT(result.status().message(), testing::HasSubstr("size"));
  }
}

TEST_F(PropagatorTest, W3CBaggageSpecCompliance_PropertyHandling) {
  // Test baggage member properties per W3C specification
  std::string baggage_with_properties = "userId=alice;version=1.0;sensitive=true,sessionId=xyz123";
  headers_->addCopy("baggage", baggage_with_properties);

  auto result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(result.ok());

  auto user_id = result.value().get("userId");
  ASSERT_TRUE(user_id.has_value());
  EXPECT_EQ(user_id.value(), "alice");
}

TEST_F(PropagatorTest, W3CBaggageSpecCompliance_MalformedHandling) {
  // Test graceful handling of malformed baggage while preserving valid members
  std::string mixed_baggage = "valid=value1,=invalid_key,valid2=value2,invalid=";
  headers_->addCopy("baggage", mixed_baggage);

  auto result = Propagator::extractBaggage(*trace_context_);
  ASSERT_TRUE(result.ok()) << "Should preserve valid baggage members";

  auto valid = result.value().get("valid");
  EXPECT_TRUE(valid.has_value());
  EXPECT_EQ(valid.value(), "value1");

  auto valid2 = result.value().get("valid2");
  EXPECT_TRUE(valid2.has_value());
  EXPECT_EQ(valid2.value(), "value2");
}

TEST_F(PropagatorTest, W3CSpecCompliance_RoundTripConsistency) {
  // Test round-trip consistency per W3C specifications
  headers_->addCopy("traceparent", valid_traceparent);
  headers_->addCopy("tracestate", valid_tracestate);
  headers_->addCopy("baggage", "userId=alice,sessionId=xyz123");

  auto original = Propagator::extract(*trace_context_);
  ASSERT_TRUE(original.ok());

  // Create new trace context for injection
  auto new_headers = Http::RequestHeaderMapImpl::create();
  auto new_trace_context = std::make_unique<Tracing::TraceContextImpl>(*new_headers);

  // Inject and re-extract
  Propagator::inject(original.value(), *new_trace_context);
  auto round_trip = Propagator::extract(*new_trace_context);

  ASSERT_TRUE(round_trip.ok());
  EXPECT_EQ(round_trip.value().traceParent().toString(), original.value().traceParent().toString());
  EXPECT_EQ(round_trip.value().traceState().toString(), original.value().traceState().toString());
}

}

} // namespace
} // namespace W3C
} // namespace Propagators
} // namespace Extensions
} // namespace Envoy
