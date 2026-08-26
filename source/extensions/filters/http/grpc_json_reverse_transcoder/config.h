#pragma once

#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.h"
#include "envoy/extensions/filters/http/grpc_json_reverse_transcoder/v3/transcoder.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcJsonReverseTranscoder {

class GrpcJsonReverseTranscoderFactory
    : public Common::UnifiedFactoryBase<
          envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
              GrpcJsonReverseTranscoder> {
public:
  GrpcJsonReverseTranscoderFactory()
      : UnifiedFactoryBase("envoy.filters.http.grpc_json_reverse_transcoder") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_json_reverse_transcoder::v3::
          GrpcJsonReverseTranscoder& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

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
