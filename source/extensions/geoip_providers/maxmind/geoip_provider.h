#pragma once

#include <array>

#include "envoy/common/platform.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/geoip/geoip_provider_driver.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/enum_to_int.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread_synchronizer.h"

#include "absl/status/statusor.h"
#include "maxminddb.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

enum class GeoField {
  Country,
  City,
  Region,
  Asn,
  AsnOrg,
  Anon,
  AnonVpn,
  AnonHosting,
  AnonTor,
  AnonProxy,
  Isp,
  ApplePrivateRelay,
  Count,
};

// The Maxmind database types a provider can be configured with.
enum class GeoDbType {
  City,
  Isp,
  Anon,
  Asn,
  Country,
  Count,
};

// Name of a database type as it appears in stat names and log messages, for example "city_db".
absl::string_view dbTypeName(GeoDbType db_type);

class GeoipProviderConfig {
public:
  GeoipProviderConfig(const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& config,
                      const std::string& stat_prefix, Stats::Scope& scope);

  // Returns the configured output key for the requested GeoIP field.
  const std::optional<std::string>& fieldKey(GeoField field) const {
    return field_keys_[enumToInt(field)];
  }

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

  void registerGeoDbStats(const absl::string_view& db_type);

  Stats::Scope& getStatsScopeForTest() const { return *stats_scope_; }

private:
  // Configured output key for each GeoField.
  std::array<std::optional<std::string>, enumToInt(GeoField::Count)> field_keys_;

  Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName unknown_hit_;
  void setFieldKey(GeoField field, const std::string& value);
  void incCounter(Stats::StatName name);
};

using GeoipProviderConfigSharedPtr = std::shared_ptr<GeoipProviderConfig>;

// Wrapper class for MMDB_s type that ensures a proper cleanup of the MMDB_s
// instance resources prior to its destruction.
class MaxmindDb {
public:
  explicit MaxmindDb(MMDB_s&& db) : db_(db) {}
  ~MaxmindDb() { MMDB_close(&db_); }
  const MMDB_s* mmdb() const { return &db_; }

private:
  MMDB_s db_;
};

using MaxmindDbSharedPtr = std::shared_ptr<MaxmindDb>;

/**
 * All stats describing a single Maxmind database file. @see stats_macros.h
 */
#define ALL_MAXMIND_DB_FILE_STATS(COUNTER, GAUGE)                                                  \
  COUNTER(db_reload_error)                                                                         \
  COUNTER(db_reload_success)                                                                       \
  GAUGE(db_build_epoch, Accumulate)

struct DbFileStats {
  ALL_MAXMIND_DB_FILE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

// Owns one Maxmind database file: the database parsed from it, the watcher that reloads it when
// the file changes, and the stats describing it.
class DbFileProvider : public Logger::Loggable<Logger::Id::geolocation> {
public:
  DbFileProvider(Event::Dispatcher& dispatcher, Stats::ScopeSharedPtr scope, GeoDbType db_type,
                 const std::string& db_path);

  // The database type this file was configured as.
  GeoDbType dbType() const { return db_type_; }

  // The currently loaded database. Null only if the file failed to load, which cannot happen
  // before the constructor returns.
  MaxmindDbSharedPtr db() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;

  absl::StatusOr<MaxmindDbSharedPtr> initMaxmindDb(const std::string& db_path);
  absl::Status onMaxmindDbUpdate(const std::string& db_path);
  void updateDb(MaxmindDbSharedPtr db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);

  void incDbReloadSuccess() { stats_.db_reload_success_.inc(); }
  void incDbReloadError() { stats_.db_reload_error_.inc(); }
  void setDbBuildEpoch(const uint64_t value) { stats_.db_build_epoch_.set(value); }

  // Declared first so that it outlives the stats that reference it.
  const Stats::ScopeSharedPtr stats_scope_;
  const GeoDbType db_type_;
  DbFileStats stats_;
  mutable absl::Mutex mmdb_mutex_;
  MaxmindDbSharedPtr db_ ABSL_GUARDED_BY(mmdb_mutex_);
  Filesystem::WatcherPtr mmdb_watcher_;
};

using DbFileProviderSharedPtr = std::shared_ptr<DbFileProvider>;

// The database file a provider reads each database type from. A null member means that database
// is not configured.
struct DbFileProviders {
  DbFileProviderSharedPtr city_db_;
  DbFileProviderSharedPtr isp_db_;
  DbFileProviderSharedPtr anon_db_;
  DbFileProviderSharedPtr asn_db_;
  DbFileProviderSharedPtr country_db_;
};

class GeoipProvider : public Envoy::Geolocation::Driver,
                      public Logger::Loggable<Logger::Id::geolocation> {

public:
  GeoipProvider(Singleton::InstanceSharedPtr owner, GeoipProviderConfigSharedPtr config,
                DbFileProviders db_file_providers);

  ~GeoipProvider() override;

  // Envoy::Geolocation::Driver
  void lookup(Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&&) const override;

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;

  MaxmindDbSharedPtr getCityDb() const {
    return db_file_providers_.city_db_ != nullptr ? db_file_providers_.city_db_->db() : nullptr;
  }
  MaxmindDbSharedPtr getIspDb() const {
    return db_file_providers_.isp_db_ != nullptr ? db_file_providers_.isp_db_->db() : nullptr;
  }
  MaxmindDbSharedPtr getAnonDb() const {
    return db_file_providers_.anon_db_ != nullptr ? db_file_providers_.anon_db_->db() : nullptr;
  }
  MaxmindDbSharedPtr getAsnDb() const {
    return db_file_providers_.asn_db_ != nullptr ? db_file_providers_.asn_db_->db() : nullptr;
  }
  MaxmindDbSharedPtr getCountryDb() const {
    return db_file_providers_.country_db_ != nullptr ? db_file_providers_.country_db_->db()
                                                     : nullptr;
  }

  // Whether each database was configured for this provider.
  bool isCityDbSet() const { return db_file_providers_.city_db_ != nullptr; }
  bool isIspDbSet() const { return db_file_providers_.isp_db_ != nullptr; }
  bool isAsnDbSet() const { return db_file_providers_.asn_db_ != nullptr; }
  bool isCountryDbSet() const { return db_file_providers_.country_db_ != nullptr; }

  GeoipProviderConfigSharedPtr config_;
  // The database files this provider looks up in. Each is shared with every other provider
  // configured with the same database type and path.
  DbFileProviders db_file_providers_;
  void lookupInCityDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAsnDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                     absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInAnonDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                      absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInIspDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                     absl::flat_hash_map<std::string, std::string>& lookup_result) const;
  void lookupInCountryDb(const Network::Address::InstanceConstSharedPtr& remote_address,
                         absl::flat_hash_map<std::string, std::string>& lookup_result) const;
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
