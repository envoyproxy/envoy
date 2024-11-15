#pragma once

#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

/**
 * Config registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
class CompressorFilterFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::compressor::v3::Compressor,
          envoy::extensions::filters::http::compressor::v3::CompressorPerRoute> {
public:
  CompressorFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.compressor") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  Router::RouteSpecificFilterConfigConstSharedPtr createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

DECLARE_FACTORY(CompressorFilterFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
