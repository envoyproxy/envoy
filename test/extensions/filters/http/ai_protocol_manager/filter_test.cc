#include <deque>
#include <memory>
#include <optional>
#include <string>

#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/http/codes.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/status_macros.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/serializer.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/http/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/logging.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "nlohmann/json.hpp"

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
    ON_CALL(callbacks_, addUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks& cb) { watermark_cb_ = &cb; }));
    ON_CALL(callbacks_, removeUpstreamWatermarkCallbacks(testing::_))
        .WillByDefault(
            Invoke([this](Http::UpstreamWatermarkCallbacks&) { watermark_cb_ = nullptr; }));
    createFilter();
    // The in-memory buffer completes via dispatcher.post(); drain() runs them.
    ON_CALL(callbacks_.dispatcher_, post(testing::_))
        .WillByDefault(Invoke([this](Event::PostCb cb) { posted_.push_back(std::move(cb)); }));
    ON_CALL(callbacks_, injectDecodedDataToFilterChain(testing::_, testing::_))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          injected_.add(data);
          injected_end_stream_ = end_stream;
          ++inject_calls_;
          if (watermark_cb_ != nullptr && inject_calls_ == raise_watermark_at_inject_) {
            watermark_cb_->onAboveWriteBufferHighWatermark();
          }
        }));
    ON_CALL(callbacks_, continueDecoding()).WillByDefault(Invoke([this]() { ++continue_calls_; }));
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

  // Run at trace so debug/trace-log argument expressions execute too.
  LogLevelSetter log_level_setter_{spdlog::level::trace};

  // A zero threshold leaves the field unset, so the default applies.
  void createFilter(bool parse_unconfigured_routes = false,
                    uint32_t inline_string_threshold_bytes = 0) {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
    proto.mutable_request_handling()->set_parse_unconfigured_routes(parse_unconfigured_routes);
    if (inline_string_threshold_bytes != 0) {
      proto.mutable_request_handling()
          ->mutable_limits()
          ->mutable_inline_string_threshold_bytes()
          ->set_value(inline_string_threshold_bytes);
    }
    filter_ = std::make_unique<AiProtocolManagerFilter>(
        factory_, std::make_shared<const FilterConfig>(proto, *stats_store_.rootScope()));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  // Parses unconfigured routes too, so a test can show the chain is not run there.
  void createFilterWithAiFilters(AiFilterFactories ai_filter_factories) {
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto;
    proto.mutable_request_handling()->set_parse_unconfigured_routes(true);
    filter_ = std::make_unique<AiProtocolManagerFilter>(
        factory_, std::make_shared<const FilterConfig>(proto, *stats_store_.rootScope(),
                                                       std::move(ai_filter_factories)));
    filter_->setDecoderFilterCallbacks(callbacks_);
  }

  // Engaging claims a SchedulableCallback, so only engaging streams may create the mock.
  Http::FilterHeadersStatus decodeHeadersEngaging() {
    replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
    request_headers_ = requestHeaders();
    return filter_->decodeHeaders(request_headers_, /*end_stream=*/false);
  }

  // parse_unconfigured_routes offloads and replays whether or not the body parses.
  void engageIgnoringPayload() {
    createFilter(/*parse_unconfigured_routes=*/true);
    ASSERT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);
  }

  Http::FilterHeadersStatus decodeHeadersUnconfigured(Http::TestRequestHeaderMapImpl headers,
                                                      bool expect_engage) {
    createFilter(/*parse_unconfigured_routes=*/true);
    if (expect_engage) {
      replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
    }
    return filter_->decodeHeaders(headers, /*end_stream=*/false);
  }

  void setRouteConfig() {
    PerRouteProto proto;
    proto.mutable_request()->set_api_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
    route_config_ = std::make_unique<RouteConfig>(proto);
    ON_CALL(callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(testing::Return(route_config_.get()));
  }

  // `model` sits between the default 1KiB inline-string threshold and 4KiB.
  static std::string oversizedModelPayload() {
    return R"({"model":")" + std::string(2000, 'm') +
           R"(","messages":[{"role":"user","content":"hi"}]})";
  }

  static Http::TestRequestHeaderMapImpl requestHeaders() {
    // Best-effort parsing gates on a JSON content type.
    return Http::TestRequestHeaderMapImpl{
        {":method", "POST"}, {":path", "/chat/completions"}, {"content-type", "application/json"}};
  }

  // The manager requires onDestroy() before destruction (see buffer_manager.h); idempotent.
  void TearDown() override { filter_->onDestroy(); }

  void drain() {
    while (!posted_.empty()) {
      Event::PostCb cb = std::move(posted_.front());
      posted_.pop_front();
      cb();
    }
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_store_, "ai_protocol_manager." + name);
    return counter != nullptr ? counter->value() : 0;
  }

  std::deque<Event::PostCb> posted_;
  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  InMemoryExternalBufferFactory factory_;
  FilterConfigSharedPtr config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> callbacks_;
  Http::UpstreamWatermarkCallbacks* watermark_cb_{};
  // Owned by the manager the filter builds; must exist before that manager is constructed.
  NiceMock<Event::MockSchedulableCallback>* replay_cb_{nullptr};
  std::unique_ptr<RouteConfig> route_config_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;
  Http::TestRequestHeaderMapImpl request_headers_;

  Buffer::OwnedImpl injected_;
  bool injected_end_stream_{false};
  int inject_calls_{0};
  int continue_calls_{0};
  int raise_watermark_at_inject_{0}; // 0 = never.
  int local_reply_calls_{0};
  std::optional<Http::Code> local_reply_code_;
  std::string local_reply_details_;
};

TEST_F(AiProtocolManagerFilterTest, HoldsHeadersWhenBodyFollows) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);
}

// Holding the headers would deadlock: no body arrives to drive the release.
TEST_F(AiProtocolManagerFilterTest, PassesHeadersOnlyRequest) {
  Http::TestRequestHeaderMapImpl headers{{":method", "GET"}, {":path", "/healthz"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
}

TEST_F(AiProtocolManagerFilterTest, ReleasesHeldHeadersOnReplay) {
  engageIgnoringPayload();

  Buffer::OwnedImpl body("{\"messages\":[\"hi\"]}");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

TEST_F(AiProtocolManagerFilterTest, OffloadsAndReplaysBody) {
  engageIgnoringPayload();
  Buffer::OwnedImpl chunk1("{\"messages\":");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2("[\"hi\"]}");
  EXPECT_EQ(filter_->decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);

  // Writes and replay are asynchronous.
  EXPECT_EQ(inject_calls_, 0);

  drain();

  EXPECT_GE(inject_calls_, 1);
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{\"messages\":[\"hi\"]}");
}

TEST_F(AiProtocolManagerFilterTest, SingleFrameBody) {
  engageIgnoringPayload();
  Buffer::OwnedImpl body("{}");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), "{}");
}

// An empty frame issues no write, so replay is scheduled rather than run reentrantly.
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

TEST_F(AiProtocolManagerFilterTest, DestroyBeforeReplay) {
  engageIgnoringPayload();
  Buffer::OwnedImpl body("payload");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  filter_->onDestroy();
  drain();

  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, RegistersUpstreamWatermarkCallbacks) {
  engageIgnoringPayload();
  EXPECT_NE(watermark_cb_, nullptr);
}

TEST_F(AiProtocolManagerFilterTest, ReplayPausesUnderUpstreamBackPressure) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x'); // > ReadChunkSize, multiple chunks.
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onAboveWriteBufferHighWatermark();

  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  // Resume is deferred off the watermark callback stack to avoid reentrant read/inject.
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.length(), big.size());
  EXPECT_EQ(injected_.toString(), big);
}

TEST_F(AiProtocolManagerFilterTest, ReplayResumesMidStream) {
  engageIgnoringPayload();
  const std::string big(200 * 1024, 'x'); // 4 chunks of 64KiB + remainder.
  raise_watermark_at_inject_ = 1;
  Buffer::OwnedImpl body(big);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);

  drain();
  EXPECT_EQ(inject_calls_, 1);
  EXPECT_FALSE(injected_end_stream_);
  EXPECT_LT(injected_.length(), big.size());

  ASSERT_NE(watermark_cb_, nullptr);
  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// High watermark callbacks can nest (stream + connection).
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

  watermark_cb_->onBelowWriteBufferLowWatermark();
  drain();
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_FALSE(injected_end_stream_);

  watermark_cb_->onBelowWriteBufferLowWatermark();
  ASSERT_TRUE(replay_cb_->enabled());
  replay_cb_->invokeCallback();
  drain();
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(injected_.toString(), big);
}

// Held trailers are released only after the replayed body.
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

TEST_F(AiProtocolManagerFilterTest, TrailersWithoutBody) {
  engageIgnoringPayload();

  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  drain();

  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(continue_calls_, 0);
}

class AiProtocolManagerFilterResponseTest : public testing::Test {
public:
  void setup(const std::string& token_usage_yaml = "{}") {
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
    TestUtility::loadFromYaml(
        fmt::format("response_handling: {{token_usage: {}}}", token_usage_yaml), proto_config);
    proto_config.mutable_response_handling()
        ->mutable_token_usage()
        ->set_include_unconfigured_routes(true);
    setupWithProto(proto_config);
  }

  void setEncodeRouteConfig(const PerRouteProto& proto) {
    route_config_ = std::make_unique<RouteConfig>(proto);
    ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(testing::Return(route_config_.get()));
  }

  void
  setupWithProto(const envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager&
                     proto_config) {
    metadata_writes_.clear();
    typed_metadata_writes_.clear();
    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope());
    filter_ = std::make_unique<AiProtocolManagerFilter>(factory_, config_);
    // Encode-path code may use the decoder callbacks (e.g. the buffer memory account).
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
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
    // Only typed metadata is published; an untyped write anywhere is a bug.
    EXPECT_TRUE(metadata_writes_.empty());
    if (filter_ != nullptr) {
      filter_->onDestroy();
    }
  }

  void sendHeaders(absl::string_view content_type, absl::string_view status = "200") {
    Http::TestResponseHeaderMapImpl headers{{":status", std::string(status)},
                                            {"content-type", std::string(content_type)}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  }

  void sendData(absl::string_view body, bool end_stream) {
    Buffer::OwnedImpl data(body);
    EXPECT_EQ(filter_->encodeData(data, end_stream), Http::FilterDataStatus::Continue);
    EXPECT_EQ(data.toString(), body); // Observe-only: the response is never modified.
  }

  uint64_t counterValue(const std::string& name) {
    const auto counter = TestUtility::findCounter(stats_store_, "ai_protocol_manager." + name);
    return counter != nullptr ? counter->value() : 0;
  }

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

  // Run at trace so debug/trace-log argument expressions execute too.
  LogLevelSetter log_level_setter_{spdlog::level::trace};

  NiceMock<Stats::MockIsolatedStatsStore> stats_store_;
  InMemoryExternalBufferFactory factory_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<RouteConfig> route_config_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;
  std::vector<std::pair<std::string, Protobuf::Struct>> metadata_writes_;
  std::vector<std::pair<std::string, Protobuf::Any>> typed_metadata_writes_;
};

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

  EXPECT_EQ(counterValue("token_usage_found"), 1);
  EXPECT_EQ(counterValue("token_usage_total_mismatch"), 0);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  // The provider total is always preserved, agreeing or not.
  EXPECT_EQ(typed->model(), "gpt-4o");
  EXPECT_EQ(typed->input_tokens().value(), 19);
  EXPECT_EQ(typed->output_tokens().value(), 10);
  EXPECT_EQ(typed->total_tokens().value(), 29);
  EXPECT_EQ(typed->provider_total_tokens().value(), 29);
  EXPECT_FALSE(typed->has_input_token_details());
  EXPECT_FALSE(typed->has_output_token_details());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::COMPLETE);
}

// After a skipped event the counts may be a stale cumulative snapshot, hence PARTIAL.
TEST_F(AiProtocolManagerFilterResponseTest, PartialUsageReportsExtractionStatus) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  auto* token_usage = proto_config.mutable_response_handling()->mutable_token_usage();
  token_usage->set_include_unconfigured_routes(true);
  token_usage->mutable_limits()->mutable_max_sse_event_size()->set_value(256);
  setupWithProto(proto_config);
  sendHeaders("text/event-stream");
  sendData("data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"a\"}]}}],"
           "\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
           "\"totalTokenCount\":22}}\n\n",
           false);
  // The larger final snapshot exceeds max_event_size and is skipped.
  sendData("data: {\"pad\":\"" + std::string(500, 'x') +
               "\",\"usageMetadata\":{\"promptTokenCount\":6,"
               "\"candidatesTokenCount\":149,\"totalTokenCount\":155}}\n\n",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->total_tokens().value(), 22); // Stale snapshot.
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::PARTIAL);
  EXPECT_EQ(counterValue("sse_event_too_large"), 1);
}

// Rejected from content-length alone, so the FAILED record carries the configured API.
TEST_F(AiProtocolManagerFilterResponseTest, ContentLengthOverCapFailsExtraction) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  auto* token_usage = proto_config.mutable_response_handling()->mutable_token_usage();
  token_usage->set_include_unconfigured_routes(true);
  token_usage->set_default_api_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  token_usage->mutable_limits()->mutable_max_json_body_size()->set_value(64);
  setupWithProto(proto_config);
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/json"}, {"content-length", "100"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  sendData(std::string(100, 'x'), true);
  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::FAILED);
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  EXPECT_EQ(counterValue("response_body_too_large"), 1);
  EXPECT_EQ(counterValue("response_parse_error"), 0);
  EXPECT_EQ(counterValue("token_usage_failed"), 1);
}

// Same outcome as a headers-only response: stats do not depend on framing.
TEST_F(AiProtocolManagerFilterResponseTest, EmptyJsonBodyCountsMissing) {
  setup();
  sendHeaders("application/json");
  sendData("", /*end_stream=*/true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_EQ(counterValue("token_usage_failed"), 0);
  EXPECT_EQ(counterValue("response_parse_error"), 0);
}

// After an in-band `error` event the terminal usage update never arrives.
TEST_F(AiProtocolManagerFilterResponseTest, AnthropicStreamErrorMarksPartial) {
  setup();
  sendHeaders("text/event-stream");
  sendData("event: message_start\n"
           "data: {\"type\":\"message_start\",\"message\":{\"model\":\"claude-opus-5\","
           "\"usage\":{\"input_tokens\":2679,\"output_tokens\":3}}}\n\n",
           false);
  sendData("event: error\n"
           "data: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\"}}\n\n",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::PARTIAL);
  EXPECT_EQ(typed->input_tokens().value(), 2679);
  EXPECT_EQ(counterValue("token_usage_partial"), 1);
}

// Consumers must tell "Envoy failed to extract" apart from "no usage" (publishes nothing).
TEST_F(AiProtocolManagerFilterResponseTest, ExtractionFailurePublishesStatusOnlyRecord) {
  setup(); // Default caps.
  sendHeaders("text/event-stream");
  // Locks protocol detection before the oversized event.
  sendData("data: {\"object\":\"chat.completion.chunk\",\"model\":\"gpt-4o\","
           "\"choices\":[{\"delta\":{\"content\":\"hi\"}}],\"usage\":null}\n\n",
           false);
  // The only usage-bearing event exceeds the 1MiB default cap.
  sendData("data: {\"object\":\"chat.completion.chunk\",\"pad\":\"" +
               std::string(1024 * 1024 + 4096, 'x') +
               "\",\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,"
               "\"total_tokens\":3}}\n\n",
           true);

  EXPECT_EQ(counterValue("token_usage_failed"), 1);
  EXPECT_EQ(counterValue("token_usage_partial"), 0);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::FAILED);
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  EXPECT_EQ(typed->model(), "gpt-4o");
  EXPECT_FALSE(typed->has_total_tokens());
  EXPECT_FALSE(typed->has_input_tokens());
}

TEST_F(AiProtocolManagerFilterResponseTest, AbsentUsagePublishesNothing) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{}}],"
           "\"usage\":null}\n\ndata: [DONE]\n\n",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_EQ(counterValue("token_usage_partial"), 0);
}

TEST_F(AiProtocolManagerFilterResponseTest, EligibleHeadersOnlyResponseCountsMissing) {
  setup();
  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_TRUE(typed_metadata_writes_.empty());
}

// Unusable count fields must not leave the earlier snapshot published as COMPLETE.
TEST_F(AiProtocolManagerFilterResponseTest, MalformedPresentUsageFieldMarksPartial) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":16,"
           "\"totalTokenCount\":22}}\n\n",
           false);
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":\"149\","
           "\"totalTokenCount\":\"155\"}}\n\n",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->total_tokens().value(), 22); // Stale snapshot.
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::PARTIAL);
  EXPECT_EQ(counterValue("malformed_usage_field"), 1);
  EXPECT_EQ(counterValue("token_usage_partial"), 1);
}

// With the filter in both chains, the first publication owns the namespace.
TEST_F(AiProtocolManagerFilterResponseTest, DuplicatePublicationSkipped) {
  setup();
  envoy::data::ai::v3::TokenUsage prior;
  prior.set_model("model-a");
  prior.mutable_total_tokens()->set_value(100);
  Protobuf::Any prior_any;
  MessageUtil::packFrom(prior_any, prior);
  (*encoder_callbacks_.stream_info_.metadata_
        .mutable_typed_filter_metadata())["envoy.ai.token_usage"] = prior_any;

  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"usage\":{\"prompt_tokens\":3,"
           "\"completion_tokens\":4,\"total_tokens\":7}}",
           true);

  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_duplicate"), 1);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
}

// total_tokens is always input + output; the provider's own figure is kept separately.
TEST_F(AiProtocolManagerFilterResponseTest, InconsistentProviderTotalSurfaced) {
  setup();
  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"model\":\"gpt-4o\","
           "\"usage\":{\"prompt_tokens\":3,\"completion_tokens\":4,\"total_tokens\":100}}",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->total_tokens().value(), 7);
  EXPECT_EQ(typed->provider_total_tokens().value(), 100);
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::COMPLETE);
  EXPECT_EQ(counterValue("token_usage_total_mismatch"), 1);
}

// Input comes from message_start, cumulative output from the last message_delta.
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

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_tokens().value(), 2679);
  EXPECT_EQ(typed->output_tokens().value(), 15);
  EXPECT_EQ(typed->total_tokens().value(), 2694);
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
}

TEST_F(AiProtocolManagerFilterResponseTest, JsonBodyGemini) {
  setup();
  sendHeaders("application/json; charset=utf-8");
  sendData("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hi\"}]},"
           "\"finishReason\":\"STOP\"}],"
           "\"usageMetadata\":{\"promptTokenCount\":6,\"candidatesTokenCount\":149,"
           "\"totalTokenCount\":167,\"thoughtsTokenCount\":12},"
           "\"modelVersion\":\"gemini-2.5-flash\"}",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_tokens().value(), 6);
  // Canonical inclusive output: 149 candidates + 12 thoughts.
  EXPECT_EQ(typed->output_tokens().value(), 161);
  EXPECT_EQ(typed->total_tokens().value(), 167);
  EXPECT_EQ(typed->output_token_details().reasoning_tokens().value(), 12);
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::GEMINI_GENERATE_CONTENT);
  EXPECT_EQ(typed->model(), "gemini-2.5-flash");
}

// response.completed embeds the whole response object, so long generations exceed 64KiB.
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

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_tokens().value(), 37);
  EXPECT_EQ(typed->output_tokens().value(), 21000);
  EXPECT_EQ(counterValue("sse_event_too_large"), 0);
  EXPECT_EQ(counterValue("token_usage_found"), 1);
}

TEST_F(AiProtocolManagerFilterResponseTest, TrailersFinalize) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"type\":\"message_delta\",\"usage\":{\"input_tokens\":5,"
           "\"output_tokens\":7}}\n\n",
           false);
  Http::TestResponseTrailerMapImpl trailers{{"grpc-status", "0"}};
  EXPECT_EQ(filter_->encodeTrailers(trailers), Http::FilterTrailersStatus::Continue);
  EXPECT_TRUE(singleTypedWrite("envoy.ai.token_usage").has_value());
}

TEST_F(AiProtocolManagerFilterResponseTest, JsonResponseEndingInTrailers) {
  setup();
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}",
           /*end_stream=*/false);
  Http::TestResponseTrailerMapImpl trailers{{"grpc-status", "0"}};
  EXPECT_EQ(filter_->encodeTrailers(trailers), Http::FilterTrailersStatus::Continue);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_tokens().value(), 5);
  EXPECT_EQ(typed->output_tokens().value(), 7);
  EXPECT_EQ(counterValue("token_usage_found"), 1);
}

TEST_F(AiProtocolManagerFilterResponseTest, EmptyTerminalDataFrameFinalizes) {
  setup();
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":3,\"output_tokens\":4}}",
           /*end_stream=*/false);
  sendData("", /*end_stream=*/true);
  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->total_tokens().value(), 7);
}

TEST_F(AiProtocolManagerFilterResponseTest, ResetDoesNotPublish) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"type\":\"message_delta\",\"usage\":{\"input_tokens\":5,"
           "\"output_tokens\":7}}\n\n",
           /*end_stream=*/false);
  filter_->onDestroy();
  filter_.reset();
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

TEST_F(AiProtocolManagerFilterResponseTest, UsageAbsentCountsMissing) {
  setup();
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[{\"delta\":{}}],"
           "\"usage\":null}\n\ndata: [DONE]\n\n",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 1);
  EXPECT_EQ(counterValue("token_usage_found"), 0);
}

// The body carries no shape marker, so auto-detection alone cannot place it.
TEST_F(AiProtocolManagerFilterResponseTest, DefaultApiProtocolConfig) {
  setup("{default_api_protocol: ANTHROPIC_MESSAGES}");
  sendHeaders("application/json");
  sendData("{\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}", true);
  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
}

// Any per-route config, even an empty one, scopes a route in.
TEST_F(AiProtocolManagerFilterResponseTest, RouteScopingDefaultsToConfiguredRoutes) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  proto_config.mutable_response_handling()->mutable_token_usage();
  setupWithProto(proto_config);

  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"usage\":{\"prompt_tokens\":3,"
           "\"completion_tokens\":4,\"total_tokens\":7}}",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);

  setupWithProto(proto_config);
  setEncodeRouteConfig(PerRouteProto());
  sendHeaders("application/json");
  sendData("{\"object\":\"chat.completion\",\"usage\":{\"prompt_tokens\":3,"
           "\"completion_tokens\":4,\"total_tokens\":7}}",
           true);
  ASSERT_TRUE(singleTypedWrite("envoy.ai.token_usage").has_value());
  EXPECT_EQ(counterValue("token_usage_found"), 1);
}

// Route response API > route request API > default_api_protocol.
TEST_F(AiProtocolManagerFilterResponseTest, PerRouteProtocolPrecedence) {
  // No shape marker, so whichever protocol seeds extraction wins.
  const std::string ambiguous = "{\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}";

  setup("{default_api_protocol: GEMINI_GENERATE_CONTENT}");
  PerRouteProto per_route;
  per_route.mutable_request()->set_api_protocol(envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS);
  per_route.mutable_response()->set_api_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  setEncodeRouteConfig(per_route);
  sendHeaders("application/json");
  sendData(ambiguous, true);
  {
    const auto typed = singleTypedWrite("envoy.ai.token_usage");
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  }

  setup("{default_api_protocol: GEMINI_GENERATE_CONTENT}");
  PerRouteProto request_only;
  request_only.mutable_request()->set_api_protocol(envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  setEncodeRouteConfig(request_only);
  sendHeaders("application/json");
  sendData(ambiguous, true);
  {
    const auto typed = singleTypedWrite("envoy.ai.token_usage");
    ASSERT_TRUE(typed.has_value());
    EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::ANTHROPIC_MESSAGES);
  }
}

TEST_F(AiProtocolManagerFilterResponseTest, CustomMetadataNamespace) {
  setup("{metadata_namespace: custom.ns}");
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":1,\"output_tokens\":2}}", true);
  EXPECT_TRUE(singleTypedWrite("custom.ns").has_value());
}

TEST_F(AiProtocolManagerFilterResponseTest, UninspectedResponses) {
  setup();
  sendHeaders("text/event-stream", "502");
  sendData("data: {\"usageMetadata\":{\"promptTokenCount\":1}}\n\n", true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_missing"), 0);

  setup();
  sendHeaders("text/plain");
  sendData("hello", true);
  EXPECT_TRUE(typed_metadata_writes_.empty());

  setup();
  Http::TestResponseHeaderMapImpl headers{{":status", "204"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, true), Http::FilterHeadersStatus::Continue);
  EXPECT_TRUE(typed_metadata_writes_.empty());
}

// Extraction needs the body decompressed before this filter on the encode path.
TEST_F(AiProtocolManagerFilterResponseTest, CompressedResponseSkipped) {
  setup();
  Http::TestResponseHeaderMapImpl headers{
      {":status", "200"}, {"content-type", "application/json"}, {"content-encoding", "gzip"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  // String juxtaposition ends the \x00 escape before 'c' (a hex digit).
  sendData("\x1f\x8b\x08\x00"
           "compressed-bytes",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("unsupported_content_encoding"), 1);
  EXPECT_EQ(counterValue("response_parse_error"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

// Content-Encoding is list-valued and repeatable.
TEST_F(AiProtocolManagerFilterResponseTest, ContentEncodingMatrix) {
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", "identity"},
                                            {"content-encoding", "gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{}", true);
    EXPECT_TRUE(typed_metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 1);
  }
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", "identity, gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{}", true);
    EXPECT_TRUE(typed_metadata_writes_.empty());
    // The fixture's stats store persists across setup() calls: cumulative.
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 2);
  }
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{{":status", "200"},
                                            {"content-type", "application/json"},
                                            {"content-encoding", " identity , identity "},
                                            {"content-encoding", "identity"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("{\"type\":\"message\",\"usage\":{\"input_tokens\":5,\"output_tokens\":7}}", true);
    EXPECT_TRUE(singleTypedWrite("envoy.ai.token_usage").has_value());
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
    EXPECT_TRUE(typed_metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 3);
  }
  setup();
  {
    Http::TestResponseHeaderMapImpl headers{
        {":status", "200"}, {"content-type", "text/plain"}, {"content-encoding", "gzip"}};
    EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
    sendData("hello", true);
    EXPECT_TRUE(typed_metadata_writes_.empty());
    EXPECT_EQ(counterValue("unsupported_content_encoding"), 3); // Unchanged.
  }
}

TEST_F(AiProtocolManagerFilterResponseTest, InputTokenDetailsPublished) {
  setup();
  sendHeaders("application/json");
  sendData("{\"type\":\"message\",\"model\":\"claude-opus-5\","
           "\"usage\":{\"input_tokens\":100,\"output_tokens\":7,"
           "\"cache_read_input_tokens\":30,\"cache_creation_input_tokens\":20}}",
           true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_token_details().cached_tokens().value(), 30);
  EXPECT_EQ(typed->input_token_details().cache_creation_tokens().value(), 20);
  EXPECT_FALSE(typed->input_token_details().has_tool_use_tokens());
}

TEST_F(AiProtocolManagerFilterResponseTest, ToolUseInputDetailPublished) {
  setup();
  sendHeaders("application/json");
  sendData("{\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"hi\"}]}}],"
           "\"usageMetadata\":{\"promptTokenCount\":6,\"toolUsePromptTokenCount\":5,"
           "\"candidatesTokenCount\":10,\"totalTokenCount\":21}}",
           true);
  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->input_token_details().tool_use_tokens().value(), 5);
  // Canonical inclusive input: 6 prompt + 5 tool-use.
  EXPECT_EQ(typed->input_tokens().value(), 11);
}

// Without content-length the cap trips in onData, before any protocol has locked.
TEST_F(AiProtocolManagerFilterResponseTest, OversizedBodyWithoutContentLengthFailsUnspecified) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager proto_config;
  auto* token_usage = proto_config.mutable_response_handling()->mutable_token_usage();
  token_usage->set_include_unconfigured_routes(true);
  token_usage->mutable_limits()->mutable_max_json_body_size()->set_value(64);
  setupWithProto(proto_config);

  Http::TestResponseHeaderMapImpl headers{{":status", "200"}, {"content-type", "application/json"}};
  EXPECT_EQ(filter_->encodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);
  sendData(std::string(100, 'x'), true);

  const auto typed = singleTypedWrite("envoy.ai.token_usage");
  ASSERT_TRUE(typed.has_value());
  EXPECT_EQ(typed->api_protocol(), envoy::type::ai::v3::API_PROTOCOL_UNSPECIFIED);
  EXPECT_EQ(typed->extraction_status(), envoy::data::ai::v3::TokenUsage::FAILED);
  EXPECT_EQ(counterValue("response_body_too_large"), 1);
  EXPECT_EQ(counterValue("token_usage_failed"), 1);
}

TEST_F(AiProtocolManagerFilterResponseTest, DisabledWithoutConfig) {
  envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager empty_response;
  empty_response.mutable_response_handling(); // No token_usage inside.
  setupWithProto(empty_response);
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
           "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n"
           "data: [DONE]\n\n",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());

  setupWithProto(envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager());
  sendHeaders("text/event-stream");
  sendData("data: {\"object\":\"chat.completion.chunk\",\"choices\":[],"
           "\"usage\":{\"prompt_tokens\":1,\"completion_tokens\":2,\"total_tokens\":3}}\n\n"
           "data: [DONE]\n\n",
           true);
  EXPECT_TRUE(typed_metadata_writes_.empty());
  EXPECT_EQ(counterValue("token_usage_found"), 0);
  EXPECT_EQ(counterValue("token_usage_missing"), 0);
}

TEST_F(AiProtocolManagerFilterTest, ParsesDeclaredEndpointPayloadAndReplaysItVerbatim) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  const std::string payload =
      R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}],"stream":true,"max_tokens":256})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(nlohmann::json::parse(injected_.toString()), nlohmann::json::parse(payload));
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(counterValue("request_parsed"), 1);
  EXPECT_EQ(counterValue("request_parse_error"), 0);
  EXPECT_EQ(counterValue("request_schema_invalid"), 0);
}

// Rewrites `model` so a test can tell the chain ran ahead of replay.
class ContextRecordingAiFilter : public AiFilter {
public:
  struct Seen {
    ApiProtocol protocol;
    std::string path;
    const StreamInfo::StreamInfo* stream_info;
  };

  ContextRecordingAiFilter(const AiFilterContext& context, std::vector<Seen>& seen)
      : context_(context), seen_(seen) {}

  Coroutine::Task<absl::Status> decode(AiRequestReceiver receive_request,
                                       AiRequestPropagator propagate_request,
                                       LocalReplier) override {
    ASSIGN_OR_CO_RETURN(AiRequestPtr request, co_await std::move(receive_request)());
    seen_.push_back({context_.request_protocol,
                     std::string(context_.request_headers.getPathValue()), &context_.stream_info});
    request->request_index().json()["model"] = "rewritten";
    co_return co_await std::move(propagate_request)(std::move(request));
  }

private:
  const AiFilterContext context_;
  std::vector<Seen>& seen_;
};

TEST_F(AiProtocolManagerFilterTest, RunsConfiguredAiFiltersOverDeclaredPayload) {
  std::vector<ContextRecordingAiFilter::Seen> seen;
  int built = 0;
  createFilterWithAiFilters({[&](const AiFilterContext& context) -> AiFilterPtr {
    ++built;
    return std::make_unique<ContextRecordingAiFilter>(context, seen);
  }});
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(built, 1);
  ASSERT_EQ(seen.size(), 1);
  EXPECT_EQ(seen[0].protocol, ApiProtocol::OpenAiChatCompletions);
  EXPECT_EQ(seen[0].path, "/chat/completions");
  EXPECT_EQ(seen[0].stream_info, &callbacks_.stream_info_);
  EXPECT_EQ(nlohmann::json::parse(injected_.toString())["model"], "rewritten");
  EXPECT_TRUE(injected_end_stream_);
}

TEST_F(AiProtocolManagerFilterTest, DoesNotRunAiFiltersOnUnconfiguredRoute) {
  std::vector<ContextRecordingAiFilter::Seen> seen;
  int built = 0;
  createFilterWithAiFilters({[&](const AiFilterContext& context) -> AiFilterPtr {
    ++built;
    return std::make_unique<ContextRecordingAiFilter>(context, seen);
  }});
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(built, 0);
  EXPECT_TRUE(seen.empty());
  EXPECT_EQ(nlohmann::json::parse(injected_.toString())["model"], "gpt-4");
  EXPECT_TRUE(injected_end_stream_);
}

TEST_F(AiProtocolManagerFilterTest, NullAiFilterIsSkipped) {
  createFilterWithAiFilters({[](const AiFilterContext&) -> AiFilterPtr { return nullptr; }});
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  const std::string payload = R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(nlohmann::json::parse(injected_.toString()), nlohmann::json::parse(payload));
  EXPECT_TRUE(injected_end_stream_);
}

TEST_F(AiProtocolManagerFilterTest, SetsContentLengthOnReplay) {
  setRouteConfig();
  replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
  request_headers_ = Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                                    {":path", "/chat/completions"},
                                                    {"content-type", "application/json"},
                                                    {"content-length", "999"}};
  ASSERT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(request_headers_.getContentLengthValue(), "999");

  const std::string payload = R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_NE(request_headers_.ContentLength(), nullptr);
  EXPECT_EQ(request_headers_.getContentLengthValue(), absl::StrCat(injected_.length()));
}

TEST_F(AiProtocolManagerFilterTest, DoesNotSetContentLengthOnReplayWhenAbsent) {
  setRouteConfig();
  replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
  request_headers_ = Http::TestRequestHeaderMapImpl{
      {":method", "POST"}, {":path", "/chat/completions"}, {"content-type", "application/json"}};
  ASSERT_EQ(filter_->decodeHeaders(request_headers_, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
  EXPECT_EQ(request_headers_.ContentLength(), nullptr);

  const std::string payload = R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(request_headers_.ContentLength(), nullptr);
}

// The schema declares `model` non-offloadable, so an offloaded value fails validation.
TEST_F(AiProtocolManagerFilterTest, ModelOverInlineStringThresholdIsRejected) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(oversizedModelPayload());
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_details_, "ai_protocol_manager_invalid_json");
  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, RaisedInlineStringThresholdKeepsLargeValuesInline) {
  createFilter(/*parse_unconfigured_routes=*/false, /*inline_string_threshold_bytes=*/4096);
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  const std::string payload = oversizedModelPayload();
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  // The DOM's std::map members reorder keys on re-serialization, so compare parsed JSON.
  EXPECT_EQ(nlohmann::json::parse(injected_.toString()), nlohmann::json::parse(payload));
  EXPECT_TRUE(injected_end_stream_);
}

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
  // Parse errors and schema failures mean different things to operators, so count apart.
  EXPECT_EQ(counterValue("request_parse_error"), 1);
  EXPECT_EQ(counterValue("request_schema_invalid"), 0);
  EXPECT_EQ(counterValue("request_parsed"), 0);
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

// No payload to validate, same as a request ending on its headers; a GET arrives this way.
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
  EXPECT_EQ(counterValue("request_parsed"), 0);
  EXPECT_EQ(counterValue("request_parse_error"), 0);
}

// Once bytes have arrived, an empty terminal frame closes the payload rather than excusing it.
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

// Parsing precedes offloading, so an early bad byte fails the request before the upload ends.
TEST_F(AiProtocolManagerFilterTest, RejectsMalformedJsonMidUpload) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl bad_chunk(R"({"model" "gpt-4",)");
  EXPECT_EQ(filter_->decodeData(bad_chunk, false), Http::FilterDataStatus::StopIterationNoBuffer);
  EXPECT_EQ(local_reply_calls_, 1);

  Buffer::OwnedImpl trailing_chunk(std::string(64 * 1024, 'x'));
  EXPECT_EQ(filter_->decodeData(trailing_chunk, true),
            Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, RejectsPayloadFailingSchemaValidation) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  // Missing required "messages" field.
  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(local_reply_details_, "ai_protocol_manager_invalid_json");
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(counterValue("request_schema_invalid"), 1);
  EXPECT_EQ(counterValue("request_parse_error"), 0);
  EXPECT_EQ(counterValue("request_parsed"), 0);
}

TEST_F(AiProtocolManagerFilterTest, PassesThroughUnknownFields) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  const std::string payload =
      R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}],"custom_tag":"blue"})";
  Buffer::OwnedImpl body(payload);
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(nlohmann::json::parse(injected_.toString()), nlohmann::json::parse(payload));
  EXPECT_TRUE(injected_end_stream_);
}

TEST_F(AiProtocolManagerFilterTest, ChunkedBodyWithEmptyTerminalFrame) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl chunk1(R"({"model":"gpt-4","messages":[{"role":")");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2(R"(user","content":"hi"}]})");
  EXPECT_EQ(filter_->decodeData(chunk2, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl terminal;
  EXPECT_EQ(filter_->decodeData(terminal, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(
      nlohmann::json::parse(injected_.toString()),
      nlohmann::json::parse(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})"));
}

// With trailers, no data frame carries end_stream, so the trailers close it.
TEST_F(AiProtocolManagerFilterTest, TrailerTerminatedJsonIsParsed) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
  EXPECT_EQ(filter_->decodeData(body, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Http::TestRequestTrailerMapImpl trailers{{"x-trailer", "1"}};
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(
      nlohmann::json::parse(injected_.toString()),
      nlohmann::json::parse(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})"));
  EXPECT_EQ(continue_calls_, 1);
}

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

TEST_F(AiProtocolManagerFilterTest, PassesThroughUndeclaredRoute) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::Continue);
  drain();

  EXPECT_EQ(body.toString(), R"({"model":"gpt-4"})");
  EXPECT_EQ(inject_calls_, 0);
  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(counterValue("request_parsed"), 0);
  EXPECT_EQ(counterValue("request_passthrough"), 0);
}

TEST_F(AiProtocolManagerFilterTest, PassThroughStreamCostsNothing) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  EXPECT_EQ(watermark_cb_, nullptr);
  EXPECT_EQ(replay_cb_, nullptr);
}

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

TEST_F(AiProtocolManagerFilterTest, PassesThroughMalformedPayloadOnUndeclaredRoute) {
  Http::TestRequestHeaderMapImpl headers = requestHeaders();
  EXPECT_EQ(filter_->decodeHeaders(headers, false), Http::FilterHeadersStatus::Continue);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::Continue);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortParsingAcceptsValidPayload) {
  createFilter(/*parse_unconfigured_routes=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model":"gpt-4"})");
  // No schema to check, but the document exists for later filters, so it counts as parsed.
  EXPECT_EQ(counterValue("request_parsed"), 1);
  EXPECT_EQ(counterValue("request_passthrough"), 0);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsMalformedPayload) {
  createFilter(/*parse_unconfigured_routes=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model" "gpt-4"})");
  EXPECT_TRUE(injected_end_stream_);
  EXPECT_EQ(counterValue("request_passthrough"), 1);
  EXPECT_EQ(counterValue("request_parse_error"), 0);
  EXPECT_EQ(counterValue("request_parsed"), 0);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsRestOfAbandonedPayload) {
  createFilter(/*parse_unconfigured_routes=*/true);
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl chunk1(R"({"model" "gpt-4",)");
  EXPECT_EQ(filter_->decodeData(chunk1, false), Http::FilterDataStatus::StopIterationNoBuffer);
  Buffer::OwnedImpl chunk2(R"("stream":true})");
  EXPECT_EQ(filter_->decodeData(chunk2, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(injected_.toString(), R"({"model" "gpt-4","stream":true})");
}

TEST_F(AiProtocolManagerFilterTest, BestEffortParsingForwardsEmptyBody) {
  createFilter(/*parse_unconfigured_routes=*/true);
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

// A JSON request arrives whole before a response is wanted, so holding headers cannot stall.
TEST_F(AiProtocolManagerFilterTest, BestEffortEngagesOnJsonContentType) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/v1/chat"},
                                               {"content-type", "application/json"}},
                /*expect_engage=*/true),
            Http::FilterHeadersStatus::StopIteration);
}

// Media type parameters are not part of the type, and the type is case-insensitive.
TEST_F(AiProtocolManagerFilterTest, BestEffortEngagesOnJsonContentTypeWithParameters) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/v1/chat"},
                                               {"content-type", "Application/JSON; charset=utf-8"}},
                /*expect_engage=*/true),
            Http::FilterHeadersStatus::StopIteration);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortEngagesOnJsonStructuredSuffix) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/v1/chat"},
                                               {"content-type", "application/vnd.openai+json"}},
                /*expect_engage=*/true),
            Http::FilterHeadersStatus::StopIteration);
}

// The stall the gate exists for; the +json suffix leaves only the protocol check to catch it.
TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsGrpcRequest) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/Chat/Complete"},
                                               {"content-type", "application/grpc+json"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

// Unary Connect sends plain application/json and stays eligible; streaming does not.
TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsConnectStreamingRequest) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/Chat/Complete"},
                                               {"content-type", "application/connect+json"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

// An upgraded connection is full-duplex whatever content type it carries.
TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsUpgrade) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "GET"},
                                               {":path", "/ws"},
                                               {"content-type", "application/json"},
                                               {"connection", "keep-alive, Upgrade"},
                                               {"upgrade", "websocket"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsConnect) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{
                    {":method", "CONNECT"}, {":path", "/"}, {"content-type", "application/json"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsNonJsonContentType) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"},
                                               {":path", "/upload"},
                                               {"content-type", "application/octet-stream"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(AiProtocolManagerFilterTest, BestEffortSkipsMissingContentType) {
  EXPECT_EQ(decodeHeadersUnconfigured(
                Http::TestRequestHeaderMapImpl{{":method", "POST"}, {":path", "/upload"}},
                /*expect_engage=*/false),
            Http::FilterHeadersStatus::Continue);
}

TEST_F(AiProtocolManagerFilterTest, DeclaredEndpointIgnoresGate) {
  setRouteConfig();
  replay_cb_ = new NiceMock<Event::MockSchedulableCallback>(&callbacks_.dispatcher_);
  Http::TestRequestHeaderMapImpl headers{
      {":method", "POST"}, {":path", "/chat/completions"}, {"content-type", "text/plain"}};
  EXPECT_EQ(filter_->decodeHeaders(headers, /*end_stream=*/false),
            Http::FilterHeadersStatus::StopIteration);
}

TEST_F(AiProtocolManagerFilterTest, RouteConfigIsStrictEvenWithBestEffortConfigured) {
  createFilter(/*parse_unconfigured_routes=*/true);
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, PassThroughEndpointIsParsedAndForwarded) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  EXPECT_EQ(
      nlohmann::json::parse(injected_.toString()),
      nlohmann::json::parse(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})"));
}

TEST_F(AiProtocolManagerFilterTest, SetsFilterStateObjectOnParsedPayload) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl body(R"({"model":"gpt-4","messages":[{"role":"user","content":"hi"}]})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 0);
  auto* fs = callbacks_.stream_info_.filterState()->getDataReadOnly<APMRequestPayloadIndex>(
      APMRequestPayloadIndex::kFilterStateKey);
  ASSERT_NE(fs, nullptr);
  EXPECT_EQ(fs->index().json()["model"], "gpt-4");
}

TEST_F(AiProtocolManagerFilterTest, SchemaValidationRejectsInvalidPayload) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  // Missing required "messages" array.
  Buffer::OwnedImpl body(R"({"model":"gpt-4"})");
  EXPECT_EQ(filter_->decodeData(body, true), Http::FilterDataStatus::StopIterationNoBuffer);
  drain();

  EXPECT_EQ(local_reply_calls_, 1);
  EXPECT_EQ(local_reply_code_, Http::Code::BadRequest);
  EXPECT_EQ(inject_calls_, 0);
}

TEST_F(AiProtocolManagerFilterTest, TrailersDroppedAfterPayloadRejection) {
  setRouteConfig();
  EXPECT_EQ(decodeHeadersEngaging(), Http::FilterHeadersStatus::StopIteration);

  Buffer::OwnedImpl bad_chunk(R"({"model" "gpt-4"})");
  EXPECT_EQ(filter_->decodeData(bad_chunk, false), Http::FilterDataStatus::StopIterationNoBuffer);

  Http::TestRequestTrailerMapImpl trailers;
  EXPECT_EQ(filter_->decodeTrailers(trailers), Http::FilterTrailersStatus::StopIteration);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
