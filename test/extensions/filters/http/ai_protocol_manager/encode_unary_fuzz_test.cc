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

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/http/ai_protocol_manager/encode_unary_fuzz.pb.h"
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
using AiProtocolManagerProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager;
using EncodeUnaryFuzzInput =
    envoy::extensions::filters::http::ai_protocol_manager::EncodeUnaryFuzzInput;
using TokenUsageProto = envoy::data::ai::v3::TokenUsage;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;
using ::testing::_;
using ::testing::NiceMock;

constexpr size_t kMaxInputSize = 64 * 1024;

// Drives unary JSON responses through AiProtocolManagerFilter.
class UnaryResponseRunner {
public:
  struct Result {
    std::string body;
    bool end_stream{false};
    int local_reply_calls{0};
    Http::Code local_reply_code{};
    std::optional<TokenUsageProto> published_usage;
  };

  UnaryResponseRunner() {
    Stats::Scope& scope = *stats_store_.rootScope();
    TranscoderProto from_ir_proto;
    from_ir_proto.set_response_handling(TranscoderProto::FROM_IR);
    auto from_ir_config = std::make_shared<TranscoderFilterConfig>(
        from_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

    TranscoderProto to_ir_proto;
    to_ir_proto.set_response_handling(TranscoderProto::TO_IR);
    auto to_ir_config = std::make_shared<TranscoderFilterConfig>(
        to_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

    // FilterManager reverses the filter chain on the response path, so registering
    // `from_ir` before `to_ir` executes TO_IR (backend -> IR) followed by FROM_IR (IR -> client).
    ai_filter_factories_ = AiFilterFactories{
        [from_ir_config](const AiFilterContext& ctx) {
          return std::make_shared<TranscoderFilter>(from_ir_config, ctx);
        },
        [to_ir_config](const AiFilterContext& ctx) {
          return std::make_shared<TranscoderFilter>(to_ir_config, ctx);
        },
    };

    // MockStreamInfo defaults start_time_ to wall-clock now(). TranscoderFilter snapshots
    // decoder_callbacks_.streamInfo().startTime() at construction (during finalizeDecode()) and
    // emits it as OpenAI's "created" field in encodeUnary(), so pin it across runs.
    decoder_callbacks_.stream_info_.start_time_ = SystemTime(std::chrono::seconds(1700000000));
    ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault([this]() {
      return route_config_.get();
    });
    ON_CALL(decoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
        .WillByDefault([this](std::function<void()> cb) {
          return new NiceMock<Event::MockSchedulableCallback>(&decoder_callbacks_.dispatcher_, cb);
        });

    ON_CALL(encoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault([this]() {
      return route_config_.get();
    });
    ON_CALL(encoder_callbacks_.dispatcher_, post(_)).WillByDefault([this](Event::PostCb cb) {
      posted_callbacks_.push_back(std::move(cb));
    });
    ON_CALL(encoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
        .WillByDefault([this](std::function<void()> cb) {
          auto* scb =
              new NiceMock<Event::MockSchedulableCallback>(&encoder_callbacks_.dispatcher_, cb);
          auto schedule = [this, cb]() { posted_callbacks_.push_back(cb); };
          ON_CALL(*scb, scheduleCallbackCurrentIteration()).WillByDefault(schedule);
          ON_CALL(*scb, scheduleCallbackNextIteration()).WillByDefault(schedule);
          return scb;
        });
    ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
        .WillByDefault([this](Buffer::Instance& data, bool end_stream) {
          result_.body.append(data.toString());
          result_.end_stream = result_.end_stream || end_stream;
        });
    ON_CALL(encoder_callbacks_, sendLocalReply(_, _, _, _, _))
        .WillByDefault([this](Http::Code code, absl::string_view,
                              std::function<void(Http::ResponseHeaderMap&)>,
                              std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
          result_.local_reply_code = code;
          ++result_.local_reply_calls;
        });
    ON_CALL(encoder_callbacks_.stream_info_, setDynamicTypedMetadata(_, _))
        .WillByDefault([this](const std::string&, const Protobuf::Any& value) {
          FUZZ_ASSERT(value.UnpackTo(&result_.published_usage.emplace()));
        });
  }

  Result run(const EncodeUnaryFuzzInput& input, bool fragmented) {
    result_ = {};
    decoder_callbacks_.stream_info_.filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::FilterChain);

    PerRouteProto route_proto;
    route_proto.mutable_request()->set_llm_protocol(input.request_protocol());
    route_proto.mutable_response()->set_llm_protocol(input.response_protocol());
    route_config_ = std::move(*RouteConfig::create(route_proto));

    AiProtocolManagerProto filter_proto;
    filter_proto.mutable_request_handling();
    auto* limits =
        filter_proto.mutable_response_handling()->mutable_token_usage()->mutable_limits();
    limits->mutable_max_json_body_size()->set_value(1 + (input.max_json_body_size() % 8192));
    AiProtocolManagerFilter filter(std::make_shared<FilterConfig>(
        filter_proto, *stats_store_.rootScope(), ai_filter_factories_, buffer_factory_));
    filter.setDecoderFilterCallbacks(decoder_callbacks_);
    filter.setEncoderFilterCallbacks(encoder_callbacks_);

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
    filter.decodeHeaders(request_headers, /*end_stream=*/false);
    Buffer::OwnedImpl request_body(R"({"model":"gpt-4"})");
    filter.decodeData(request_body, /*end_stream=*/true);

    Http::TestResponseHeaderMapImpl response_headers{{":status", "200"},
                                                     {"content-type", "application/json"}};
    filter.encodeHeaders(response_headers, /*end_stream=*/false);

    absl::string_view body = input.body();
    size_t offset = 0;
    if (fragmented) {
      uint64_t seed = input.fragmentation_seed();
      size_t max_chunk_size = (seed & 1ULL) != 0 ? 7 : 97;
      uint64_t state = seed | 1;
      while (offset < body.size()) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        size_t chunk_size =
            std::min<size_t>(1 + (state >> 33) % max_chunk_size, body.size() - offset);
        Buffer::OwnedImpl chunk(body.substr(offset, chunk_size));
        filter.encodeData(chunk, /*end_stream=*/false);
        runPostedCallbacks();
        offset += chunk_size;
      }
    }
    Buffer::OwnedImpl tail(body.substr(offset));
    filter.encodeData(tail, /*end_stream=*/true);
    runPostedCallbacks();
    filter.onDestroy();
    return result_;
  }

private:
  void runPostedCallbacks() {
    while (!posted_callbacks_.empty()) {
      Event::PostCb cb = std::move(posted_callbacks_.front());
      posted_callbacks_.pop_front();
      cb();
    }
  }

  Stats::TestUtil::TestStore stats_store_;
  ExternalBufferFactorySharedPtr buffer_factory_{std::make_shared<InMemoryExternalBufferFactory>()};
  AiFilterFactories ai_filter_factories_;
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  std::deque<Event::PostCb> posted_callbacks_;
  std::unique_ptr<RouteConfig> route_config_;
  Result result_;
};

DEFINE_PROTO_FUZZER(const EncodeUnaryFuzzInput& input) {
  if (input.body().size() > kMaxInputSize) {
    return;
  }

  static UnaryResponseRunner runner;
  UnaryResponseRunner::Result fragmented = runner.run(input, /*fragmented=*/true);
  UnaryResponseRunner::Result whole = runner.run(input, /*fragmented=*/false);

  FUZZ_ASSERT(fragmented.local_reply_calls == whole.local_reply_calls);
  FUZZ_ASSERT(fragmented.local_reply_code == whole.local_reply_code);
  if (fragmented.local_reply_calls == 0) {
    FUZZ_ASSERT(fragmented.end_stream && whole.end_stream);
    FUZZ_ASSERT(fragmented.body == whole.body);
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
