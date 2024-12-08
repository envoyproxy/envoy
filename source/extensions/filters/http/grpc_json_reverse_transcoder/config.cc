#include "source/extensions/filters/http/grpc_json_reverse_transcoder/config.h"

#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter.h"
#include "source/extensions/filters/http/grpc_json_reverse_transcoder/filter_config.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

Http::FilterFactoryCb GrpcJsonReverseTranscoderFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
        GrpcJsonReverseTranscoder& proto_config,
    const std::string&, Server::Configuration::FactoryContext& context) {
  std::shared_ptr<GrpcJsonReverseTranscoderConfig> filter_config =
      std::make_shared<GrpcJsonReverseTranscoderConfig>(proto_config,
                                                        context.serverFactoryContext().api());
  return [filter_config](Envoy::Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<GrpcJsonReverseTranscoderFilter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
GrpcJsonReverseTranscoderFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
        GrpcJsonReverseTranscoder& proto_config,
    Envoy::Server::Configuration::ServerFactoryContext& context,
    Envoy::ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<GrpcJsonReverseTranscoderConfig>(proto_config, context.api());
}

/**
 * Static registration for the grpc json reverse transcoder filter. @see RegisterFactory.
 */
REGISTER_FACTORY(GrpcJsonReverseTranscoderFactory,
                 Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
