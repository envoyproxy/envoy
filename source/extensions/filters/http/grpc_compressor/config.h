#pragma once

#include "envoy/extensions/filters/http/grpc_compressor/v3/compressor.pb.h"
#include "envoy/extensions/filters/http/grpc_compressor/v3/compressor.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GrpcCompressor {

/**
 * Config registration for the gRPC compressor filter. @see NamedHttpFilterConfigFactory.
 */
class GrpcCompressorFilterFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::grpc_compressor::v3::Compressor> {
public:
  GrpcCompressorFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.grpc_compressor") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::grpc_compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(GrpcCompressorFilterFactory);

} // namespace GrpcCompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
