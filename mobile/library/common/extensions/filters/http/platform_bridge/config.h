#pragma once

#include <string>

#include "source/extensions/filters/http/common/factory_base.h"

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
    : public Common::UnifiedFactoryBase<
          envoymobile::extensions::filters::http::platform_bridge::PlatformBridge> {
public:
  PlatformBridgeFilterFactory() : UnifiedFactoryBase("platform_bridge") {}

private:
  absl::StatusOr<::Envoy::Http::FilterFactoryCb> createHttpFilterFactoryFromProtoTyped(
      const envoymobile::extensions::filters::http::platform_bridge::PlatformBridge& config,
      Server::Configuration::ServerFactoryContext& context,
      Server::Configuration::ExtraFactoryContext& extra_context) override;
};

DECLARE_FACTORY(PlatformBridgeFilterFactory);

} // namespace PlatformBridge
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
