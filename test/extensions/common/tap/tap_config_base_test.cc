#include <vector>

#include "envoy/config/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/common.pb.h"
#include "envoy/data/tap/v3/wrapper.pb.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/common/tap/tap_config_base.h"

#include "test/mocks/server/server_factory_context.h"
#include "test/test_common/test_runtime.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Tap {
namespace {

// Concrete subclass that exposes the protected constructor for direct test
// instantiation.
class TestableTapConfigBase : public TapConfigBaseImpl {
public:
  TestableTapConfigBase(const envoy::config::tap::v3::TapConfig& proto_config,
                        Server::Configuration::GenericFactoryContext& context)
      : TapConfigBaseImpl(proto_config, /*admin_streamer=*/nullptr, context) {}
};

// Minimal valid TapConfig that uses file_per_tap so no admin streamer is needed.
envoy::config::tap::v3::TapConfig minimalTapConfig() {
  envoy::config::tap::v3::TapConfig proto_config;
  proto_config.mutable_match()->set_any_match(true);
  auto* sink = proto_config.mutable_output_config()->mutable_sinks()->Add();
  sink->mutable_file_per_tap()->set_path_prefix("/tmp/test_tap");
  return proto_config;
}

TEST(BodyBytesToString, All) {
  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_bytes());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_request_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().request_body_chunk().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_http_streamed_trace_segment()->mutable_response_body_chunk()->set_as_bytes(
        "hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.http_streamed_trace_segment().response_body_chunk().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_read()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().read().data().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    // Two read events
    auto* event_r1 = trace.mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
    event_r1->mutable_read()->mutable_data()->set_as_bytes("hello");
    auto* event_r2 = trace.mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
    event_r2->mutable_read()->mutable_data()->set_as_bytes("world");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);

    const auto& socket_trace_events = trace.socket_streamed_trace_segment().events().events();
    const auto& event_h = socket_trace_events.Get(0);
    EXPECT_EQ("hello", event_h.read().data().as_string());
    const auto& event_w = socket_trace_events.Get(1);
    EXPECT_EQ("world", event_w.read().data().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    trace.mutable_socket_streamed_trace_segment()
        ->mutable_event()
        ->mutable_write()
        ->mutable_data()
        ->set_as_bytes("hello");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);
    EXPECT_EQ("hello", trace.socket_streamed_trace_segment().event().write().data().as_string());
  }

  {
    envoy::data::tap::v3::TraceWrapper trace;
    // Two write events
    auto* event_w1 = trace.mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
    event_w1->mutable_write()->mutable_data()->set_as_bytes("hello");
    auto* event_w2 = trace.mutable_socket_streamed_trace_segment()->mutable_events()->add_events();
    event_w2->mutable_write()->mutable_data()->set_as_bytes("world");
    Utility::bodyBytesToString(trace, envoy::config::tap::v3::OutputSink::JSON_BODY_AS_STRING);

    const auto& socket_trace_events = trace.socket_streamed_trace_segment().events().events();
    const auto& event_h = socket_trace_events.Get(0);
    EXPECT_EQ("hello", event_h.write().data().as_string());
    const auto& event_w = socket_trace_events.Get(1);
    EXPECT_EQ("world", event_w.write().data().as_string());
  }
}

TEST(AddBufferToProtoBytes, All) {
  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 5, data, 4, 1);
    EXPECT_EQ("o", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 3, data, 0, 5);
    EXPECT_EQ("hel", body.as_bytes());
    EXPECT_TRUE(body.truncated());
  }

  {
    Buffer::OwnedImpl data("hello");
    envoy::data::tap::v3::Body body;
    Utility::addBufferToProtoBytes(body, 100, data, 0, 5);
    EXPECT_EQ("hello", body.as_bytes());
    EXPECT_FALSE(body.truncated());
  }
}

TEST(TrimSlice, All) {
  std::string slice_mem = "static base slice memory that is long enough";
  void* test_base = static_cast<void*>(&slice_mem[0]);
  {
    std::vector<Buffer::RawSlice> slices;
    Utility::trimSlices(slices, 0, 100);
    EXPECT_TRUE(slices.empty());
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}};
    Utility::trimSlices(slices, 0, 100);

    const std::vector<Buffer::RawSlice> expected{{test_base, 5}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[3]), 2}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 3, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[3]), 2},
                                                 {static_cast<void*>(&slice_mem[0]), 1}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 6, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[5]), 0},
                                                 {static_cast<void*>(&slice_mem[1]), 3}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 0, 0);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[0]), 0},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 0, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[0]), 3},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }

  {
    std::vector<Buffer::RawSlice> slices = {{test_base, 5}, {test_base, 4}};
    Utility::trimSlices(slices, 1, 3);

    const std::vector<Buffer::RawSlice> expected{{static_cast<void*>(&slice_mem[1]), 3},
                                                 {static_cast<void*>(&slice_mem[0]), 0}};
    EXPECT_EQ(expected, slices);
  }
}

TEST(TapConfigBaseImplSampling, ShouldRecordWhenUnset) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  // Unconfigured short-circuits to true without touching the runtime layer.
  EXPECT_CALL(
      context.server_context_.runtime_loader_.snapshot_,
      featureEnabled(testing::_,
                     testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_)))
      .Times(0);
  auto proto = minimalTapConfig();
  // tap_enabled is not set.
  TestableTapConfigBase config(proto, context);

  EXPECT_TRUE(config.shouldRecord());
}

TEST(TapConfigBaseImplSampling, ShouldRecordZeroPercent) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  // 0% sample rate: runtime layer consulted with the (empty) runtime_key and the
  // configured default; mock returns false.
  ON_CALL(context.server_context_.runtime_loader_.snapshot_,
          featureEnabled(absl::string_view(""),
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_)))
      .WillByDefault(testing::Return(false));
  auto proto = minimalTapConfig();
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(0);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  TestableTapConfigBase config(proto, context);

  EXPECT_FALSE(config.shouldRecord());
}

TEST(TapConfigBaseImplSampling, ShouldRecordFullPercent) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  // 100% sample rate: runtime layer consulted with the (empty) runtime_key and
  // the configured default; mock returns true.
  ON_CALL(context.server_context_.runtime_loader_.snapshot_,
          featureEnabled(absl::string_view(""),
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_)))
      .WillByDefault(testing::Return(true));
  auto proto = minimalTapConfig();
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(100);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  TestableTapConfigBase config(proto, context);

  EXPECT_TRUE(config.shouldRecord());
}

TEST(TapConfigBaseImplSampling, ShouldRecordWhenRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.tap_honor_tap_enabled", "false"}});
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto proto = minimalTapConfig();
  // 0% sample rate, but the disabled guard short-circuits enforcement entirely.
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(0);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  TestableTapConfigBase config(proto, context);

  EXPECT_TRUE(config.shouldRecord());
}

TEST(TapConfigBaseImplSampling, ShouldRecordHonorsRuntimeOverride) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  // Verify the runtime key is forwarded as-is and the runtime decision is honored.
  // Default value is 0%, but the runtime layer "override" returns true (simulating an
  // operator setting tap.sampling.test_key to 100).
  ON_CALL(context.server_context_.runtime_loader_.snapshot_,
          featureEnabled("tap.sampling.test_key",
                         testing::Matcher<const envoy::type::v3::FractionalPercent&>(testing::_)))
      .WillByDefault(testing::Return(true));
  auto proto = minimalTapConfig();
  proto.mutable_tap_enabled()->set_runtime_key("tap.sampling.test_key");
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(0);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::HUNDRED);
  TestableTapConfigBase config(proto, context);

  // Runtime override wins over default (0%).
  EXPECT_TRUE(config.shouldRecord());
}

// Test sink that captures emitted TraceWrappers verbatim instead of writing
// them anywhere. Used to verify per-stream stamping behavior.
class CapturingSink : public Sink {
public:
  PerTapSinkHandlePtr
  createPerTapSinkHandle(uint64_t,
                         envoy::config::tap::v3::OutputSink::OutputSinkTypeCase) override {
    return std::make_unique<Handle>(*this);
  }

  std::vector<envoy::data::tap::v3::TraceWrapper> captured_;

private:
  struct Handle : public PerTapSinkHandle {
    explicit Handle(CapturingSink& parent) : parent_(parent) {}
    void submitTrace(TraceWrapperPtr&& trace, envoy::config::tap::v3::OutputSink::Format) override {
      parent_.captured_.push_back(*trace);
    }
    CapturingSink& parent_;
  };
};

// Subclass that takes an admin streamer, so the streaming_admin sink can be
// used in tests without spinning up real admin infrastructure.
class TestableTapConfigBaseWithSink : public TapConfigBaseImpl {
public:
  TestableTapConfigBaseWithSink(const envoy::config::tap::v3::TapConfig& proto, Sink* sink,
                                Server::Configuration::GenericFactoryContext& ctx)
      : TapConfigBaseImpl(proto, sink, ctx) {}
};

// Builds a config that routes traces to the streaming_admin sink (so the
// admin_streamer passed to the ctor is consulted) with JSON_BODY_AS_BYTES
// format.
envoy::config::tap::v3::TapConfig streamingAdminTapConfig() {
  envoy::config::tap::v3::TapConfig proto;
  proto.mutable_match()->set_any_match(true);
  auto* sink = proto.mutable_output_config()->mutable_sinks()->Add();
  sink->mutable_streaming_admin();
  sink->set_format(envoy::config::tap::v3::OutputSink::JSON_BODY_AS_BYTES);
  return proto;
}

TEST(TapConfigBaseImplStamping, ConfiguredSampleRateStampedOnFirstSegmentOnly) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto proto = streamingAdminTapConfig();
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(1);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  CapturingSink capturing;
  TestableTapConfigBaseWithSink config(proto, &capturing, context);

  auto handle = config.createPerTapSinkHandleManager(/*trace_id=*/42);

  auto first = makeTraceWrapper();
  first->mutable_http_buffered_trace();
  handle->submitTrace(std::move(first));

  auto second = makeTraceWrapper();
  second->mutable_http_streamed_trace_segment();
  handle->submitTrace(std::move(second));

  ASSERT_EQ(2u, capturing.captured_.size());
  EXPECT_TRUE(capturing.captured_[0].has_configured_sample_rate());
  EXPECT_EQ(1u, capturing.captured_[0].configured_sample_rate().numerator());
  EXPECT_EQ(envoy::type::v3::FractionalPercent::TEN_THOUSAND,
            capturing.captured_[0].configured_sample_rate().denominator());
  EXPECT_FALSE(capturing.captured_[1].has_configured_sample_rate());
}

TEST(TapConfigBaseImplStamping, ConfiguredSampleRateStampedOnSocketTraces) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto proto = streamingAdminTapConfig();
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(1);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  CapturingSink capturing;
  TestableTapConfigBaseWithSink config(proto, &capturing, context);

  auto handle = config.createPerTapSinkHandleManager(/*trace_id=*/42);

  // The transport socket tap consults shouldRecord() just like the HTTP filter, so
  // socket traces carry the stamp on the first emitted segment only.
  auto buffered = makeTraceWrapper();
  buffered->mutable_socket_buffered_trace();
  handle->submitTrace(std::move(buffered));

  auto streamed = makeTraceWrapper();
  streamed->mutable_socket_streamed_trace_segment();
  handle->submitTrace(std::move(streamed));

  ASSERT_EQ(2u, capturing.captured_.size());
  EXPECT_TRUE(capturing.captured_[0].has_configured_sample_rate());
  EXPECT_EQ(1u, capturing.captured_[0].configured_sample_rate().numerator());
  EXPECT_FALSE(capturing.captured_[1].has_configured_sample_rate());
}

TEST(TapConfigBaseImplStamping, ConfiguredSampleRateNotStampedWhenGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.tap_honor_tap_enabled", "false"}});
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto proto = streamingAdminTapConfig();
  proto.mutable_tap_enabled()->mutable_default_value()->set_numerator(1);
  proto.mutable_tap_enabled()->mutable_default_value()->set_denominator(
      envoy::type::v3::FractionalPercent::TEN_THOUSAND);

  CapturingSink capturing;
  TestableTapConfigBaseWithSink config(proto, &capturing, context);

  // With the guard disabled neither sampling nor stamping is applied, restoring the
  // pre-implementation trace output.
  auto handle = config.createPerTapSinkHandleManager(/*trace_id=*/42);
  auto trace = makeTraceWrapper();
  trace->mutable_http_buffered_trace();
  handle->submitTrace(std::move(trace));

  ASSERT_EQ(1u, capturing.captured_.size());
  EXPECT_FALSE(capturing.captured_[0].has_configured_sample_rate());
}

TEST(TapConfigBaseImplStamping, ConfiguredSampleRateAbsentWhenNotConfigured) {
  testing::NiceMock<Server::Configuration::MockGenericFactoryContext> context;
  auto proto = streamingAdminTapConfig();
  // tap_enabled is unset.

  CapturingSink capturing;
  TestableTapConfigBaseWithSink config(proto, &capturing, context);

  auto handle = config.createPerTapSinkHandleManager(/*trace_id=*/42);
  auto trace = makeTraceWrapper();
  trace->mutable_http_buffered_trace();
  handle->submitTrace(std::move(trace));

  ASSERT_EQ(1u, capturing.captured_.size());
  EXPECT_FALSE(capturing.captured_[0].has_configured_sample_rate());
}

} // namespace
} // namespace Tap
} // namespace Common
} // namespace Extensions
} // namespace Envoy
