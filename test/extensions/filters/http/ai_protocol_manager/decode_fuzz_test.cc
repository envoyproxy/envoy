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

#include "envoy/data/ai/v3/request_info.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/extensions/filters/http/ai_protocol_manager/v3/ai_protocol_manager.pb.h"
#include "envoy/extensions/http/ai_filters/request_info/v3/request_info.pb.h"
#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"
#include "envoy/http/codes.h"
#include "envoy/http/filter.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/ai_filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/filter.h"
#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/request_info/filter.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/common/stats/stat_test_utility.h"
#include "test/extensions/filters/http/ai_protocol_manager/decode_fuzz.pb.h"
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

using AiFilters::RequestInfo::RequestInfoFilter;
using AiFilters::RequestInfo::RequestInfoFilterConfig;
using AiFilters::Transcoder::TranscoderFilter;
using AiFilters::Transcoder::TranscoderFilterConfig;
using AiProtocolManagerProto =
    envoy::extensions::filters::http::ai_protocol_manager::v3::AiProtocolManager;
using DecodeFuzzInput = envoy::extensions::filters::http::ai_protocol_manager::DecodeFuzzInput;
using RequestInfoConfigProto = envoy::extensions::http::ai_filters::request_info::v3::RequestInfo;
using RequestInfoProto = envoy::data::ai::v3::RequestInfo;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;
using ::testing::_;
using ::testing::NiceMock;

constexpr size_t kMaxInputSize = 64 * 1024;

// Drives JSON requests through AiProtocolManagerFilter's decode pipeline.
class DecodeRequestRunner {
public:
  struct Result {
    Http::TestRequestHeaderMapImpl headers;
    std::string body;
    bool end_stream{false};
    int local_reply_calls{0};
    Http::Code local_reply_code{};
    std::optional<RequestInfoProto> published_request_info;
  };

  DecodeRequestRunner() {
    Stats::Scope& scope = *stats_store_.rootScope();
    RequestInfoConfigProto request_info_proto;
    request_info_proto.mutable_token_estimation()->set_tokens_per_byte(0.25);
    auto request_info_config = std::make_shared<RequestInfoFilterConfig>(request_info_proto, scope);

    TranscoderProto to_ir_proto;
    to_ir_proto.set_request_handling(TranscoderProto::TO_IR);
    auto to_ir_config = std::make_shared<TranscoderFilterConfig>(
        to_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

    TranscoderProto from_ir_proto;
    from_ir_proto.set_request_handling(TranscoderProto::FROM_IR);
    auto from_ir_config = std::make_shared<TranscoderFilterConfig>(
        from_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

    ai_filter_factories_ = AiFilterFactories{
        [request_info_config](const AiFilterContext& ctx) {
          return std::make_shared<RequestInfoFilter>(request_info_config, ctx);
        },
        [to_ir_config](const AiFilterContext& ctx) {
          return std::make_shared<TranscoderFilter>(to_ir_config, ctx);
        },
        [from_ir_config](const AiFilterContext& ctx) {
          return std::make_shared<TranscoderFilter>(from_ir_config, ctx);
        },
    };

    // MockStreamInfo defaults start_time_ to wall-clock now(). TranscoderFilter snapshots
    // decoder_callbacks_.streamInfo().startTime() at construction (during finalizeDecode()) and
    // emits it as OpenAI's "created" field in encodeSSE(), so pin it across runs.
    decoder_callbacks_.stream_info_.start_time_ = SystemTime(std::chrono::seconds(1700000000));
    ON_CALL(decoder_callbacks_, mostSpecificPerFilterConfig()).WillByDefault([this]() {
      return route_config_;
    });
    ON_CALL(decoder_callbacks_, bufferLimit()).WillByDefault([this]() {
      return decoder_buffer_limit_;
    });
    ON_CALL(decoder_callbacks_.dispatcher_, post(_)).WillByDefault([this](Event::PostCb cb) {
      posted_callbacks_.push_back(std::move(cb));
    });
    ON_CALL(decoder_callbacks_.dispatcher_, createSchedulableCallback_(_))
        .WillByDefault([this](std::function<void()> cb) {
          auto* scb =
              new NiceMock<Event::MockSchedulableCallback>(&decoder_callbacks_.dispatcher_, cb);
          auto schedule = [this, cb]() { posted_callbacks_.push_back(cb); };
          ON_CALL(*scb, scheduleCallbackCurrentIteration()).WillByDefault(schedule);
          ON_CALL(*scb, scheduleCallbackNextIteration()).WillByDefault(schedule);
          return scb;
        });
    ON_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _))
        .WillByDefault([this](Buffer::Instance& data, bool end_stream) {
          result_.body.append(data.toString());
          result_.end_stream = result_.end_stream || end_stream;
        });
    ON_CALL(decoder_callbacks_, sendLocalReply(_, _, _, _, _))
        .WillByDefault([this](Http::Code code, absl::string_view,
                              std::function<void(Http::ResponseHeaderMap&)>,
                              std::optional<Grpc::Status::GrpcStatus>, absl::string_view) {
          result_.local_reply_code = code;
          ++result_.local_reply_calls;
        });
    ON_CALL(decoder_callbacks_.stream_info_, setDynamicTypedMetadata(_, _))
        .WillByDefault([this](const std::string&, const Protobuf::Any& value) {
          FUZZ_ASSERT(value.UnpackTo(&result_.published_request_info.emplace()));
        });
  }

  Result run(const RouteConfig& route_config, const DecodeFuzzInput& input, bool fragmented) {
    result_ = {};
    route_config_ = &route_config;
    decoder_buffer_limit_ = input.decoder_buffer_limit() % 4096;
    decoder_callbacks_.stream_info_.filter_state_ = std::make_shared<StreamInfo::FilterStateImpl>(
        StreamInfo::FilterState::LifeSpan::FilterChain);

    AiProtocolManagerProto filter_proto;
    filter_proto.mutable_request_handling()
        ->mutable_limits()
        ->mutable_inline_string_threshold_bytes()
        ->set_value(64 + (input.inline_string_threshold_bytes() % 4096));
    AiProtocolManagerFilter filter(std::make_shared<FilterConfig>(
        filter_proto, *stats_store_.rootScope(), ai_filter_factories_, buffer_factory_));
    filter.setDecoderFilterCallbacks(decoder_callbacks_);

    Http::TestRequestHeaderMapImpl request_headers{
        {":method", "POST"},
        {":path", Fuzz::replaceInvalidCharacters(input.path())},
        {"content-type", "application/json"},
    };
    filter.decodeHeaders(request_headers, /*end_stream=*/false);

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
        filter.decodeData(chunk, /*end_stream=*/false);
        runPostedCallbacks();
        offset += chunk_size;
      }
    }
    Buffer::OwnedImpl tail(body.substr(offset));
    filter.decodeData(tail, /*end_stream=*/true);
    runPostedCallbacks();
    filter.onDestroy();
    result_.headers = std::move(request_headers);
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
  const RouteConfig* route_config_{nullptr};
  uint32_t decoder_buffer_limit_{0};
  std::deque<Event::PostCb> posted_callbacks_;
  Result result_;
};

DEFINE_PROTO_FUZZER(const DecodeFuzzInput& input) {
  if (input.body().size() > kMaxInputSize) {
    return;
  }

  PerRouteProto route_proto;
  route_proto.mutable_request()->set_llm_protocol(input.request_protocol());
  if (input.has_request_binding()) {
    *route_proto.mutable_request()->mutable_binding() = input.request_binding();
    route_proto.mutable_request()->mutable_binding()->set_uri_pattern(
        Fuzz::replaceInvalidCharacters(input.request_binding().uri_pattern()));
  }
  route_proto.mutable_response()->set_llm_protocol(input.response_protocol());
  if (input.has_response_binding()) {
    *route_proto.mutable_response()->mutable_binding() = input.response_binding();
    route_proto.mutable_response()->mutable_binding()->set_uri_pattern(
        Fuzz::replaceInvalidCharacters(input.response_binding().uri_pattern()));
  }
  absl::StatusOr<std::unique_ptr<RouteConfig>> route_config = RouteConfig::create(route_proto);
  if (!route_config.ok()) {
    return;
  }

  static DecodeRequestRunner runner;
  DecodeRequestRunner::Result fragmented = runner.run(**route_config, input, /*fragmented=*/true);
  DecodeRequestRunner::Result whole = runner.run(**route_config, input, /*fragmented=*/false);

  FUZZ_ASSERT(fragmented.local_reply_calls == whole.local_reply_calls);
  FUZZ_ASSERT(fragmented.local_reply_code == whole.local_reply_code);
  if (fragmented.local_reply_calls == 0) {
    FUZZ_ASSERT(fragmented.end_stream && whole.end_stream);
    FUZZ_ASSERT(fragmented.body == whole.body);
    FUZZ_ASSERT(fragmented.headers == whole.headers);
  }
  FUZZ_ASSERT(fragmented.published_request_info.has_value() ==
              whole.published_request_info.has_value());
  if (fragmented.published_request_info.has_value()) {
    FUZZ_ASSERT(
        TestUtility::protoEqual(*fragmented.published_request_info, *whole.published_request_info));
  }
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
