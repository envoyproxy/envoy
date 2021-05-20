#pragma once

#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_transcoder/v3/transcoder.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonTranscoder {

/**
 * Config registration for the gRPC JSON transcoder filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcJsonTranscoderFilterConfig
    : public Common::FactoryBase<
          envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder> {
public:
  GrpcJsonTranscoderFilterConfig() : FactoryBase(HttpFilterNames::get().GrpcJsonTranscoder) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&
          proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::grpc_json_transcoder::v3::GrpcJsonTranscoder&,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

} // namespace GrpcJsonTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
