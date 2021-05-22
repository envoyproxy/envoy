#include <string>

#include "extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/route_cache_reset/filter.pb.h"
#include "library/common/extensions/filters/http/route_cache_reset/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

/**
 * Config registration for the route_cache_reset filter. @see NamedHttpFilterConfigFactory.
 */
class RouteCacheResetFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset> {
public:
  RouteCacheResetFilterFactory() : FactoryBase("route_cache_reset") {}

private:
  ::Envoy::Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(RouteCacheResetFilterFactory);

} // namespace RouteCacheReset
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
