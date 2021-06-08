#pragma once

#include "envoy/extensions/filters/http/gzip/v3/gzip.pb.h"
#include "envoy/extensions/filters/http/gzip/v3/gzip.pb.validate.h"

#include "source/extensions/filters/http/common/factory_base.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Gzip {

/**
 * Config registration for the gzip filter. @see NamedHttpFilterConfigFactory.
 */
class GzipFilterFactory
    : public Common::FactoryBase<envoy::extensions::filters::http::gzip::v3::Gzip> {
public:
  GzipFilterFactory() : FactoryBase("envoy.filters.http.gzip") {}

private:
  Http::FilterFactoryCb
  createFilterFactoryFromProtoTyped(const envoy::extensions::filters::http::gzip::v3::Gzip& config,
                                    const std::string& stats_prefix,
                                    Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(GzipFilterFactory);

} // namespace Gzip
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
