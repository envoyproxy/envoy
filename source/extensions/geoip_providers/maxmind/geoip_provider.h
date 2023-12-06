#pragma once

#include "envoy/common/platform.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/geoip/geoip_provider_driver.h"

#include "source/common/common/logger.h"

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

  void registerGeoDbStats(const std::string& db_type);

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

  Stats::Scope& scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName stats_prefix_;
  const Stats::StatName unknown_hit_;
  void incCounter(Stats::StatName name);
};

using GeoipProviderConfigSharedPtr = std::shared_ptr<GeoipProviderConfig>;

using MaxmindDbPtr = std::unique_ptr<MMDB_s>;
class GeoipProvider : public Envoy::Geolocation::Driver,
                      public Logger::Loggable<Logger::Id::geolocation> {

public:
  GeoipProvider(Singleton::InstanceSharedPtr owner, GeoipProviderConfigSharedPtr config)
      : config_(config), owner_(owner) {
    city_db_ = initMaxMindDb(config_->cityDbPath());
    isp_db_ = initMaxMindDb(config_->ispDbPath());
    anon_db_ = initMaxMindDb(config_->anonDbPath());
  };

  ~GeoipProvider();

  // Envoy::Geolocation::Driver
  void lookup(Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&&) const override;

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;
  GeoipProviderConfigSharedPtr config_;
  MaxmindDbPtr city_db_;
  MaxmindDbPtr isp_db_;
  MaxmindDbPtr anon_db_;
  MaxmindDbPtr initMaxMindDb(const absl::optional<std::string>& db_path);
  void lookupInCityDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAsnDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                     absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAnonDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  template <typename... Params>
  void populateGeoLookupResult(MMDB_lookup_result_s& mmdb_lookup_result,
                               absl::flat_hash_map<std::string, std::string>& lookup_result,
                               const std::string& result_key, Params... lookup_params) const;
  // A shared_ptr to keep the provider singleton alive as long as any of its providers are in use.
  const Singleton::InstanceSharedPtr owner_;
};

using GeoipProviderSharedPtr = std::shared_ptr<GeoipProvider>;

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
