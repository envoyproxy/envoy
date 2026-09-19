#pragma once

#include <array>

#include "envoy/common/platform.h"
#include "envoy/extensions/geoip_providers/maxmind/v3/maxmind.pb.h"
#include "envoy/geoip/geoip_provider_driver.h"

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

// The set of Maxmind database files a provider reads from. An empty path means that database is
// not configured.
struct DbFilePaths {
  std::string city_db_path_;
  std::string isp_db_path_;
  std::string anon_db_path_;
  std::string asn_db_path_;
  std::string country_db_path_;

  // Key that identifies this set of files. Maxmind databases are large, so every provider
  // configured with the same set of files shares a single DbFilesProvider looked up by this key.
  // Each path is encoded before being joined, so a path that contains the separator cannot be
  // confused with a different set of files.
  std::string key() const;
};

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

// Owns the Maxmind databases loaded from one set of files and keeps them up to date by watching
// the files for changes. Maxmind databases are large, so a single instance is shared by every
// GeoipProvider configured with the same set of files, regardless of the rest of their config.
//
// The database file level stats this emits (db_build_epoch, db_reload_success, db_reload_error)
// are rooted at the server scope rather than at a provider's stat prefix, since the files are not
// owned by any one listener.
class DbFilesProvider : public Logger::Loggable<Logger::Id::geolocation> {
public:
  DbFilesProvider(Singleton::InstanceSharedPtr owner, Event::Dispatcher& dispatcher,
                  Stats::Scope& scope, const DbFilePaths& db_file_paths);

  // Whether the given database was configured, and so is loaded and available for lookups.
  bool hasDbSet(GeoDbType db_type) const { return db_set_[enumToInt(db_type)]; }

  MaxmindDbSharedPtr getCityDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getIspDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getAnonDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getAsnDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  MaxmindDbSharedPtr getCountryDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_);

  Stats::Scope& getStatsScopeForTest() const { return *stats_scope_; }

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;

  absl::StatusOr<MaxmindDbSharedPtr> initMaxmindDb(const std::string& db_path, GeoDbType db_type);
  absl::Status onMaxmindDbUpdate(const std::string& db_path, GeoDbType db_type);
  absl::Status mmdbReload(MaxmindDbSharedPtr reloaded_db, GeoDbType db_type)
      ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateCityDb(MaxmindDbSharedPtr city_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateIspDb(MaxmindDbSharedPtr isp_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateAnonDb(MaxmindDbSharedPtr anon_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateAsnDb(MaxmindDbSharedPtr asn_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);
  void updateCountryDb(MaxmindDbSharedPtr country_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_);

  void registerGeoDbStats(GeoDbType db_type);

  void incDbReloadSuccess(GeoDbType db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(dbTypeName(db_type), ".db_reload_success"),
                                          unknown_hit_));
  }

  void incDbReloadError(GeoDbType db_type) {
    incCounter(stat_name_set_->getBuiltin(absl::StrCat(dbTypeName(db_type), ".db_reload_error"),
                                          unknown_hit_));
  }

  void setDbBuildEpoch(GeoDbType db_type, const uint64_t value) {
    setGauge(stat_name_set_->getBuiltin(absl::StrCat(dbTypeName(db_type), ".db_build_epoch"),
                                        unknown_hit_),
             value);
  }

  void incCounter(Stats::StatName name);
  void setGauge(Stats::StatName name, const uint64_t value);

  // Whether each database was configured, indexed by GeoDbType.
  std::array<bool, enumToInt(GeoDbType::Count)> db_set_{};

  Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNameSetPtr stat_name_set_;
  const Stats::StatName unknown_hit_;
  mutable absl::Mutex mmdb_mutex_;
  MaxmindDbSharedPtr city_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr isp_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr anon_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr asn_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  MaxmindDbSharedPtr country_db_ ABSL_GUARDED_BY(mmdb_mutex_);
  Filesystem::WatcherPtr mmdb_watcher_;
  // A shared_ptr to keep the provider singleton alive as long as any of these databases are in
  // use, since the singleton is what hands them out.
  const Singleton::InstanceSharedPtr owner_;
};

using DbFilesProviderSharedPtr = std::shared_ptr<DbFilesProvider>;

class GeoipProvider : public Envoy::Geolocation::Driver,
                      public Logger::Loggable<Logger::Id::geolocation> {

public:
  GeoipProvider(Singleton::InstanceSharedPtr owner, GeoipProviderConfigSharedPtr config,
                DbFilesProviderSharedPtr db_files_provider);

  ~GeoipProvider() override;

  // Envoy::Geolocation::Driver
  void lookup(Geolocation::LookupRequest&&, Geolocation::LookupGeoHeadersCallback&&) const override;

private:
  // Allow the unit test to have access to private members.
  friend class GeoipProviderPeer;
  GeoipProviderConfigSharedPtr config_;
  // The databases this provider looks up in. Shared with every other provider configured with the
  // same set of database files.
  DbFilesProviderSharedPtr db_files_provider_;
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
