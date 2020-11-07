#pragma once

#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.h"
#include "envoy/extensions/filters/http/decompressor/v3/decompressor.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Decompressor {

/**
 * Config registration for the decompressor filter. @see NamedHttpFilterConfigFactory.
 */
class DecompressorFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::decompressor::v3::Decompressor> {
public:
  DecompressorFilterFactory() : FactoryBase(HttpFilterNames::get().Decompressor) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::decompressor::v3::Decompressor& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(DecompressorFilterFactory);

} // namespace Decompressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy