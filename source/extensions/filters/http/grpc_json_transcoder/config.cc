#include "source/extensions/filters/http/grpc_json_transcoder/config.h"

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/http/grpc_json_transcoder/json_transcoder_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

absl::StatusOr<Http::FilterFactoryCb>
GrpcJsonTranscoderFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  JsonTranscoderConfigSharedPtr filter_config = std::make_shared<JsonTranscoderConfig>(
      proto_config, context.serverFactoryContext().api(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  auto stats = std::make_shared<GrpcJsonTranscoderFilterStats>(
      GrpcJsonTranscoderFilterStats::generateStats(stats_prefix, context.scope()));
  return [filter_config, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<JsonTranscoderFilter>(filter_config, stats));
  };
}

absl::StatusOr<Http::FilterFactoryCb>
GrpcJsonTranscoderFilterConfig::createHttpFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    const std::string& stats_prefix, Server::Configuration::ServerFactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  JsonTranscoderConfigSharedPtr filter_config =
      std::make_shared<JsonTranscoderConfig>(proto_config, context.api(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  auto stats = std::make_shared<GrpcJsonTranscoderFilterStats>(
      GrpcJsonTranscoderFilterStats::generateStats(stats_prefix, context.scope()));
  return [filter_config, stats](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<JsonTranscoderFilter>(filter_config, stats));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
GrpcJsonTranscoderFilterConfig::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
        proto_config,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {

  absl::Status creation_status = absl::OkStatus();
  auto filter_config =
      std::make_shared<JsonTranscoderConfig>(proto_config, context.api(), creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return filter_config;
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
