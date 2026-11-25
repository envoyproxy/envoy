#include <memory>
#include <utility>

#include "envoy/extensions/http/ai_filters/request_info/v3/request_info.pb.h"
#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/request_info/filter.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fuzz.pb.h"
#include "test/extensions/filters/http/ai_protocol_manager/fuzz_lib.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using AiFilters::RequestInfo::RequestInfoFilter;
using AiFilters::RequestInfo::RequestInfoFilterConfig;
using AiFilters::Transcoder::TranscoderFilter;
using AiFilters::Transcoder::TranscoderFilterConfig;
using DecodeFuzzInput = envoy::extensions::filters::http::ai_protocol_manager::DecodeFuzzInput;
using RequestInfoConfigProto = envoy::extensions::http::ai_filters::request_info::v3::RequestInfo;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

AiFilterFactories createAiFilterFactories(Stats::Scope& scope) {
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

  return AiFilterFactories{
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
}

FuzzRunner::Result run(const RouteConfig& route_config, const DecodeFuzzInput& input,
                       bool fragmented) {
  static FuzzRunner runner;
  static const AiFilterFactories ai_filter_factories = createAiFilterFactories(runner.scope());

  AiProtocolManagerProto filter_proto;
  filter_proto.mutable_request_handling()
      ->mutable_limits()
      ->mutable_inline_string_threshold_bytes()
      ->set_value(64 + (input.inline_string_threshold_bytes() % 4096));

  runner.createFilter(route_config, filter_proto, ai_filter_factories,
                      input.decoder_buffer_limit() % 4096);
  runner.decodeHeaders({{":method", "POST"},
                        {":path", Fuzz::replaceInvalidCharacters(input.path())},
                        {"content-type", "application/json"}});
  runner.decodeBody(input.body(), input.fragmentation_seed(), fragmented);
  return runner.finish();
}

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

  FuzzRunner::Result fragmented = run(**route_config, input, /*fragmented=*/true);
  FuzzRunner::Result whole = run(**route_config, input, /*fragmented=*/false);
  FuzzRunner::verifyEquivalent(fragmented, whole);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
