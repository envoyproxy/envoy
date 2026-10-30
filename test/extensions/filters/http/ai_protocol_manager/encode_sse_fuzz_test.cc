#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "envoy/data/ai/v3/token_usage.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"
#include "envoy/type/ai/v3/llm_protocol.pb.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"
#include "test/mocks/http/mocks.h"
#include "test/test_common/utility.h"

#include "absl/strings/string_view.h"
#include "gmock/gmock.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using AiFilters::Transcoder::TranscoderFilter;
using AiFilters::Transcoder::TranscoderFilterConfig;
using AiFilters::Transcoder::TranscoderFilterConfigSharedPtr;
using AiProtocolManagerProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager;
using LlmProtocolProto = envoy::type::ai::v3::LLMProtocol;
using TokenUsageProto = envoy::data::ai::v3::TokenUsage;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

constexpr size_t kMaxInputSize = 64 * 1024;
constexpr LlmProtocolProto kProtocols[] = {
    envoy::type::ai::v3::LLM_PROTOCOL_UNSPECIFIED, envoy::type::ai::v3::OPENAI_CHAT_COMPLETIONS,
    envoy::type::ai::v3::OPENAI_RESPONSES,         envoy::type::ai::v3::ANTHROPIC_MESSAGES,
    envoy::type::ai::v3::GEMINI_GENERATE_CONTENT,
};

// Drives one SSE response stream through AiProtocolManagerFilter.
struct SseStreamRunner {
  struct Options {
    uint32_t max_sse_event_size;
    uint32_t max_parsed_sse_events;
  };

  SseStreamRunner(LlmProtocolProto request_protocol, LlmProtocolProto response_protocol,
                  const Options& options) {
    // Reuse across fuzz iterations to avoid rebuilding TranscodingEngine's dialect rules per run.
    static Stats::TestUtil::TestStore stats_store;
    static ExternalBufferFactorySharedPtr buffer_factory =
        std::make_shared<InMemoryExternalBufferFactory>();
    static AiFilterFactories transcoder_factories = []() {
      TranscoderProto from_ir_proto;
      from_ir_proto.set_response_handling(TranscoderProto::FROM_IR);
      TranscoderFilterConfigSharedPtr from_ir_config = std::make_shared<TranscoderFilterConfig>(
          from_ir_proto, std::move(*TranscodingEngine::createDefault()), *stats_store.rootScope());

      TranscoderProto to_ir_proto;
      to_ir_proto.set_response_handling(TranscoderProto::TO_IR);
      TranscoderFilterConfigSharedPtr to_ir_config = std::make_shared<TranscoderFilterConfig>(
          to_ir_proto, std::move(*TranscodingEngine::createDefault()), *stats_store.rootScope());

      // FilterManager reverses the filter chain on the response path, so registering
      // `from_ir` before `to_ir` executes TO_IR (backend -> IR) followed by FROM_IR (IR -> client).
      return AiFilterFactories{
          [from_ir_config](const AiFilterContext& ctx) {
            return std::make_shared<TranscoderFilter>(from_ir_config, ctx);
          },
          [to_ir_config](const AiFilterContext& ctx) {
            return std::make_shared<TranscoderFilter>(to_ir_config, ctx);
          },
      };
    }();

    PerRouteProto route_proto;
    route_proto.mutable_request()->set_llm_protocol(request_protocol);
    route_proto.mutable_response()->set_llm_protocol(response_protocol);
    route_config_ = std::move(*RouteConfig::create(route_proto));

    AiProtocolManagerProto filter_proto;
    filter_proto.mutable_request_handling();
    auto* limits =
        filter_proto.mutable_response_handling()->mutable_token_usage()->mutable_limits();
    limits->mutable_max_sse_event_size()->set_value(options.max_sse_event_size);
    limits->mutable_max_parsed_sse_events()->set_value(options.max_parsed_sse_events);
    filter_ = std::make_unique<AiProtocolManagerFilter>(std::make_shared<FilterConfig>(
        filter_proto, *stats_store.rootScope(), transcoder_factories, buffer_factory));
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);

    // MockStreamInfo defaults start_time_ to wall-clock now(). TranscoderFilter snapshots
    // decoder_callbacks_.streamInfo().startTime() at construction (during finalizeDecode()) and
    // emits it as OpenAI's "created" field in encodeSSE(), so pin it across both runners.
    decoder_callbacks_.stream_info_.start_time_ = SystemTime(std::chrono::seconds(1700000000));
    ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(Return(route_config_.get()));
    ON_CALL(decoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
        .WillByDefault(Invoke([this](std::function<void()> cb) {
          return new NiceMock<Event::MockSchedulableCallback>(&decoder_callbacks_.dispatcher_, cb);
        }));

    ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig())
        .WillByDefault(Return(route_config_.get()));
    ON_CALL(encoder_callbacks_.dispatcher_, post(_)).WillByDefault(Invoke([this](Event::PostCb cb) {
      posted_callbacks_.push_back(std::move(cb));
    }));
    ON_CALL(encoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
        .WillByDefault(Invoke([this](std::function<void()> cb) {
          Event::MockSchedulableCallback* scb =
              new NiceMock<Event::MockSchedulableCallback>(&encoder_callbacks_.dispatcher_, cb);
          auto schedule = [this, cb]() { posted_callbacks_.push_back(cb); };
          ON_CALL(*scb, scheduleCallbackCurrentIteration()).WillByDefault(Invoke(schedule));
          ON_CALL(*scb, scheduleCallbackNextIteration()).WillByDefault(Invoke(schedule));
          return scb;
        }));
    ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
        .WillByDefault(Invoke([this](Buffer::Instance& data, bool end_stream) {
          encoded_data.add(data);
          encoded_end_stream = encoded_end_stream || end_stream;
        }));
    ON_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _))
        .WillByDefault(Invoke([this](Http::Code code, absl::string_view,
                                     std::function<void(Http::ResponseHeaderMap&)>,
                                     std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
          local_reply_code = code;
          ++local_reply_calls;
        }));
    ON_CALL(encoder_callbacks_.stream_info_, setDynamicTypedMetadata(_, _))
        .WillByDefault(Invoke([this](const std::string&, const Protobuf::Any& value) {
          FUZZ_ASSERT(value.UnpackTo(&published_usage.emplace()));
        }));

    // AiProtocolManagerFilter only constructs filter_manager_ (and the TranscoderFilter instances)
    // in finalizeDecode() at the end of a non-empty JSON request, so encodeHeaders() would bypass
    // response filtering without decodeHeaders() + decodeData() first. With request_handling unset
    // on TranscoderFilter, TranscoderFilter::decode() passes the request straight through to
    // RequestFilterManager::runSink(), which calls decode_manager_->inject() to schedule
    // request-body replay on decoder_callbacks_.dispatcher_. Returning an un-wired
    // MockSchedulableCallback above satisfies MockDispatcher's non-null assert while keeping that
    // replay a no-op.
    Http::TestRequestHeaderMapImpl request_headers{{":method", "POST"},
                                                   {":path", "/v1/chat/completions"},
                                                   {"content-type", "application/json"}};
    filter_->decodeHeaders(request_headers, /*end_stream=*/false);
    Buffer::OwnedImpl request_body(R"({"model":"gpt-4"})");
    filter_->decodeData(request_body, /*end_stream=*/true);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"content-type", "text/event-stream"}};
    filter_->encodeHeaders(response_headers, /*end_stream=*/false);
  }

  void runFragmented(absl::string_view body, uint64_t seed) {
    size_t max_chunk_size = (seed & 1ULL) != 0 ? 7 : 97;
    uint64_t state = seed | 1;
    size_t offset = 0;
    while (offset < body.size()) {
      state = state * 6364136223846793005ULL + 1442695040888963407ULL;
      size_t chunk_size =
          std::min<size_t>(1 + (state >> 33) % max_chunk_size, body.size() - offset);
      Buffer::OwnedImpl chunk(body.substr(offset, chunk_size));
      filter_->encodeData(chunk, /*end_stream=*/false);
      runPostedCallbacks();
      offset += chunk_size;
    }

    Buffer::OwnedImpl empty;
    filter_->encodeData(empty, /*end_stream=*/true);
    runPostedCallbacks();
    filter_->onDestroy();
  }

  void runWhole(absl::string_view body) {
    Buffer::OwnedImpl all(body);
    filter_->encodeData(all, /*end_stream=*/true);
    runPostedCallbacks();
    filter_->onDestroy();
  }

  Buffer::OwnedImpl encoded_data;
  bool encoded_end_stream{false};
  int local_reply_calls{0};
  Http::Code local_reply_code{};
  std::optional<TokenUsageProto> published_usage;

private:
  void runPostedCallbacks() {
    while (!posted_callbacks_.empty()) {
      Event::PostCb cb = std::move(posted_callbacks_.front());
      posted_callbacks_.pop_front();
      cb();
    }
  }

  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::deque<Event::PostCb> posted_callbacks_;
  std::unique_ptr<RouteConfig> route_config_;
  std::unique_ptr<AiProtocolManagerFilter> filter_;
};

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  if (len > kMaxInputSize) {
    return;
  }

  FuzzedDataProvider provider(buf, len);
  LlmProtocolProto request_protocol = provider.PickValueInArray(kProtocols);
  LlmProtocolProto response_protocol = provider.PickValueInArray(kProtocols);
  SseStreamRunner::Options options{
      .max_sse_event_size = provider.ConsumeIntegralInRange<uint32_t>(1, 4 * 1024),
      .max_parsed_sse_events = provider.ConsumeIntegralInRange<uint32_t>(1, 16),
  };
  uint64_t fragmentation_seed = provider.ConsumeIntegral<uint64_t>();
  std::string body = provider.ConsumeRemainingBytesAsString();

  SseStreamRunner fragmented(request_protocol, response_protocol, options);
  fragmented.runFragmented(body, fragmentation_seed);

  SseStreamRunner whole(request_protocol, response_protocol, options);
  whole.runWhole(body);

  FUZZ_ASSERT(fragmented.local_reply_calls == whole.local_reply_calls);
  FUZZ_ASSERT(fragmented.local_reply_code == whole.local_reply_code);
  if (fragmented.local_reply_calls == 0) {
    FUZZ_ASSERT(fragmented.encoded_end_stream && whole.encoded_end_stream);
    FUZZ_ASSERT(fragmented.encoded_data.toString() == whole.encoded_data.toString());
  }
  FUZZ_ASSERT(fragmented.published_usage.has_value() == whole.published_usage.has_value());
  if (fragmented.published_usage.has_value()) {
    FUZZ_ASSERT(TestUtility::protoEqual(*fragmented.published_usage, *whole.published_usage));
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
