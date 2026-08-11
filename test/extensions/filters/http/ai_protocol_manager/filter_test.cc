#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;
using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

class AiProtocolManagerFilterTest : public testing::Test {
public:
  AiProtocolManagerFilterTest() {
    // Capture the upstream watermark callbacks the filter registers so tests can
    // simulate upstream back-pressure.
    ON_CALL(callbacks_, addUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks& cb) { watermark_cb_ = &cb; }));
    ON_CALL(callbacks_, removeUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks&) { watermark_cb_ = nullptr; }));
    createFilter();
    // The in-memory buffer delivers completions via dispatcher.post(). Capture
    // those callbacks so the test can run the event loop deterministically.
    ON_CALL(callbacks_.dispatcher_, post(testing::_))
        .WillByDefault(Invoke([this](Event::PostCb cb) { posted_.push_back(std::move(cb)); }));
    ON_CALL(callbacks_, injectDecodedDataToFilterChain(testing::_, testing::_))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          injected_.add(data);
          injected_end_stream_ = end_stream;
          ++inject_calls_;
          // Simulate upstream back-pressure arising mid-replay: when configured,
          // raise the watermark right after the Nth injected chunk.
          if (watermark_cb_ != nullptr && inject_calls_ == raise_watermark_at_inject_) {
            watermark_cb_->onAboveWriteBufferHighWatermark();
          }
        }));
    // Capture continueDecoding() so trailer-terminated streams can assert the
    // held trailers are released after the replayed body.
    ON_CALL(callbacks_, continueDecoding()).WillByDefault(Invoke([this]() { ++continue_calls_; }));
    // Capture local replies so rejection tests can assert code and details.
    ON_CALL(callbacks_, sendLocalReply(testing::_, testing::_, testing::_, testing::_, testing::_))
        .WillByDefault(Invoke([this](Http::Code code, absl::string_view,
                                     std::function<void(Http::ResponseHeaderMap&)>,
                                     const std::optional<Grpc::Status::GrpcStatus>,
                                     absl::string_view details) {
          local_reply_code_ = code;
          local_reply_details_ = std::string(details);
          ++local_reply_calls_;
        }));
  }

  // A test wanting a different filter-level config calls this again first.
  void createFilter(bool best_effort_parsing = false) {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
    proto.set_best_effort_parsing(best_effort_parsing);
    filter_ = std::make_unique<AiProtocolManagerFilter>(
        factory_, std::make_shared<const FilterConfig>(proto, *stats_store_.rootScope()));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  // decodeHeaders() for a stream the filter is expected to engage on. Engaging
  // builds the BufferManager, which claims a SchedulableCallback, so the mock is
  // created here and only here -- a pass-through stream must not create one, or
  // its expectation goes unsatisfied.
  Http::FilterHeadersStatus decodeHeadersEngaging() {
    replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
    Http::TestRequestHeaderMapImpl headers = requestHeaders();
    return filter_->decodeHeaders(headers, /*end_stream=*/false);
  }

  // Engages the filter where the payload is not what the test is about: best
  // effort offloads and replays whether or not the body parses.
  void engageIgnoringPayload() {
    createFilter(/*best_effort_parsing=*/true);
    ASSERT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);
  }

  // Attaches a per-route config, declaring the route an AI endpoint.
  void setRouteConfig(bool normalize = false) {
    PerRouteProto proto;
    proto.set_schema(PerRouteProto::OPENAI_CHAT_COMPLETIONS);
    proto.set_normalize(normalize);
    route_config_ = std::make_unique<RouteConfig>(proto);
    ON_CALL(callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(testing::Return(route_config_.get()));
  }

  static Http::TestRequestHeaderMapImpl requestHeaders() {
    return Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/chat/completions"}};
  }

  // The manager the filter owns requires onDestroy() before destruction (see
  // buffer_manager.h); Envoy guarantees that in production, so drive it here to
  // detach decode_manager_ before ~filter_ frees it. Idempotent, so tests that
  // already called onDestroy() are fine.
  void TearDown() override { filter_->onDestroy(); }

  // Run all posted callbacks, including ones enqueued while draining.
  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  std::deque<Event::PostCb> posted_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  InMemoryExternalBufferFactory factory_;
  FilterConfigSharedPtr config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::UpstreamWatermarkCallbacks* watermark_cb_{};
  // Owned by the manager the filter builds; present so createSchedulableCallback()
  // returns a usable callback during construction.
  NiceMock<Event::MockSchedulableCallback>* replay_cb_{nullptr};
  std::unique_ptr<RouteConfig> route_config_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;

  Buffer::OwnedImpl injected_;
  bool injected_end_stream_{false};
  int inject_calls_{0};
  int continue_calls_{0};
  int raise_watermark_at_inject_{0}; // 0 = never.
  int local_reply_calls_{0};
  std::optional<Http::Code> local_reply_code_;
  std::string local_reply_details_;
};

// With a payload to inspect, iteration pauses so the rest of the chain does not
// see the headers until it is offloaded.
TEST_F(AiProtocolManagerFilterTest, HoldsHeadersWhenBodyFollows) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);
}

// A headers-only request has no payload to inspect, so the headers flow
// immediately; holding them would deadlock since no body drives the release.
TEST_F(AiProtocolManagerFilterTest, PassesHeadersOnlyRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/healthz"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
}

// Headers held during decodeHeaders() are released when replay begins: the body
// is offloaded while iteration is paused, then injected back to the chain.
TEST_F(AiProtocolManagerFilterTest, ReleasesHeldHeadersOnReplay) {
  engageIgnoringPayload();

  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// The body is offloaded chunk-by-chunk and replayed verbatim once end_stream is
// seen, with end_stream propagated on the final injected frame.
TEST_F(AiProtocolManagerFilterTest, OffloadsAndReplaysBody) {
  engageIgnoringPayload();
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(filter_->decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Nothing has been replayed yet: writes and replay are all asynchronous.
  EXPECT_EQ(inject_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

// A body that arrives in a single end_stream frame is still round-tripped.
TEST_F(AiProtocolManagerFilterTest, SingleFrameBody) {
  engageIgnoringPayload();
  Buffer::OwnedImpl body("{}");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{}");
}

// An empty terminal frame produces an empty end_stream marker downstream. It
// issues no write, so replay is scheduled (not started reentrantly from
// decodeData) and runs on the next event-loop iteration.
TEST_F(AiProtocolManagerFilterTest, EmptyBody) {
  engageIgnoringPayload();
  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_->decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);

  EXPECT_EQ(inject_calls_, 0);
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();

  EXPECT_EQ(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// A payload larger than the replay chunk size is streamed back in multiple
// bounded frames and reassembles to the original bytes.
TEST_F(AiProtocolManagerFilterTest, LargePayloadReplayedInChunks) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize (64KiB)
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GT(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Destroying the filter mid-flight cancels pending callbacks; no replay occurs.
TEST_F(AiProtocolManagerFilterTest, DestroyBeforeReplay) {
  engageIgnoringPayload();
  Buffer::OwnedImpl body("payload");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  filter_->onDestroy();
  drain();

  EXPECT_EQ(inject_calls_, 0);
}

// The filter registers for upstream watermark callbacks so it can observe
// upstream back-pressure during replay.
TEST_F(AiProtocolManagerFilterTest, RegistersUpstreamWatermarkCallbacks) {
  engageIgnoringPayload();
  EXPECT_NE(watermark_cb_, nullptr);
}

// When the downstream chain (toward upstream) is backed up before replay starts,
// no data is injected until the back-pressure is released.
TEST_F(AiProtocolManagerFilterTest, ReplayPausesUnderUpstreamBackPressure) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Upstream signals back-pressure before replay begins.
  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();

  // Draining completes the write and starts replay, but replay is paused: no
  // chunk is read or injected while the high watermark is held.
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Releasing back-pressure schedules the resume off the watermark callback stack
  // (deferred so we never read/inject reentrantly from within it); firing the
  // continuation runs replay to completion.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

// Back-pressure arising mid-replay (the upstream fills as we inject) halts the
// synchronous read loop, and replay resumes when it clears.
TEST_F(AiProtocolManagerFilterTest, ReplayResumesMidStream) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  // Upstream backs up right after the first replayed chunk; the loop must then
  // stop until it is released.
  raise_watermark_at_inject_ = 1;
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Append completes, replay injects one chunk, then pauses on the watermark
  // raised during that inject.
  drain();
  EXPECT_EQ(inject_calls_, 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_LT(injected_.length(), big.size());

  // Release back-pressure; the resume is scheduled off the watermark callback
  // stack (deferred to avoid reentrant read/inject) and the continuation runs
  // replay to completion.
  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection); replay resumes only
// after a matching number of low-watermark callbacks.
TEST_F(AiProtocolManagerFilterTest, NestedWatermarksRequireBalancedRelease) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x');
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();
  watermark_cb_->onAboveWriteBufferHighWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);

  // One release is not enough to resume.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Balanced release schedules the resume off the watermark callback stack; the
  // continuation runs replay to completion.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// A request whose body is terminated by trailers (last data frame has
// end_stream=false; the trailers carry END_STREAM) is not stuck: the body is
// replayed (final frame end_stream=false) and the held trailers are released
// via continueDecoding() once the body has been injected.
TEST_F(AiProtocolManagerFilterTest, TrailerTerminatedStream) {
  engageIgnoringPayload();
  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
  EXPECT_EQ(continue_calls_, 1);
}

// A trailer-only request (headers + trailers, no body) has nothing to offload, so
// the trailers flow immediately (Continue) and nothing is injected or held.
TEST_F(AiProtocolManagerFilterTest, TrailersWithoutBody) {
  engageIgnoringPayload();

  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  drain();

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

// ---------------------------------------------------------------------------
// Encode-path (response handling) tests. The encode path is independent of the
// decode-path offload machinery: no decoder callbacks are needed.

class AiProtocolManagerFilterResponseTest : public testing::Test {
public:
  // Build a filter whose config enables response handling (optionally from
  // yaml overriding the ResponseHandling fields).
  void setup(const std::string& response_handling_yaml = "{}") {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    TestUtility::loadFromYaml(fmt::format("response_handling: {}", response_handling_yaml),
                              proto_config);
    setupWithProto(proto_config);
  }

  void
  setupWithProto(const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
                     proto_config) {
    metadata_writes_.clear();
    typed_metadata_writes_.clear();
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
    filter_ = std::make_unique<AiProtocolManagerFilter>(factory_, config_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    ON_CALL(encoder_callbacks_.stream_info_, setDynamicMetadata(testing::_, testing::_))
        .WillByDefault(Invoke([this](const std::string& ns, const Protobuf::Struct& value) {
          metadata_writes_.emplace_back(ns, value);
        }));
    ON_CALL(encoder_callbacks_.stream_info_, setDynamicTypedMetadata(testing::_, testing::_))
        .WillByDefault(Invoke([this](const std::string& ns, const Protobuf::Any& value) {
          typed_metadata_writes_.emplace_back(ns, value);
        }));
  }

  void TearDown() override {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
  }

  // Send a 200 response header with the given content type.
  void sendHeaders(absl::string_view content_type, absl::string_view status = "200") {
    Http::TestResponseHeaderMapImpl headers{{":status", std::string(status)},
                                            {"content-type", std::string(content_type)}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  }

  // Send one response body frame; asserts pass-through leaves the bytes intact.
  void sendData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    EXPECT_EQ(filter_->encodeData(data, end_stream), Http::FilterDataStatus::Continue);
    EXPECT_EQ(data.toString(), body); // Observe-only: the response is never modified.
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_store_, "ai_protocol_manager." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  const Protobuf::Struct* singleMetadataWrite(const std::string& expected_namespace) {
    if (metadata_writes_.size() != 1 || metadata_writes_[0].first != expected_namespace) {
      return nullptr;
    }
    return &metadata_writes_[0].second;
  }

  // The authoritative typed record published alongside the Struct projection.
  std::optional<envoy::data::ai::v3::TokenUsage>
  singleTypedWrite(const std::string& expected_namespace) {
    if (typed_metadata_writes_.size() != 1 ||
        typed_metadata_writes_[0].first != expected_namespace) {
      return std::nullopt;
    }
    envoy::data::ai::v3::TokenUsage typed;
    if (!typed_metadata_writes_[0].second.UnpackTo(&typed)) {
      return std::nullopt;
    }
    return typed;
  }

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  InMemoryExternalBufferFactory factory_;
  FilterConfigSharedPtr config_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;
  std::vector<std::pair<std::string, Protobuf::Struct>> metadata_writes_;
  std::vector<std::pair<std::string, Protobuf::Any>> typed_metadata_writes_;
};

// An SSE response is teed, usage extracted, and published at end of stream
// under the default namespace, while every frame passes through untouched.
TEST_F(AiProtocolManagerFilterResponseTest, SseUsagePublishedAtEndOfStream) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
           "\"choices\":[{\"delta\":{\"content\":\"hi\"}}],\"usage\":null}\n\n",
           false);
  sendData("data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\",\"choices\":[],"
           "\"usage\":{\"prompt_tokens\":19,\"completion_tokens\":10,\"total_tokens\":29}}\n\n",
           false);
  sendData("data: [DONE]\n\n", true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  const auto& fields = metadata->fields();
  EXPECT_EQ(fields.at("api_format").string_value(), "openai");
  EXPECT_EQ(fields.at("model").string_value(), "gpt-4o");
  EXPECT_EQ(fields.at("input_tokens").number_value(), 19);
  EXPECT_EQ(fields.at("output_tokens").number_value(), 10);
  EXPECT_EQ(fields.at("total_tokens").number_value(), 29);
  EXPECT_EQ(fields.at("extraction_status").string_value(), "complete");
  EXPECT_EQ(fields.count("reported_total_tokens"), 0);
  EXPECT_EQ(counterValue("token_usage_found"), 1);

  // The authoritative typed record carries the same values with full uint64
  // presence semantics.
  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->api_format(), envoy::data::ai::v3::TokenUsage::OPENAI);
  EXPECT_EQ(typed->model(), "gpt-4o");
  EXPECT_EQ(typed->input_tokens().value(), 19);
  EXPECT_EQ(typed->output_tokens().value(), 10);
  EXPECT_EQ(typed->total_tokens().value(), 29);
  EXPECT_FALSE(typed->has_reported_total_tokens());
  EXPECT_FALSE(typed->has_tool_use_input_tokens());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::COMPLETE);
}

// A degraded stream -- some usage extracted, then an event over the cap --
// still publishes at clean end of stream, but flags the counts as partial so
// a consumer can tell the emitted values may be stale (e.g. an earlier
// cumulative snapshot) rather than final.
TEST_F(AiProtocolManagerFilterResponseTest, PartialUsageReportsExtractionStatus) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  proto_config.mutable_response_handling()->mutable_max_event_size()->set_value(256);
  setupWithProto(proto_config);
  sendHeaders("text/event-stream");
  // An early cumulative Gemini snapshot extracts normally.
  sendData("data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"a\"}]}}],"
           "\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
           "\"totalTokenCount\":22}}\n\n",
           false);
  // The larger final snapshot exceeds max_event_size and is skipped.
  sendData("data: {\"pad\":\"" + std::string(500, 'x') +
               "\",\"usageMetadata\":{\"promptTokenCount\":6,"
               "\"candidatesTokenCount\":149,\"totalTokenCount\":155}}\n\n",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 22); // Stale snapshot.
  EXPECT_EQ(metadata->fields().at("extraction_status").string_value(), "partial");
  EXPECT_EQ(counterValue("sse_event_too_large"), 1);
}

// A JSON response whose content-length already exceeds the inspection cap
// never constructs a handler: nothing is buffered only to be discarded.
TEST_F(AiProtocolManagerFilterResponseTest, ContentLengthOverCapSkipsExtraction) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  proto_config.mutable_response_handling()->mutable_max_inspected_body_size()->set_value(64);
  setupWithProto(proto_config);
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/json"}, {"content-length", "100"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  sendData(std::string(100, 'x'), true); // Passes through untouched, uninspected.
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("response_body_too_large"), 1);
  EXPECT_EQ(counterValue("response_parse_error"), 0); // Never buffered or parsed.
}

// Complete extraction failure still publishes a status-only record: per-stream
// consumers must be able to distinguish "Envoy failed to extract" from "the
// provider supplied no usage" (which publishes nothing). The failing event
// here is an OpenAI Responses terminal event above the production 1MiB
// default cap -- a normal operational failure mode for long generations, not
// only an adversarial one.
TEST_F(AiProtocolManagerFilterResponseTest, ExtractionFailurePublishesStatusOnlyRecord) {
  setup(); // Default caps.
  sendHeaders("text/event-stream");
  // Detection locks from a small skipped-nothing chunk first.
  sendData("data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
           "\"choices\":[{\"delta\":{\"content\":\"hi\"}}],\"usage\":null}\n\n",
           false);
  // The only usage-bearing event exceeds the 1MiB default cap.
  sendData("data: {\"object\":\"chat.completion.chunk\",\"pad\":\"" +
               std::string(1024 * 1024 + 4096, 'x') +
               "\",\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
               "\"total_tokens\":3}}\n\n",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("extraction_status").string_value(), "partial");
  EXPECT_EQ(metadata->fields().at("api_format").string_value(), "openai");
  EXPECT_EQ(metadata->fields().at("model").string_value(), "gpt-4o");
  EXPECT_EQ(metadata->fields().count("total_tokens"), 0); // No counts recovered.
  EXPECT_EQ(counterValue("token_usage_partial"), 1);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::PARTIAL);
  EXPECT_EQ(typed->api_format(), envoy::data::ai::v3::TokenUsage::OPENAI);
  EXPECT_FALSE(typed->has_input_tokens());
}

// A clean stream that simply carries no usage still publishes nothing.
TEST_F(AiProtocolManagerFilterResponseTest, AbsentUsagePublishesNothing) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{}}],"
           "\"usage\":null}\n\ndata: [DONE]\n\n",
           true);
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_EQ(counterValue("token_usage_partial"), 0);
}

// An eligible response ending at the headers counts as missing, exactly like
// one ending in an empty terminal DATA frame: stats do not depend on codec
// framing of an empty body.
TEST_F(AiProtocolManagerFilterResponseTest, EligibleHeadersOnlyResponseCountsMissing) {
  setup();
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_TRUE(metadata_writes_.empty());
}

// A syntactically valid final document whose known count fields are unusable
// must not leave the earlier snapshot published as complete.
TEST_F(AiProtocolManagerFilterResponseTest, MalformedPresentUsageFieldMarksPartial) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
           "\"totalTokenCount\":22}}\n\n",
           false);
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":\"149\","
           "\"totalTokenCount\":\"155\"}}\n\n",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 22); // Stale snapshot.
  EXPECT_EQ(metadata->fields().at("extraction_status").string_value(), "partial");
  EXPECT_EQ(counterValue("malformed_usage_field"), 1);
  EXPECT_EQ(counterValue("token_usage_partial"), 1);
}

// With the filter installed in both chains (allowed by the API), the first
// publication owns the namespace: StreamInfo merges Structs per namespace, so
// a second write would blend two observations into one hybrid record.
TEST_F(AiProtocolManagerFilterResponseTest, DuplicatePublicationSkipped) {
  setup();
  // Simulate the upstream installation having already published.
  Protobuf::Struct prior;
  (*prior.mutable_fields())["model"].set_string_value("model-a");
  (*prior.mutable_fields())["total_tokens"].set_number_value(100);
  (*encoder_callbacks_.stream_info_.metadata_.mutable_filter_metadata())["envoy.ai.token_usage"] =
      prior;

  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"usage\":{\"prompt_tokens\":3,"
           "\"completion_tokens\":4,\"total_tokens\":7}}",
           true);

  EXPECT_TRUE(metadata_writes_.empty()); // No second write, no hybrid merge.
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_duplicate"), 1);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
}

// The provider-reported total is surfaced separately when it disagrees with
// the canonical input + output sum; total_tokens stays internally consistent.
TEST_F(AiProtocolManagerFilterResponseTest, InconsistentProviderTotalSurfaced) {
  setup();
  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"model\":\"gpt-4o\","
           "\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":4,\"total_tokens\":100}}",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 7);
  EXPECT_EQ(metadata->fields().at("reported_total_tokens").number_value(), 100);
  EXPECT_EQ(metadata->fields().at("extraction_status").string_value(), "complete");
  EXPECT_EQ(counterValue("token_usage_total_mismatch"), 1);
}

// Anthropic named-event stream: input from message_start, cumulative output
// from the last message_delta, total computed at finalize.
TEST_F(AiProtocolManagerFilterResponseTest, SseAnthropicComputedTotal) {
  setup();
  sendHeaders("text/event-stream; charset=utf-8");
  sendData("event: message_start\n"
           "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5\","
           "\"usage\":{\"input_tokens\":2679,\"output_tokens\":3}}}\n\n",
           false);
  sendData("event: message_delta\n"
           "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
           "\"usage\":{\"output_tokens\":15}}\n\n"
           "event: message_stop\ndata: {\"type\":\"message_stop\"}\n\n",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("input_tokens").number_value(), 2679);
  EXPECT_EQ(metadata->fields().at("output_tokens").number_value(), 15);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 2694);
  EXPECT_EQ(metadata->fields().at("api_format").string_value(), "anthropic");
}

// A JSON body (Gemini generateContent) is parsed at end of stream.
TEST_F(AiProtocolManagerFilterResponseTest, JsonBodyGemini) {
  setup();
  sendHeaders("application/json; charset=utf-8");
  sendData("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hi\"}]},"
           "\"finishReason\":\"STOP\"}],"
           "\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":149,"
           "\"totalTokenCount\":167,\"thoughtsTokenCount\":12},"
           "\"modelVersion\":\"gemini-2.5-flash\"}",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("input_tokens").number_value(), 6);
  // Canonical inclusive output: 149 candidates + 12 thoughts.
  EXPECT_EQ(metadata->fields().at("output_tokens").number_value(), 161);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 167);
  EXPECT_EQ(metadata->fields().at("reasoning_tokens").number_value(), 12);
  EXPECT_EQ(metadata->fields().at("api_format").string_value(), "gemini");
  EXPECT_EQ(metadata->fields().at("model").string_value(), "gemini-2.5-flash");
}

// Reviewer merge-gate case: an ordinary long OpenAI Responses generation
// embeds the complete response object in its terminal lifecycle event. Under
// the default configuration a ~64KiB response.completed event must still
// yield usage (the old 16KiB default silently dropped it).
TEST_F(AiProtocolManagerFilterResponseTest, LargeOpenAiResponsesTerminalEventDefaultConfig) {
  setup();
  sendHeaders("text/event-stream");
  sendData("event: response.output_text.delta\n"
           "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\n\n",
           false);
  const std::string big_output(64 * 1024, 'x');
  sendData("event: response.completed\n"
           "data: {\"type\":\"response.completed\",\"response\":{\"object\":\"response\","
           "\"status\":\"completed\",\"model\":\"gpt-5.4\","
           "\"output\":[{\"type\":\"message\",\"content\":[{\"type\":\"output_text\","
           "\"text\":\"" +
               big_output +
               "\"}]}],"
               "\"usage\":{\"input_tokens\":37,\"output_tokens\":21000,"
               "\"total_tokens\":21037}}}\n\n",
           true);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("input_tokens").number_value(), 37);
  EXPECT_EQ(metadata->fields().at("output_tokens").number_value(), 21000);
  EXPECT_EQ(counterValue("sse_event_too_large"), 0);
  EXPECT_EQ(counterValue("token_usage_found"), 1);
}

// A stream ending in trailers (no end_stream data frame) finalizes there.
TEST_F(AiProtocolManagerFilterResponseTest, TrailersFinalize) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"type\":\"message_delta\",\"usage\":{\"input_tokens\":5,"
           "\"output_tokens\":7}}\n\n",
           false);
  Http::TestResponseTrailerMapImpl trailers{{"grpc-status", "0"}};
  EXPECT_EQ(filter_->encodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  EXPECT_NE(singleMetadataWrite("envoy.ai.token_usage"), nullptr);
}

// A JSON response whose body ends via trailers (no end_stream data frame) is
// still parsed: end-of-stream reaches the handler through encodeTrailers().
TEST_F(AiProtocolManagerFilterResponseTest, JsonResponseEndingInTrailers) {
  setup();
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}",
           /*end_stream=*/false);
  Http::TestResponseTrailerMapImpl trailers{{"grpc-status", "0"}};
  EXPECT_EQ(filter_->encodeTrailers(trailers), Http::FilterTrailersStatus::Continue);

  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("input_tokens").number_value(), 5);
  EXPECT_EQ(metadata->fields().at("output_tokens").number_value(), 7);
  EXPECT_EQ(counterValue("token_usage_found"), 1);
}

// End-of-stream arriving as an empty terminal data frame (a common Envoy
// framing) still finalizes the handler.
TEST_F(AiProtocolManagerFilterResponseTest, EmptyTerminalDataFrameFinalizes) {
  setup();
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":3,\"output_tokens\":4}}",
           /*end_stream=*/false);
  sendData("", /*end_stream=*/true);
  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("total_tokens").number_value(), 7);
}

// A stream that resets before end-of-stream publishes nothing: no metadata,
// no found/missing accounting.
TEST_F(AiProtocolManagerFilterResponseTest, ResetDoesNotPublish) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"type\":\"message_delta\",\"usage\":{\"input_tokens\":5,"
           "\"output_tokens\":7}}\n\n",
           /*end_stream=*/false);
  filter_->onDestroy();
  filter_.reset();
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

// Streams that legitimately carry no usage produce the missing stat and no
// metadata.
TEST_F(AiProtocolManagerFilterResponseTest, UsageAbsentCountsMissing) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{}}],"
           "\"usage\":null}\n\ndata: [DONE]\n\n",
           true);
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
}

// A configured format pins extraction for shapes auto-detection cannot place.
TEST_F(AiProtocolManagerFilterResponseTest, ExplicitApiFormatConfig) {
  setup("{token_usage: {api_format: ANTHROPIC}}");
  sendHeaders("application/json");
  sendData("{\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}", true);
  const auto* metadata = singleMetadataWrite("envoy.ai.token_usage");
  ASSERT_NE(metadata, nullptr);
  EXPECT_EQ(metadata->fields().at("api_format").string_value(), "anthropic");
}

// A configured namespace overrides the default.
TEST_F(AiProtocolManagerFilterResponseTest, CustomMetadataNamespace) {
  setup("{metadata_namespace: custom.ns}");
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":1,\"output_tokens\":2}}", true);
  EXPECT_NE(singleMetadataWrite("custom.ns"), nullptr);
}

// Non-2xx responses, unsupported content types, and headers-only responses are
// never inspected: no handler, no metadata, no stats.
TEST_F(AiProtocolManagerFilterResponseTest, UninspectedResponses) {
  setup();
  sendHeaders("text/event-stream", "502");
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":1}}\n\n", true);
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 0);

  setup();
  sendHeaders("text/plain");
  sendData("hello", true);
  EXPECT_TRUE(metadata_writes_.empty());

  setup();
  Http::TestResponseHeaderMapImpl headers{{":status", "204"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
  EXPECT_TRUE(metadata_writes_.empty());
}

// Compressed responses cannot be inspected: any non-identity content-encoding
// skips extraction with a dedicated stat (the body must be decompressed before
// this filter to enable extraction).
TEST_F(AiProtocolManagerFilterResponseTest, CompressedResponseSkipped) {
  setup();
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/json"}, {"content-encoding", "gzip"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  // String juxtaposition ends the \x00 escape before 'c' (a hex digit).
  sendData("\x1f\x8b\x08\x00"
           "compressed-bytes",
           true);
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("unsupported_content_encoding"), 1);
  EXPECT_EQ(counterValue("response_parse_error"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

// Content-Encoding is list-valued and repeatable: a non-identity coding
// anywhere disqualifies the response; identity-only variants (with optional
// whitespace) do not; and an encoding on a never-eligible content type counts
// nothing.
TEST_F(AiProtocolManagerFilterResponseTest, ContentEncodingMatrix) {
  // Duplicate header entries: identity then gzip.
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", "identity"},
                                            {"content-encoding", "gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{}", true);
    EXPECT_TRUE(metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 1);
  }
  // Comma-separated codings in one entry.
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", "identity, gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{}", true);
    EXPECT_TRUE(metadata_writes_.empty());
    // The fixture's stats store persists across setup() calls: cumulative.
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 2);
  }
  // Identity-only, repeated and with optional whitespace: extraction proceeds.
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", " identity , identity "},
                                            {"content-encoding", "identity"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}", true);
    EXPECT_NE(singleMetadataWrite("envoy.ai.token_usage"), nullptr);
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 2); // Unchanged.
  }
  // Empty list elements are malformed and disqualify the response.
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", "identity,,identity"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{}", true);
    EXPECT_TRUE(metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 3);
  }
  // Compressed but never-eligible content type: no handler and no stat.
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{
        {":status", "200"}, {"content-type", "text/plain"}, {"content-encoding", "gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("hello", true);
    EXPECT_TRUE(metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 3); // Unchanged.
  }
}

// Without response_handling config the encode path is fully inert.
TEST_F(AiProtocolManagerFilterResponseTest, DisabledWithoutConfig) {
  setupWithProto(envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager());
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
           "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n"
           "data: [DONE]\n\n",
           true);
  EXPECT_TRUE(metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

// A declared endpoint's payload is parsed as it is offloaded and still replayed
// downstream byte for byte: parsing observes the payload, it does not rewrite it.
TEST_F(AiProtocolManagerFilterTest, ParsesDeclaredEndpointPayloadAndReplaysItVerbatim) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  const std::string payload = R"({"model":"gpt-4","stream":true,"max_tokens":256})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), payload);
  EXPECT_TRUE(injected_end_stream_);
}

// Malformed JSON is answered with a 400 and never reaches the upstream.
TEST_F(AiProtocolManagerFilterTest, RejectsMalformedJson) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_details_, "ai_protocol_manager_invalid_json");
  EXPECT_EQ(inject_calls_, 0);
}

// Where Envoy and the backend could otherwise read the same body differently.
TEST_F(AiProtocolManagerFilterTest, RejectsDuplicateKeys) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"a","model":"b"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// Incomplete rather than malformed; caught when the terminal frame closes.
TEST_F(AiProtocolManagerFilterTest, RejectsTruncatedJson) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4")");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// A stream whose framing promised a body but delivered none -- a chunked request
// with only the terminating chunk, or an empty terminal DATA frame -- carries no
// payload to validate, so it passes through just like a request that ended on
// its headers. A GET reaches the filter this way.
TEST_F(AiProtocolManagerFilterTest, EmptyBodyOnDeclaredEndpointIsPassedThrough) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_->decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// Only a stream with no body at all is exempt. Once bytes have arrived they are
// a payload, and an empty terminal frame closes it rather than excusing it.
TEST_F(AiProtocolManagerFilterTest, RejectsTruncatedJsonEndedByEmptyTerminalFrame) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4")");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl terminal;
  EXPECT_EQ(filter_->decodeData(terminal, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// Parsing before offloading means a bad byte early in a large upload fails the
// request immediately, and the rest of the client's data is dropped.
TEST_F(AiProtocolManagerFilterTest, RejectsMalformedJsonMidUpload) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl bad_chunk(R"({"model" "gpt-4",)");
  EXPECT_EQ(filter_->decodeData(bad_chunk, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(local_reply_calls_, 1);

  // What was in flight is discarded, and nothing is replayed.
  Buffer::OwnedImpl trailing_chunk(std::string(64 * 1024, 'x'));
  EXPECT_EQ(filter_->decodeData(trailing_chunk, true),
            Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(inject_calls_, 0);
}

// A body ending on an empty terminal frame still gets its document closed, so a
// complete payload split that way is not mistaken for a truncated one.
TEST_F(AiProtocolManagerFilterTest, ChunkedBodyWithEmptyTerminalFrame) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl chunk1(R"({"model":"gpt)");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2(R"(-4"})");
  EXPECT_EQ(filter_->decodeData(chunk2, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl terminal;
  EXPECT_EQ(filter_->decodeData(terminal, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
}

// With trailers, no data frame carries end_stream, so the trailers close it.
TEST_F(AiProtocolManagerFilterTest, TrailerTerminatedJsonIsParsed) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
  EXPECT_EQ(continue_calls_, 1);
}

// The same path catches a truncated body that would otherwise be forwarded.
TEST_F(AiProtocolManagerFilterTest, RejectsTruncatedJsonTerminatedByTrailers) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4")");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

// An undeclared route gives the filter nothing to look at: headers are not held,
// the body is not offloaded, nothing is replayed.
TEST_F(AiProtocolManagerFilterTest, PassesThroughUndeclaredRoute) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::Continue);
  drain();

  // Left in the chain's own buffer rather than round-tripped.
  EXPECT_EQ(body.toString(), R"({"model":"gpt-4"})");
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(local_reply_calls_, 0);
}

// Nor does it subscribe to watermarks or claim any replay machinery.
TEST_F(AiProtocolManagerFilterTest, PassThroughStreamCostsNothing) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(watermark_cb_, nullptr);
  EXPECT_EQ(replay_cb_, nullptr);
}

// Trailers on a pass-through stream flow like everything else.
TEST_F(AiProtocolManagerFilterTest, PassesThroughTrailersOnUndeclaredRoute) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::Continue);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  drain();

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

// A malformed payload there is forwarded untouched -- nothing is looking at it.
TEST_F(AiProtocolManagerFilterTest, PassesThroughMalformedPayloadOnUndeclaredRoute) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::Continue);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(inject_calls_, 0);
}

// Best-effort parsing runs on an undeclared route and leaves a valid payload
// untouched.
TEST_F(AiProtocolManagerFilterTest, BestEffortParsingAcceptsValidPayload) {
  createFilter(/*best_effort_parsing=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
}

// Best effort means exactly that: a payload that does not parse is forwarded
// unchanged rather than rejected.
TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsMalformedPayload) {
  createFilter(/*best_effort_parsing=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model" "gpt-4"})");
  EXPECT_TRUE(injected_end_stream_);
}

// A payload abandoned mid-upload is still offloaded and replayed in full.
TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsRestOfAbandonedPayload) {
  createFilter(/*best_effort_parsing=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl chunk1(R"({"model" "gpt-4",)");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2(R"("stream":true})");
  EXPECT_EQ(filter_->decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model" "gpt-4","stream":true})");
}

// Best effort also tolerates an empty body, where a declared endpoint rejects it.
TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsEmptyBody) {
  createFilter(/*best_effort_parsing=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl empty;
  EXPECT_EQ(filter_->decodeData(empty, true), Http::FilterDataStatus::StopIterationNoBuffer);
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), 0);
}

// A declared endpoint is strict regardless of the filter-level best-effort
// setting: the schema is what says this payload has to be well formed.
TEST_F(AiProtocolManagerFilterTest, RouteConfigIsStrictEvenWithBestEffortConfigured) {
  createFilter(/*best_effort_parsing=*/true);
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

// A pass-through endpoint (no normalization) is parsed and forwarded just the
// same; normalization only governs the transcoding a later change adds.
TEST_F(AiProtocolManagerFilterTest, PassThroughEndpointIsParsedAndForwarded) {
  setRouteConfig(/*normalize=*/false);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
