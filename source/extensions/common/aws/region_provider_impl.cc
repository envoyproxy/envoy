
#include "source/extensions/common/aws/region_provider_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

// static const char AWS_REGION[] = "AWS_REGION";

absl::optional<std::string> EnvoyConfigRegionProvider::getRegion() { 
    ENVOY_LOG_MISC(debug, "called EnvoyConfigRegionProvider::getRegion");
return "envoyconfig"; }

absl::optional<std::string> EnvironmentRegionProvider::getRegion() {     ENVOY_LOG_MISC(debug, "called EnvironmentRegionProvider::getRegion");
return "environment"; }

absl::optional<std::string> AWSProfileRegionProvider::getRegion() {     ENVOY_LOG_MISC(debug, "called AWSProfileRegionProvider::getRegion");
return "profile"; }
absl::optional<std::string> AWSConfigRegionProvider::getRegion() {     ENVOY_LOG_MISC(debug, "called AWSConfigRegionProvider::getRegion");
return "config"; }

RegionProviderChain::RegionProviderChain(const RegionProviderChainFactories& factories) {
  add(factories.createEnvironmentRegionProvider());
  add(factories.createEnvoyConfigRegionProvider());
  add(factories.createAWSProfileRegionProvider());
  add(factories.createAWSConfigRegionProvider());
}

absl::optional<std::string> RegionProviderChain::getRegion() {
  ENVOY_LOG_MISC(debug, "called RegionProviderChain::getRegion");
  for (auto& provider : providers_) {
    const auto region = provider->getRegion();
      return region;
  }
  return absl::nullopt;
}

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
