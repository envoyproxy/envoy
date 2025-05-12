#include "source/extensions/geoip_providers/maxmind/config.h"

#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

using ConfigProto = envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig;

/**
 * A singleton that acts as a factory for generating and looking up GeoipProviders.
 * When given equivalent provider configs, the singleton returns pointers to the same
 * driver/provider. When given different configs, the singleton returns different provider
 * instances.
 */
class DriverSingleton : public Envoy::Singleton::Instance {
public:
  std::shared_ptr<GeoipProvider> get(std::shared_ptr<DriverSingleton> singleton,
                                     const ConfigProto& proto_config,
                                     const std::string& stat_prefix,
                                     Server::Configuration::FactoryContext& context) {
    std::shared_ptr<GeoipProvider> driver;
    const uint64_t key = MessageUtil::hash(proto_config);
    absl::MutexLock lock(&mu_);
    auto it = drivers_.find(key);
    if (it != drivers_.end()) {
      driver = it->second.lock();
    } else {
      const auto& provider_config =
          std::make_shared<GeoipProviderConfig>(proto_config, stat_prefix, context.scope());
      driver = std::make_shared<GeoipProvider>(
          context.serverFactoryContext().mainThreadDispatcher(),
          context.serverFactoryContext().api(), singleton, provider_config);
      drivers_[key] = driver;
    }
    return driver;
  }

private:
  absl::Mutex mu_;
  // We keep weak_ptr here so the providers can be destroyed if the config is updated to stop using
  // that config of the provider. Each provider stores shared_ptrs to this singleton, which keeps
  // the singleton from being destroyed unless it's no longer keeping track of any providers. (The
  // singleton shared_ptr is *only* held by driver instances.)
  absl::flat_hash_map<uint64_t, std::weak_ptr<GeoipProvider>> drivers_ ABSL_GUARDED_BY(mu_);
};

SINGLETON_MANAGER_REGISTRATION(maxmind_geolocation_provider_singleton);

MaxmindProviderFactory::MaxmindProviderFactory() : FactoryBase("envoy.geoip_providers.maxmind") {}

DriverSharedPtr MaxmindProviderFactory::createGeoipProviderDriverTyped(
    const ConfigProto& proto_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& context) {
  std::shared_ptr<DriverSingleton> drivers =
      context.serverFactoryContext().singletonManager().getTyped<DriverSingleton>(
          SINGLETON_MANAGER_REGISTERED_NAME(maxmind_geolocation_provider_singleton),
          [] { return std::make_shared<DriverSingleton>(); });
  return drivers->get(drivers, proto_config, stat_prefix, context);
}

/**
 * Static registration for the Maxmind provider. @see RegisterFactory.
 */
REGISTER_FACTORY(MaxmindProviderFactory, Geolocation::GeoipProviderFactory);

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
