#pragma once

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

/**
 * Config registration for the gRPC JSON transcoder filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcJsonTranscoderFilterConfig
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder> {
public:
  GrpcJsonTranscoderFilterConfig()
      : UnifiedFactoryBase("envoy.filters.http.grpc_json_transcoder") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
