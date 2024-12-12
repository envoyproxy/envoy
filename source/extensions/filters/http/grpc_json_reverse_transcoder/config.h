#pragma once

#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

class GrpcJsonReverseTranscoderFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::grpc_json_reverse_transcoder::
                                     v3::GrpcJsonReverseTranscoder> {
public:
  GrpcJsonReverseTranscoderFactory()
      : FactoryBase("envoy.filters.http.grpc_json_reverse_transcoder") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
          GrpcJsonReverseTranscoder& proto_config,
      const std::string&, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
          GrpcJsonReverseTranscoder& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor&) override;
};

} // namespace GrpcJsonReverseTranscoder
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
