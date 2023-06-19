#include "source/extensions/network/dns_resolver/apple/apple_dns_impl.h"

#include "extension_registry_platform_additions.h"

namespace Envoy {

void ExtensionRegistryPlatformAdditions::registerFactories() {
  Envoy::Network::forceRegisterAppleDnsResolverFactory();
}

} // namespace Envoy
