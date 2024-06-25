#include "source/extensions/filters/http/grpc_json_transcoder/config.h"

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

Http::FilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  JsonTranscoderConfigSharedPtr filter_config =
      std::make_shared<JsonTranscoderConfig>(proto_config, context.serverFactoryContext().api());
  auto stats = std::make_shared<GrpcJsonTranscoderFilterStats>(
      GrpcJsonTranscoderFilterStats::generateStats(stats_prefix, context.scope()));
  return [filter_config, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<JsonTranscoderFilter>(filter_config, stats));
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
GrpcJsonTranscoderFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {

  return std::make_shared<JsonTranscoderConfig>(proto_config, context.api());
}

/**
 * Static registration for the grpc transcoding filter. @see RegisterNamedHttpFilterConfigFactory.
 */
LEGACY_REGISTER_FACTORY(GrpcJsonTranscoderFilterConfig,
                        Server::Configuration::NamedHttpFilterConfigFactory,
                        "envoy.grpc_json_transcoder");

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
