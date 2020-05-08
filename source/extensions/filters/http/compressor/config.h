#pragma once

#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.h"
#include "envoy/extensions/filters/http/compressor/v3/compressor.pb.validate.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Compressor {

/**
 * Config registration for the compressor filter. @see NamedHttpFilterConfigFactory.
 */
class CompressorFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::compressor::v3::Compressor> {
public:
  CompressorFilterFactory() : FactoryBase(HttpFilterNames::get().Compressor) {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::extensions::filters::http::compressor::v3::Compressor& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(CompressorFilterFactory);

} // namespace Compressor
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
