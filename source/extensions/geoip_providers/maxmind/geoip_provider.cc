#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

namespace {
static constexpr const char* MMDB_CITY_LOOKUP_ARGS[] = {"city", "names", "en"};
static constexpr const char* MMDB_REGION_LOOKUP_ARGS[] = {"subdivisions", "0", "iso_code"};
static constexpr const char* MMDB_COUNTRY_LOOKUP_ARGS[] = {"country", "iso_code"};
static constexpr const char* MMDB_ASN_LOOKUP_ARGS[] = {"autonomous_system_number"};
static constexpr const char* MMDB_ANON_LOOKUP_ARGS[] = {"is_anonymous", "is_anonymous_vpn",
                                                        "is_hosting_provider", "is_tor_exit_node",
                                                        "is_public_proxy"};

static constexpr absl::string_view CITY_DB_TYPE = "city_db";
static constexpr absl::string_view ISP_DB_TYPE = "isp_db";
static constexpr absl::string_view ANON_DB_TYPE = "anon_db";
} // namespace

GeoipProviderConfig::GeoipProviderConfig(
    const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& config,
    const std::string& stat_prefix, Stats::Scope& scope)
    : city_db_path_(!config.city_db_path().empty() ? absl::make_optional(config.city_db_path())
                                                   : absl::nullopt),
      isp_db_path_(!config.isp_db_path().empty() ? absl::make_optional(config.isp_db_path())
                                                 : absl::nullopt),
      anon_db_path_(!config.anon_db_path().empty() ? absl::make_optional(config.anon_db_path())
                                                   : absl::nullopt),
      stats_scope_(scope.createScope(absl::StrCat(stat_prefix, "maxmind."))),
      stat_name_set_(stats_scope_->symbolTable().makeSet("Maxmind")) {
  auto geo_headers_to_add = config.common_provider_config().geo_headers_to_add();
  country_header_ = !geo_headers_to_add.country().empty()
                        ? absl::make_optional(geo_headers_to_add.country())
                        : absl::nullopt;
  city_header_ = !geo_headers_to_add.city().empty() ? absl::make_optional(geo_headers_to_add.city())
                                                    : absl::nullopt;
  region_header_ = !geo_headers_to_add.region().empty()
                       ? absl::make_optional(geo_headers_to_add.region())
                       : absl::nullopt;
  asn_header_ = !geo_headers_to_add.asn().empty() ? absl::make_optional(geo_headers_to_add.asn())
                                                  : absl::nullopt;
  anon_header_ = !geo_headers_to_add.is_anon().empty()
                     ? absl::make_optional(geo_headers_to_add.is_anon())
                     : absl::nullopt;
  anon_vpn_header_ = !geo_headers_to_add.anon_vpn().empty()
                         ? absl::make_optional(geo_headers_to_add.anon_vpn())
                         : absl::nullopt;
  anon_hosting_header_ = !geo_headers_to_add.anon_hosting().empty()
                             ? absl::make_optional(geo_headers_to_add.anon_hosting())
                             : absl::nullopt;
  anon_tor_header_ = !geo_headers_to_add.anon_tor().empty()
                         ? absl::make_optional(geo_headers_to_add.anon_tor())
                         : absl::nullopt;
  anon_proxy_header_ = !geo_headers_to_add.anon_proxy().empty()
                           ? absl::make_optional(geo_headers_to_add.anon_proxy())
                           : absl::nullopt;
  if (!city_db_path_ && !isp_db_path_ && !anon_db_path_) {
    throw EnvoyException("At least one geolocation database path needs to be configured: "
                         "city_db_path, isp_db_path or anon_db_path");
  }
  if (city_db_path_) {
    registerGeoDbStats(CITY_DB_TYPE);
  }
  if (isp_db_path_) {
    registerGeoDbStats(ISP_DB_TYPE);
  }
  if (anon_db_path_) {
    registerGeoDbStats(ANON_DB_TYPE);
  }
};

void GeoipProviderConfig::registerGeoDbStats(const absl::string_view& db_type) {
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".total"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".hit"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".lookup_error"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".db_reload_error"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".db_reload_success"));
}

bool GeoipProviderConfig::isLookupEnabledForHeader(const absl::optional<std::string>& header) {
  return (header && !header.value().empty());
}

void GeoipProviderConfig::incCounter(Stats::StatName name) {
  stats_scope_->counterFromStatName(name).inc();
}

GeoipProvider::GeoipProvider(Event::Dispatcher& dispatcher, Api::Api& api,
                             Singleton::InstanceSharedPtr owner,
                             GeoipProviderConfigSharedPtr config)
    : config_(config), owner_(owner) {
  city_db_ =
      config_->cityDbPath() ? initMaxmindDb(config_->cityDbPath().value(), CITY_DB_TYPE) : nullptr;
  isp_db_ =
      config_->ispDbPath() ? initMaxmindDb(config_->ispDbPath().value(), ISP_DB_TYPE) : nullptr;
  anon_db_ =
      config_->anonDbPath() ? initMaxmindDb(config_->anonDbPath().value(), ANON_DB_TYPE) : nullptr;
  mmdb_reload_dispatcher_ = api.allocateDispatcher("mmdb_reload_routine");
  mmdb_watcher_ = dispatcher.createFilesystemWatcher();
  mmdb_reload_thread_ = api.threadFactory().createThread(
      [this]() -> void {
        ENVOY_LOG_MISC(debug, "Started mmdb_reload_routine");
        if (config_->cityDbPath()) {
          mmdb_watcher_->addWatch(
              config_->cityDbPath().value(), Filesystem::Watcher::Events::Modified,
              [this](uint32_t) { onMaxmindDbUpdate(config_->cityDbPath().value(), CITY_DB_TYPE); });
        }
        if (config_->ispDbPath()) {
          mmdb_watcher_->addWatch(
              config_->ispDbPath().value(), Filesystem::Watcher::Events::Modified,
              [this](uint32_t) { onMaxmindDbUpdate(config_->ispDbPath().value(), ISP_DB_TYPE); });
        }
        if (config_->anonDbPath()) {
          mmdb_watcher_->addWatch(
              config_->anonDbPath().value(), Filesystem::Watcher::Events::Modified,
              [this](uint32_t) { onMaxmindDbUpdate(config_->anonDbPath().value(), ANON_DB_TYPE); });
        }
        mmdb_reload_dispatcher_->run(Event::Dispatcher::RunType::RunUntilExit);
      },
      Thread::Options{std::string("mmdb_reload_routine")});
};

GeoipProvider::~GeoipProvider() {
  ENVOY_LOG(debug, "Shutting down Maxmind geolocation provider");
  if (mmdb_reload_dispatcher_) {
    mmdb_reload_dispatcher_->exit();
  }
  if (mmdb_reload_thread_) {
    mmdb_reload_thread_->join();
    mmdb_reload_thread_.reset();
  }
  if (city_db_) {
    MMDB_close(city_db_.get());
  }
  if (isp_db_) {
    MMDB_close(isp_db_.get());
  }
  if (anon_db_) {
    MMDB_close(anon_db_.get());
  }
}

void GeoipProvider::lookup(Geolocation::LookupRequest&& request,
                           Geolocation::LookupGeoHeadersCallback&& cb) const {
  auto& remote_address = request.remoteAddress();
  auto lookup_result = absl::flat_hash_map<std::string, std::string>{};
  lookupInCityDb(remote_address, lookup_result);
  lookupInAsnDb(remote_address, lookup_result);
  lookupInAnonDb(remote_address, lookup_result);
  cb(std::move(lookup_result));
}

void GeoipProvider::lookupInCityDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  if (config_->isLookupEnabledForHeader(config_->cityHeader()) ||
      config_->isLookupEnabledForHeader(config_->regionHeader()) ||
      config_->isLookupEnabledForHeader(config_->countryHeader())) {
    ASSERT(city_db_, "Maxmind city database is not initialised for performing lookups");
    int mmdb_error;
    const uint32_t n_prev_hits = lookup_result.size();
    MMDB_lookup_result_s mmdb_lookup_result = MMDB_lookup_sockaddr(
        city_db_.get(), reinterpret_cast<const sockaddr*>(remote_address->sockAddr()), &mmdb_error);
    if (!mmdb_error) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        if (config_->isLookupEnabledForHeader(config_->cityHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result, config_->cityHeader().value(),
                                  MMDB_CITY_LOOKUP_ARGS[0], MMDB_CITY_LOOKUP_ARGS[1],
                                  MMDB_CITY_LOOKUP_ARGS[2]);
        }
        if (config_->isLookupEnabledForHeader(config_->regionHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->regionHeader().value(), MMDB_REGION_LOOKUP_ARGS[0],
                                  MMDB_REGION_LOOKUP_ARGS[1], MMDB_REGION_LOOKUP_ARGS[2]);
        }
        if (config_->isLookupEnabledForHeader(config_->countryHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->countryHeader().value(), MMDB_COUNTRY_LOOKUP_ARGS[0],
                                  MMDB_COUNTRY_LOOKUP_ARGS[1]);
        }
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(CITY_DB_TYPE);
        }
        MMDB_free_entry_data_list(entry_data_list);
      }

    } else {
      config_->incLookupError(CITY_DB_TYPE);
    }
    config_->incTotal(CITY_DB_TYPE);
  }
}

void GeoipProvider::lookupInAsnDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  if (config_->isLookupEnabledForHeader(config_->asnHeader())) {
    RELEASE_ASSERT(isp_db_, "Maxmind asn database is not initialized for performing lookups");
    int mmdb_error;
    const uint32_t n_prev_hits = lookup_result.size();
    MMDB_lookup_result_s mmdb_lookup_result = MMDB_lookup_sockaddr(
        isp_db_.get(), reinterpret_cast<const sockaddr*>(remote_address->sockAddr()), &mmdb_error);
    if (!mmdb_error) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS && entry_data_list) {
        populateGeoLookupResult(mmdb_lookup_result, lookup_result, config_->asnHeader().value(),
                                MMDB_ASN_LOOKUP_ARGS[0]);
        MMDB_free_entry_data_list(entry_data_list);
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(ISP_DB_TYPE);
        }
      } else {
        config_->incLookupError(ISP_DB_TYPE);
      }
    }
    config_->incTotal(ISP_DB_TYPE);
  }
}

void GeoipProvider::lookupInAnonDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  if (config_->isLookupEnabledForHeader(config_->anonHeader()) || config_->anonVpnHeader()) {
    ASSERT(anon_db_, "Maxmind city database is not initialised for performing lookups");
    int mmdb_error;
    const uint32_t n_prev_hits = lookup_result.size();
    MMDB_lookup_result_s mmdb_lookup_result = MMDB_lookup_sockaddr(
        anon_db_.get(), reinterpret_cast<const sockaddr*>(remote_address->sockAddr()), &mmdb_error);
    if (!mmdb_error) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        if (config_->isLookupEnabledForHeader(config_->anonHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result, config_->anonHeader().value(),
                                  MMDB_ANON_LOOKUP_ARGS[0]);
        }
        if (config_->isLookupEnabledForHeader(config_->anonVpnHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->anonVpnHeader().value(), MMDB_ANON_LOOKUP_ARGS[1]);
        }
        if (config_->isLookupEnabledForHeader(config_->anonHostingHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->anonHostingHeader().value(), MMDB_ANON_LOOKUP_ARGS[2]);
        }
        if (config_->isLookupEnabledForHeader(config_->anonTorHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->anonTorHeader().value(), MMDB_ANON_LOOKUP_ARGS[3]);
        }
        if (config_->isLookupEnabledForHeader(config_->anonProxyHeader())) {
          populateGeoLookupResult(mmdb_lookup_result, lookup_result,
                                  config_->anonProxyHeader().value(), MMDB_ANON_LOOKUP_ARGS[4]);
        }
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(ANON_DB_TYPE);
        }
        MMDB_free_entry_data_list(entry_data_list);
      } else {
        config_->incLookupError(ANON_DB_TYPE);
      }
    }
    config_->incTotal(ANON_DB_TYPE);
  }
}

MaxmindDbSharedPtr GeoipProvider::initMaxmindDb(const std::string& db_path,
                                                const absl::string_view& db_type, bool reload) {
  MMDB_s maxmind_db;
  int result_code = MMDB_open(db_path.c_str(), MMDB_MODE_MMAP, &maxmind_db);
  if (reload && MMDB_SUCCESS != result_code) {
    ENVOY_LOG(error, "Failed to reload Maxmind database {} from file {}. Error {}", db_type,
              db_path, std::string(MMDB_strerror(result_code)));
    return nullptr;
  } else {
    RELEASE_ASSERT(MMDB_SUCCESS == result_code,
                   fmt::format("Unable to open Maxmind database file {}. Error {}", db_path,
                               std::string(MMDB_strerror(result_code))));
    ENVOY_LOG(info, "Succeeded to reload Maxmind database {} from file {}.", db_type, db_path);
    return std::make_shared<MMDB_s>(maxmind_db);
  }
}

void GeoipProvider::mmdbReload(MaxmindDbSharedPtr& old_db, const MaxmindDbSharedPtr reloaded_db,
                               absl::Mutex& mu, const absl::string_view& db_type) {
  if (reloaded_db) {
    absl::MutexLock lock(&mu);
    old_db = reloaded_db;
    config_->incDbReloadSuccess(db_type);
  } else {
    config_->incDbReloadError(db_type);
  }
}

void GeoipProvider::onMaxmindDbUpdate(const std::string& db_path,
                                      const absl::string_view& db_type) {
  MaxmindDbSharedPtr reloaded_db = initMaxmindDb(db_path, db_type, true /* reload */);
  if (db_type == CITY_DB_TYPE) {
    mmdbReload(city_db_, reloaded_db, city_db_mutex_, CITY_DB_TYPE);
  } else if (db_type == ISP_DB_TYPE) {
    mmdbReload(isp_db_, reloaded_db, isp_db_mutex_, ISP_DB_TYPE);
  } else if (db_type == ANON_DB_TYPE) {
    mmdbReload(anon_db_, reloaded_db, anon_db_mutex_, ANON_DB_TYPE);
  } else {
    ENVOY_LOG(error, "Unsupported maxmind db type {}", db_type);
  }
}

template <class... Params>
void GeoipProvider::populateGeoLookupResult(
    MMDB_lookup_result_s& mmdb_lookup_result,
    absl::flat_hash_map<std::string, std::string>& lookup_result, const std::string& result_key,
    Params... lookup_params) const {
  MMDB_entry_data_s entry_data;
  if ((MMDB_get_value(&mmdb_lookup_result.entry, &entry_data, lookup_params..., NULL)) ==
      MMDB_SUCCESS) {
    std::string result_value;
    if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
      result_value = std::string(entry_data.utf8_string, entry_data.data_size);
    } else if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_UINT32 &&
               entry_data.uint32 > 0) {
      result_value = std::to_string(entry_data.uint32);
    } else if (entry_data.has_data && entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
      result_value = entry_data.boolean ? "true" : "false";
    }
    if (!result_value.empty()) {
      lookup_result.insert(std::make_pair(result_key, result_value));
    }
  }
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
