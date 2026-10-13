#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

#include "test/common/http/filters/route_cache_reset/filter.pb.h"
#include "test/common/http/filters/route_cache_reset/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RouteCacheReset {

/**
 * Config registration for the route_cache_reset filter. @see NamedHttpFilterConfigFactory.
 */
class RouteCacheResetFilterFactory
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset> {
public:
  RouteCacheResetFilterFactory() : UnifiedFactoryBase("route_cache_reset") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::route_cache_reset::RouteCacheReset& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(RouteCacheResetFilterFactory);

} // namespace RouteCacheReset
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
