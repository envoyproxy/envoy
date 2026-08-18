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
    : public Common::DualFactoryBase<
          envoy::extensions::filters::http::compressor::v3::Compressor,
          envoy::extensions::filters::http::compressor::v3::CompressorPerRoute> {
public:
  CompressorFilterFactory() : DualFactoryBase("envoy.filters.http.compressor") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, DualInfo info,
      Server::Configuration::ServerFactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;

  absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::compressor::v3::Compressor& proto_config,
      const std::string& stats_prefix, Server::Configuration::GenericFactoryContext& context);

  absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
  createRouteSpecificFilterConfigTyped(
      const envoy::extensions::filters::http::compressor::v3::CompressorPerRoute& proto_config,
      Server::Configuration::ServerFactoryContext& context,
      ProtobufMessage::ValidationVisitor& validator) override;
};

using UpstreamCompressorFilterFactory = CompressorFilterFactory;

DECLARE_FACTORY(CompressorFilterFactory);
DECLARE_FACTORY(UpstreamCompressorFilterFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
