#pragma once

#include <string>

#include "extensions/filters/http/common/factory_base.h"

#include "library/common/extensions/filters/http/platform_bridge/filter.pb.h"
#include "library/common/extensions/filters/http/platform_bridge/filter.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace PlatformBridge {

/**
 * Config registration for the platform bridge filter. @see NamedHttpFilterConfigFactory.
 */
class PlatformBridgeFilterFactory
    : public Common::FactoryBase<
          envoymobile::extensions::filters::http::platform_bridge::PlatformBridge> {
public:
  PlatformBridgeFilterFactory() : FactoryBase("platform_bridge") {}

private:
  Http::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& config,
      const std::string& stats_prefix, Server::Configuration::FactoryContext& context) override;
};

DECLARE_FACTORY(PlatformBridgeFilterFactory);

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
