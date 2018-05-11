#pragma once

#include "envoy/config/filter/http/gzip/v2/gzip.pb.h"

#include "extensions/filters/http/common/factory_base.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

/**
 * Config registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
class GzipFilterFactory : public Common::FactoryBase<envoy::config::filter::http::gzip::v2::Gzip> {
public:
  GzipFilterFactory() : FactoryBase(HttpFilterNames::get().ENVOY_GZIP) {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::config::filter::http::gzip::v2::Gzip& config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
};

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
