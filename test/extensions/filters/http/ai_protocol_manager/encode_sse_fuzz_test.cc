#include <memory>
#include <utility>

#include "envoy/extensions/http/ai_filters/transcoder/v3/transcoder.pb.h"

#include "source/extensions/filters/http/ai_protocol_manager/transcoding_engine.h"
#include "source/extensions/http/ai_filters/transcoder/filter.h"

#include "test/extensions/filters/http/ai_protocol_manager/fuzz.pb.h"
#include "test/extensions/filters/http/ai_protocol_manager/fuzz_lib.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using AiFilters::Transcoder::TranscoderFilter;
using AiFilters::Transcoder::TranscoderFilterConfig;
using EncodeSseFuzzInput =
    envoy::extensions::filters::http::ai_protocol_manager::EncodeSseFuzzInput;
using TranscoderProto = envoy::extensions::http::ai_filters::transcoder::v3::Transcoder;

AiFilterFactories createAiFilterFactories(Stats::Scope& scope) {
  TranscoderProto from_ir_proto;
  from_ir_proto.set_response_handling(TranscoderProto::FROM_IR);
  auto from_ir_config = std::make_shared<TranscoderFilterConfig>(
      from_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

  TranscoderProto to_ir_proto;
  to_ir_proto.set_response_handling(TranscoderProto::TO_IR);
  auto to_ir_config = std::make_shared<TranscoderFilterConfig>(
      to_ir_proto, std::move(*TranscodingEngine::createDefault()), scope);

  return AiFilterFactories{
      [from_ir_config](const AiFilterContext& ctx) {
        return std::make_shared<TranscoderFilter>(from_ir_config, ctx);
      },
      [to_ir_config](const AiFilterContext& ctx) {
        return std::make_shared<TranscoderFilter>(to_ir_config, ctx);
      },
  };
}

FuzzRunner::Result run(const RouteConfig& route_config, const EncodeSseFuzzInput& input,
                       bool fragmented) {
  static FuzzRunner runner;
  static const AiFilterFactories ai_filter_factories = createAiFilterFactories(runner.scope());

  AiProtocolManagerProto filter_proto;
  filter_proto.mutable_request_handling();
  auto* limits = filter_proto.mutable_response_handling()->mutable_token_usage()->mutable_limits();
  limits->mutable_max_sse_event_size()->set_value(1 + (input.max_sse_event_size() % 4096));
  limits->mutable_max_parsed_sse_events()->set_value(1 + (input.max_parsed_sse_events() % 16));

  runner.createFilter(route_config, filter_proto, ai_filter_factories);
  runner.decodeHeaders({{":method", "POST"},
                        {":path", "/v1/chat/completions"},
                        {"content-type", "application/json"}});
  runner.decodeBody(R"({"model":"gpt-4"})");
  runner.encodeHeaders({{":status", "200"}, {"content-type", "text/event-stream"}});
  runner.encodeBody(input.body(), input.fragmentation_seed(), fragmented);
  return runner.finish();
}

DEFINE_PROTO_FUZZER(const EncodeSseFuzzInput& input) {
  if (input.body().size() > kMaxInputSize) {
    return;
  }

  PerRouteProto route_proto;
  route_proto.mutable_request()->set_llm_protocol(input.request_protocol());
  route_proto.mutable_response()->set_llm_protocol(input.response_protocol());
  auto route_config = std::move(*RouteConfig::create(route_proto));

  FuzzRunner::Result fragmented = run(*route_config, input, /*fragmented=*/true);
  FuzzRunner::Result whole = run(*route_config, input, /*fragmented=*/false);
  FuzzRunner::verifyEquivalent(fragmented, whole);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
