#pragma once

#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

/**
 * Config registration for the decompressor filter. @see NamedHttpFilterConfigFactory.
 */
class DecompressorFilterFactory
    : public Common::ExceptionFreeFactoryBase<
          envoy::extensions::filters::http::decompressor::v3::Decompressor> {
public:
  DecompressorFilterFactory() : ExceptionFreeFactoryBase("envoy.filters.http.decompressor") {}

private:
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;

  absl::StatusOr<Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& config,
      const std::string& stats_prefix,
      Server::Configuration::ServerFactoryContext& context) override;

  // Shared factory creation used by both the downstream (FactoryContext) and route/vhost-level
  // (ServerFactoryContext) paths.
  absl::StatusOr<Http::FilterFactoryCb> createFilterFactory(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& proto_config,
      const std::string& stats_prefix, Server::Configuration::GenericFactoryContext& context);
};

DECLARE_FACTORY(DecompressorFilterFactory);

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
