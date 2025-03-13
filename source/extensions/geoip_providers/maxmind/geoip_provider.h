#pragma once

#include "envoy/common/platform.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/geoip/geoip_provider_driver.h"

#include "source/common/common/logger.h"
#include "source/common/common/thread_synchronizer.h"

#include "maxminddb.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

class GeoipProviderConfig {
public:
  GeoipProviderConfig(const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& config,
                      const std::string& stat_prefix, Stats::Scope& scope);

  const absl::optional<std::string>& cityDbPath() const { return city_db_path_; }
  const absl::optional<std::string>& ispDbPath() const { return isp_db_path_; }
  const absl::optional<std::string>& anonDbPath() const { return anon_db_path_; }

  bool isLookupEnabledForHeader(const absl::optional<std::string>& header);

  const absl::optional<std::string>& countryHeader() const { return country_header_; }
  const absl::optional<std::string>& cityHeader() const { return city_header_; }
  const absl::optional<std::string>& regionHeader() const { return region_header_; }
  const absl::optional<std::string>& asnHeader() const { return asn_header_; }

  const absl::optional<std::string>& anonHeader() const { return anon_header_; }
  const absl::optional<std::string>& anonVpnHeader() const { return anon_vpn_header_; }
  const absl::optional<std::string>& anonHostingHeader() const { return anon_hosting_header_; }
  const absl::optional<std::string>& anonTorHeader() const { return anon_tor_header_; }
  const absl::optional<std::string>& anonProxyHeader() const { return anon_proxy_header_; }

  void incLookupError(absl::string_view maxmind_db_type) {
    incCounter(
        stat_name_set_->getBuiltin(absl::StrCat(maxmind_db_type, ".lookup_error"), unknown_hit_));
  }

  void incTotal(absl::string_view maxmind_db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(maxmind_db_type, ".total"), unknown_hit_));
  }

  void incHit(absl::string_view maxmind_db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(maxmind_db_type, ".hit"), unknown_hit_));
  }

  void incDbReloadSuccess(absl::string_view maxmind_db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(maxmind_db_type, ".db_reload_success"),
                                          unknown_hit_));
  }

  void incDbReloadError(absl::string_view maxmind_db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(maxmind_db_type, ".db_reload_error"),
                                          unknown_hit_));
  }

  void registerGeoDbStats(const absl::string_view& db_type);

  Stats::Scope& getStatsScopeForTest() const { return *stats_scope_; }

private:
  absl::optional<std::string> city_db_path_;
  absl::optional<std::string> isp_db_path_;
  absl::optional<std::string> anon_db_path_;

  absl::optional<std::string> country_header_;
  absl::optional<std::string> city_header_;
  absl::optional<std::string> region_header_;
  absl::optional<std::string> asn_header_;

  absl::optional<std::string> anon_header_;
  absl::optional<std::string> anon_vpn_header_;
  absl::optional<std::string> anon_hosting_header_;
  absl::optional<std::string> anon_tor_header_;
  absl::optional<std::string> anon_proxy_header_;

  Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName unknown_hit_;
  void incCounter(Stats::StatName name);
};

using GeoipProviderConfigSharedPtr = std::shared_ptr<GeoipProviderConfig>;

// Wrapper class for MMDB_s type that ensures a proper cleanup of the MMDB_s
// instance resources prior to its destruction.
class MaxmindDb {
public:
  MaxmindDb(MMDB_s&& db) : db_(db) {}
  ~MaxmindDb() { MMDB_close(&db_); }
  const MMDB_s* mmdb() const { return &db_; }

private:
  MMDB_s db_;
};

using MaxmindDbSharedPtr = std::shared_ptr<MaxmindDb>;
class GeoipProvider : public Envoy::Geolocation::Driver,
                      public Logger::Loggable<Logger::Id::geolocation> {

public:
  GeoipProvider(Event::Dispatcher& dispatcher, Api::Api& api, Singleton::InstanceSharedPtr owner,
                GeoipProviderConfigSharedPtr config);

  ~GeoipProvider() override;

  // Envoy::Geolocation::Driver
  void lookup(Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&&) const override;

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;
  GeoipProviderConfigSharedPtr config_;
  mutable absl::Mutex mmdb_mutex_;
  MaxmindDbSharedPtr city_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr isp_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr anon_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  Thread::ThreadPtr mmdb_reload_thread_;
  Event::DispatcherPtr mmdb_reload_dispatcher_;
  Filesystem::WatcherPtr mmdb_watcher_;
  MaxmindDbSharedPtr initMaxmindDb(const std::string& db_path, const absl::string_view& db_type,
                                   bool reload = false);
  void lookupInCityDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAsnDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                     absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAnonDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  absl::Status onMaxmindDbUpdate(const std::string& db_path, const absl::string_view& db_type);
  absl::Status mmdbReload(const MaxmindDbSharedPtr reloaded_db, const absl::string_view& db_type)
      ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  template <typename... Params>
  void populateGeoLookupResult(MMDB_lookup_result_s& mmdb_lookup_result,
                               absl::flat_hash_map<std::string, std::string>& lookup_result,
                               const std::string& result_key, Params... lookup_params) const;
  MaxmindDbSharedPtr getCityDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getIspDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getAnonDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateCityDb(MaxmindDbSharedPtr city_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateIspDb(MaxmindDbSharedPtr isp_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateAnonDb(MaxmindDbSharedPtr anon_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  // A shared_ptr to keep the provider singleton alive as long as any of its providers are in use.
  const Singleton::InstanceSharedPtr owner_;
  // Used for testing only.
  mutable Thread::ThreadSynchronizer synchronizer_;
};

using GeoipProviderSharedPtr = std::shared_ptr<GeoipProvider>;

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
