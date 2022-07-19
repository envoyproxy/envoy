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
    : public Common::DownstreamFactoryBase<
          envoy::extensions::filters::http::decompressor::v3::Decompressor> {
public:
  DecompressorFilterFactory() : DownstreamFactoryBase("envoy.filters.http.decompressor") {}

private:
  Http::FilterFactoryCb createDownstreamFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(DecompressorFilterFactory);

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
