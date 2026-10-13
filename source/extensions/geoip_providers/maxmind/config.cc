#include "source/extensions/geoip_providers/maxmind/config.h"

#include "envoy/common/exception.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/registry/registry.h"

#include "source/common/common/thread.h"
#include "source/common/common/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

using ConfigProto = envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig;

namespace {

// Key that identifies a provider. Two listeners that share a provider config and stats namespace
// will be considered the same provider.
std::string driverKey(const ConfigProto& proto_config, const std::string& stat_prefix,
                      Stats::Scope& scope) {
  return absl::StrCat(absl::Base64Escape(scope.symbolTable().toString(scope.prefix())), "|",
                      absl::Base64Escape(stat_prefix), "|", MessageUtil::hash(proto_config));
}

// Key that identifies one database file. The type is one of a fixed set of tokens that contain no
// separator, so the first separator always ends it and no path can be confused with another.
std::string dbFileKey(GeoDbType db_type, const std::string& db_path) {
  return absl::StrCat(dbTypeName(db_type), "|", db_path);
}

} // namespace

/**
 * A singleton that acts as a factory for generating and looking up GeoipProviders.
 * When given equivalent provider configs from the same stat namespace, the singleton returns
 * pointers to the same driver/provider. Otherwise it returns different provider instances, which
 * still share the databases they look up in whenever they were configured with the same files.
 */
class DriverSingleton : public Envoy::Singleton::Instance {
public:
  DriverSingleton(Stats::Scope& scope) : db_file_stats_scope_(scope.createScope("maxmind.")) {}

  std::shared_ptr<GeoipProvider> get(std::shared_ptr<DriverSingleton> singleton,
                                     const ConfigProto& proto_config,
                                     const std::string& stat_prefix,
                                     Server::Configuration::ServerFactoryContext& context) {
    // Providers are only ever created while loading configuration, on the main thread, so the
    // maps below need no locking.
    ASSERT_IS_MAIN_OR_TEST_THREAD();
    const std::string key = driverKey(proto_config, stat_prefix, context.scope());
    auto it = drivers_.find(key);
    if (it != drivers_.end()) {
      // The map holds weak_ptrs, so a present entry may refer to a provider that has already been
      // destroyed. Only reuse it if it is still alive; otherwise fall through and build a new one.
      std::shared_ptr<GeoipProvider> driver = it->second.lock();
      if (driver != nullptr) {
        return driver;
      }
    }
    // Nothing prunes the map on provider destruction, so drop the expired entries before adding
    // another one. Otherwise it grows without bound across config updates that change the provider
    // config, since every distinct config keys to a new entry.
    absl::erase_if(drivers_, [](const auto& entry) { return entry.second.expired(); });

    // Load the database files first, so that a configuration that names none of them, or names
    // one that cannot be opened, is rejected before anything is cached.
    DbFileProviders db_file_providers = getDbFileProviders(proto_config, context);
    const auto provider_config =
        std::make_shared<GeoipProviderConfig>(proto_config, stat_prefix, context.scope());
    std::shared_ptr<GeoipProvider> driver =
        std::make_shared<GeoipProvider>(singleton, provider_config, std::move(db_file_providers));
    drivers_[key] = driver;
    return driver;
  }

private:
  DbFileProviders getDbFileProviders(const ConfigProto& proto_config,
                                     Server::Configuration::ServerFactoryContext& context) {
    // An empty path means that database is not configured.
    if (proto_config.city_db_path().empty() && proto_config.isp_db_path().empty() &&
        proto_config.anon_db_path().empty() && proto_config.asn_db_path().empty() &&
        proto_config.country_db_path().empty()) {
      throw EnvoyException("At least one geolocation database path needs to be configured: "
                           "city_db_path, isp_db_path, asn_db_path, anon_db_path or "
                           "country_db_path");
    }

    DbFileProviders db_file_providers;
    db_file_providers.city_db_ =
        getDbFileProvider(GeoDbType::City, proto_config.city_db_path(), context);
    db_file_providers.isp_db_ =
        getDbFileProvider(GeoDbType::Isp, proto_config.isp_db_path(), context);
    db_file_providers.anon_db_ =
        getDbFileProvider(GeoDbType::Anon, proto_config.anon_db_path(), context);
    db_file_providers.asn_db_ =
        getDbFileProvider(GeoDbType::Asn, proto_config.asn_db_path(), context);
    db_file_providers.country_db_ =
        getDbFileProvider(GeoDbType::Country, proto_config.country_db_path(), context);
    return db_file_providers;
  }

  DbFileProviderSharedPtr getDbFileProvider(GeoDbType db_type, const std::string& db_path,
                                            Server::Configuration::ServerFactoryContext& context) {
    if (db_path.empty()) {
      return nullptr;
    }
    const std::string key = dbFileKey(db_type, db_path);
    auto it = db_file_providers_.find(key);
    if (it != db_file_providers_.end()) {
      DbFileProviderSharedPtr db_file_provider = it->second.lock();
      if (db_file_provider != nullptr) {
        return db_file_provider;
      }
    }
    // As with drivers_, nothing prunes the map when the last provider referencing a file goes
    // away, so drop the expired entries before adding another one.
    absl::erase_if(db_file_providers_, [](const auto& entry) { return entry.second.expired(); });

    DbFileProviderSharedPtr db_file_provider = std::make_shared<DbFileProvider>(
        context.mainThreadDispatcher(), db_file_stats_scope_, db_type, db_path);
    db_file_providers_[key] = db_file_provider;
    return db_file_provider;
  }

  // We keep weak_ptr here so the providers can be destroyed if the config is updated to stop using
  // that config of the provider. Each provider stores shared_ptrs to this singleton, which keeps
  // the singleton from being destroyed unless it's no longer keeping track of any providers.
  absl::flat_hash_map<std::string, std::weak_ptr<GeoipProvider>> drivers_;
  absl::flat_hash_map<std::string, std::weak_ptr<DbFileProvider>> db_file_providers_;
  const Stats::ScopeSharedPtr db_file_stats_scope_;
};

SINGLETON_MANAGER_REGISTRATION(maxmind_geolocation_provider_singleton);

MaxmindProviderFactory::MaxmindProviderFactory() : FactoryBase("envoy.geoip_providers.maxmind") {}

DriverSharedPtr MaxmindProviderFactory::createGeoipProviderDriverTyped(
    const ConfigProto& proto_config, const std::string& stat_prefix,
    Server::Configuration::ServerFactoryContext& context) {
  std::shared_ptr<DriverSingleton> drivers = context.singletonManager().getTyped<DriverSingleton>(
      SINGLETON_MANAGER_REGISTERED_NAME(maxmind_geolocation_provider_singleton),
      [&context] { return std::make_shared<DriverSingleton>(context.scope()); });
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
