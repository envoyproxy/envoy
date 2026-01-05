#include "source/extensions/wildcard/hostname_lookup/config.h"

#include "envoy/extensions/wildcard/hostname_lookup/v3/hostname_lookup.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"

#include "source/common/common/assert.h"
#include "source/extensions/wildcard/virtual_ip_cache/cache_manager.h"

namespace Envoy {
namespace Extensions {
namespace Wildcard {
namespace HostnameLookup {

absl::StatusOr<Network::FilterFactoryCb> HostnameLookupConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message&, Server::Configuration::FactoryContext& context) {

  // Get the singleton cache manager (must be created in bootstrap)
  auto cache_manager = VirtualIp::VirtualIpCacheManager::singleton(context.serverFactoryContext());

  RELEASE_ASSERT(cache_manager != nullptr,
                 "VirtualIpCacheManager not found. Configure envoy.bootstrap.virtual_ip_cache in "
                 "the bootstrap section.");

  return [cache_manager](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<HostnameLookupFilter>(cache_manager));
  };
}

ProtobufTypes::MessagePtr HostnameLookupConfigFactory::createEmptyConfigProto() {
  return std::make_unique<envoy::extensions::wildcard::hostname_lookup::v3::HostnameLookup>();
}

std::string HostnameLookupConfigFactory::name() const {
  return "envoy.filters.network.hostname_lookup";
}

REGISTER_FACTORY(HostnameLookupConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace HostnameLookup
} // namespace Wildcard
} // namespace Extensions
} // namespace Envoy
