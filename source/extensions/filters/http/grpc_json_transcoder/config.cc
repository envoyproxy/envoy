#include "extensions/filters/http/grpc_json_transcoder/config.h"

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

Http::FilterFactoryCb GrpcJsonTranscoderFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  JsonTranscoderConfigSharedPtr filter_config =
      std::make_shared<JsonTranscoderConfig>(proto_config, context.api());

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<JsonTranscoderFilter>(*filter_config));
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
REGISTER_FACTORY(GrpcJsonTranscoderFilterConfig,
                 Server::Configuration::NamedHttpFilterConfigFactory){"envoy.grpc_json_transcoder"};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
