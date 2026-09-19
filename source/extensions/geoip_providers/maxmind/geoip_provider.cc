#include "source/extensions/geoip_providers/maxmind/geoip_provider.h"

#include <algorithm>
#include <span>

#include "source/common/common/assert.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/strings/escaping.h"
#include "absl/strings/str_join.h"

namespace Envoy {
namespace Extensions {
namespace GeoipProviders {
namespace Maxmind {

namespace {
constexpr const char* MMDB_CITY_LOOKUP_PATH[] = {"city", "names", "en", nullptr};
constexpr const char* MMDB_REGION_LOOKUP_PATH[] = {"subdivisions", "0", "iso_code", nullptr};
constexpr const char* MMDB_COUNTRY_LOOKUP_PATH[] = {"country", "iso_code", nullptr};
constexpr const char* MMDB_ASN_LOOKUP_PATH[] = {"autonomous_system_number", nullptr};
constexpr const char* MMDB_ASN_ORG_LOOKUP_PATH[] = {"autonomous_system_organization", nullptr};
constexpr const char* MMDB_ISP_LOOKUP_PATH[] = {"isp", nullptr};
constexpr const char* MMDB_ISP_ASN_LOOKUP_PATH[] = {"autonomous_system_number", nullptr};
constexpr const char* MMDB_ISP_ORG_LOOKUP_PATH[] = {"organization", nullptr};
constexpr const char* MMDB_ANON_LOOKUP_PATH[] = {"is_anonymous", nullptr};
constexpr const char* MMDB_ANON_VPN_LOOKUP_PATH[] = {"is_anonymous_vpn", nullptr};
constexpr const char* MMDB_ANON_HOSTING_LOOKUP_PATH[] = {"is_hosting_provider", nullptr};
constexpr const char* MMDB_ANON_TOR_LOOKUP_PATH[] = {"is_tor_exit_node", nullptr};
constexpr const char* MMDB_ANON_PROXY_LOOKUP_PATH[] = {"is_public_proxy", nullptr};

enum class LookupValueTransform { None, ApplePrivateRelay };

struct LookupFieldSpec {
  GeoField field_;
  const char* const* path_;
  LookupValueTransform transform_{LookupValueTransform::None};
};

constexpr LookupFieldSpec CITY_LOOKUP_FIELDS[] = {
    {GeoField::City, MMDB_CITY_LOOKUP_PATH},
    {GeoField::Region, MMDB_REGION_LOOKUP_PATH},
};
constexpr LookupFieldSpec COUNTRY_LOOKUP_FIELDS[] = {
    {GeoField::Country, MMDB_COUNTRY_LOOKUP_PATH},
};
constexpr LookupFieldSpec ASN_LOOKUP_FIELDS[] = {
    {GeoField::Asn, MMDB_ASN_LOOKUP_PATH},
    {GeoField::AsnOrg, MMDB_ASN_ORG_LOOKUP_PATH},
};
constexpr LookupFieldSpec ANON_LOOKUP_FIELDS[] = {
    {GeoField::Anon, MMDB_ANON_LOOKUP_PATH},
    {GeoField::AnonVpn, MMDB_ANON_VPN_LOOKUP_PATH},
    {GeoField::AnonHosting, MMDB_ANON_HOSTING_LOOKUP_PATH},
    {GeoField::AnonTor, MMDB_ANON_TOR_LOOKUP_PATH},
    {GeoField::AnonProxy, MMDB_ANON_PROXY_LOOKUP_PATH},
};
constexpr LookupFieldSpec ISP_LOOKUP_FIELDS[] = {
    {GeoField::Isp, MMDB_ISP_LOOKUP_PATH},
    {GeoField::ApplePrivateRelay, MMDB_ISP_LOOKUP_PATH, LookupValueTransform::ApplePrivateRelay},
};
constexpr LookupFieldSpec ISP_ASN_LOOKUP_FIELDS[] = {
    {GeoField::Asn, MMDB_ISP_ASN_LOOKUP_PATH},
    {GeoField::AsnOrg, MMDB_ISP_ORG_LOOKUP_PATH},
};

static constexpr absl::string_view CITY_DB_TYPE = "city_db";
static constexpr absl::string_view ISP_DB_TYPE = "isp_db";
static constexpr absl::string_view ANON_DB_TYPE = "anon_db";
static constexpr absl::string_view ASN_DB_TYPE = "asn_db";
static constexpr absl::string_view COUNTRY_DB_TYPE = "country_db";

// Indexed by GeoDbType.
static constexpr absl::string_view DB_TYPE_NAMES[] = {CITY_DB_TYPE, ISP_DB_TYPE, ANON_DB_TYPE,
                                                      ASN_DB_TYPE, COUNTRY_DB_TYPE};
static_assert(std::size(DB_TYPE_NAMES) == enumToInt(GeoDbType::Count));

// Separator between the encoded database file paths that make up a DbFilesProvider key.
static constexpr absl::string_view DB_FILE_PATHS_KEY_SEPARATOR = "|";

// Helper to get optional string from config field, returns nullopt if empty.
std::optional<std::string> getOptionalString(const std::string& value) {
  return !value.empty() ? std::make_optional(value) : std::nullopt;
}

bool hasConfiguredField(const GeoipProviderConfig& config,
                        std::span<const LookupFieldSpec> fields) {
  for (const auto& field : fields) {
    if (config.fieldKey(field.field_).has_value()) {
      return true;
    }
  }
  return false;
}

std::optional<std::string> lookupValue(MMDB_lookup_result_s& mmdb_lookup_result,
                                       const char* const* path) {
  MMDB_entry_data_s entry_data;
  if (MMDB_aget_value(&mmdb_lookup_result.entry, &entry_data, path) != MMDB_SUCCESS ||
      !entry_data.has_data) {
    return std::nullopt;
  }

  if (entry_data.type == MMDB_DATA_TYPE_UTF8_STRING) {
    return std::string(entry_data.utf8_string, entry_data.data_size);
  }
  if (entry_data.type == MMDB_DATA_TYPE_UINT32 && entry_data.uint32 > 0) {
    return std::to_string(entry_data.uint32);
  }
  if (entry_data.type == MMDB_DATA_TYPE_BOOLEAN) {
    return entry_data.boolean ? "true" : "false";
  }
  return std::nullopt;
}

void populateGeoLookupResults(const GeoipProviderConfig& config,
                              MMDB_lookup_result_s& mmdb_lookup_result,
                              absl::flat_hash_map<std::string, std::string>& lookup_result,
                              std::span<const LookupFieldSpec> fields) {
  for (const auto& field : fields) {
    const auto& result_key = config.fieldKey(field.field_);
    if (!result_key.has_value()) {
      continue;
    }

    auto result_value = lookupValue(mmdb_lookup_result, field.path_);
    if (field.transform_ == LookupValueTransform::ApplePrivateRelay) {
      lookup_result[result_key.value()] =
          result_value.has_value() && result_value.value() == "iCloud Private Relay" ? "true"
                                                                                     : "false";
    } else if (result_value.has_value() && !result_value.value().empty()) {
      lookup_result.insert(std::make_pair(result_key.value(), std::move(result_value.value())));
    }
  }
}
} // namespace

absl::string_view dbTypeName(GeoDbType db_type) { return DB_TYPE_NAMES[enumToInt(db_type)]; }

std::string DbFilePaths::key() const {
  const auto encode = [](const std::string& db_path) { return absl::Base64Escape(db_path); };
  return absl::StrJoin({encode(city_db_path_), encode(isp_db_path_), encode(anon_db_path_),
                        encode(asn_db_path_), encode(country_db_path_)},
                       DB_FILE_PATHS_KEY_SEPARATOR);
}

GeoipProviderConfig::GeoipProviderConfig(
    const envoy::extensions::geoip_providers::maxmind::v3::MaxMindConfig& config,
    const std::string& stat_prefix, Stats::Scope& scope)
    : stats_scope_(scope.createScope(absl::StrCat(stat_prefix, "maxmind."))),
      stat_name_set_(stats_scope_->symbolTable().makeSet("Maxmind")) {
  const auto& common_config = config.common_provider_config();

  const auto set_common_field_keys = [this](const auto& keys) {
    setFieldKey(GeoField::Country, keys.country());
    setFieldKey(GeoField::City, keys.city());
    setFieldKey(GeoField::Region, keys.region());
    setFieldKey(GeoField::Asn, keys.asn());
    setFieldKey(GeoField::AsnOrg, keys.asn_org());
    setFieldKey(GeoField::Anon, keys.anon());
    setFieldKey(GeoField::AnonVpn, keys.anon_vpn());
    setFieldKey(GeoField::AnonHosting, keys.anon_hosting());
    setFieldKey(GeoField::AnonTor, keys.anon_tor());
    setFieldKey(GeoField::AnonProxy, keys.anon_proxy());
    setFieldKey(GeoField::Isp, keys.isp());
    setFieldKey(GeoField::ApplePrivateRelay, keys.apple_private_relay());
  };

  if (common_config.has_geo_field_keys()) {
    // Use geo_field_keys (preferred).
    const auto& keys = common_config.geo_field_keys();
    set_common_field_keys(keys);
  } else if (common_config.has_geo_headers_to_add()) {
    // Fall back to deprecated geo_headers_to_add for backward compatibility.
    const auto& headers = common_config.geo_headers_to_add();
    set_common_field_keys(headers);
    // TODO(barroca): When the is_anon field is fully deprecated, remove this fallback.
    if (headers.anon().empty()) {
      setFieldKey(GeoField::Anon, headers.is_anon());
    }
  }

  // Which databases a provider looks up in is a property of the database files it was given, not
  // of this config, so the lookup stats are remembered for every database type.
  registerGeoDbStats(CITY_DB_TYPE);
  registerGeoDbStats(ISP_DB_TYPE);
  registerGeoDbStats(ANON_DB_TYPE);
  registerGeoDbStats(ASN_DB_TYPE);
  registerGeoDbStats(COUNTRY_DB_TYPE);
}

void GeoipProviderConfig::registerGeoDbStats(const absl::string_view& db_type) {
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".total"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".hit"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type, ".lookup_error"));
}

void GeoipProviderConfig::setFieldKey(GeoField field, const std::string& value) {
  field_keys_[enumToInt(field)] = getOptionalString(value);
}

void GeoipProviderConfig::incCounter(Stats::StatName name) {
  stats_scope_->counterFromStatName(name).inc();
}

DbFilesProvider::DbFilesProvider(Singleton::InstanceSharedPtr owner, Event::Dispatcher& dispatcher,
                                 Stats::Scope& scope, const DbFilePaths& db_file_paths)
    // The database files are shared by every provider that references them, so their stats are
    // rooted at the server scope and carry no listener specific stat prefix.
    : stats_scope_(scope.createScope("maxmind.")),
      stat_name_set_(stats_scope_->symbolTable().makeSet("Maxmind")), owner_(std::move(owner)) {
  // An empty path means that database is not configured.
  db_set_[enumToInt(GeoDbType::City)] = !db_file_paths.city_db_path_.empty();
  db_set_[enumToInt(GeoDbType::Isp)] = !db_file_paths.isp_db_path_.empty();
  db_set_[enumToInt(GeoDbType::Anon)] = !db_file_paths.anon_db_path_.empty();
  db_set_[enumToInt(GeoDbType::Asn)] = !db_file_paths.asn_db_path_.empty();
  db_set_[enumToInt(GeoDbType::Country)] = !db_file_paths.country_db_path_.empty();

  if (std::none_of(db_set_.begin(), db_set_.end(), [](bool db_set) { return db_set; })) {
    throw EnvoyException("At least one geolocation database path needs to be configured: "
                         "city_db_path, isp_db_path, asn_db_path, anon_db_path or country_db_path");
  }

  mmdb_watcher_ = dispatcher.createFilesystemWatcher();
  // A database that cannot be opened at startup is a configuration error: the provider would have
  // nothing to serve lookups from. Reject the configuration rather than run without it.
  const auto load_db = [this](const std::string& db_path, GeoDbType db_type) -> MaxmindDbSharedPtr {
    if (!hasDbSet(db_type)) {
      return nullptr;
    }
    registerGeoDbStats(db_type);
    absl::StatusOr<MaxmindDbSharedPtr> db_or_error = initMaxmindDb(db_path, db_type);
    ENVOY_BUG(!absl::IsResourceExhausted(db_or_error.status()),
              "Cannot open Maxmind database due to resource exhaustion");
    THROW_IF_NOT_OK_REF(db_or_error.status());
    THROW_IF_NOT_OK(mmdb_watcher_->addWatch(
        db_path, Filesystem::Watcher::Events::MovedTo,
        [this, path = db_path, db_type](uint32_t) { return onMaxmindDbUpdate(path, db_type); }));
    return std::move(db_or_error.value());
  };

  city_db_ = load_db(db_file_paths.city_db_path_, GeoDbType::City);
  isp_db_ = load_db(db_file_paths.isp_db_path_, GeoDbType::Isp);
  anon_db_ = load_db(db_file_paths.anon_db_path_, GeoDbType::Anon);
  asn_db_ = load_db(db_file_paths.asn_db_path_, GeoDbType::Asn);
  country_db_ = load_db(db_file_paths.country_db_path_, GeoDbType::Country);
}

void DbFilesProvider::registerGeoDbStats(GeoDbType db_type) {
  const absl::string_view db_type_name = dbTypeName(db_type);
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type_name, ".db_reload_error"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type_name, ".db_reload_success"));
  stat_name_set_->rememberBuiltin(absl::StrCat(db_type_name, ".db_build_epoch"));
}

void DbFilesProvider::incCounter(Stats::StatName name) {
  stats_scope_->counterFromStatName(name).inc();
}

void DbFilesProvider::setGauge(Stats::StatName name, const uint64_t value) {
  stats_scope_->gaugeFromStatName(name, Stats::Gauge::ImportMode::Accumulate).set(value);
}

absl::StatusOr<MaxmindDbSharedPtr> DbFilesProvider::initMaxmindDb(const std::string& db_path,
                                                                  GeoDbType db_type) {
  MMDB_s maxmind_db;
  const int result_code = MMDB_open(db_path.c_str(), MMDB_MODE_MMAP, &maxmind_db);
  if (MMDB_SUCCESS != result_code) {
    const std::string error_msg = fmt::format("Unable to open Maxmind database file {}. Error {}",
                                              db_path, MMDB_strerror(result_code));
    return result_code == MMDB_OUT_OF_MEMORY_ERROR ? absl::ResourceExhaustedError(error_msg)
                                                   : absl::InvalidArgumentError(error_msg);
  }

  setDbBuildEpoch(db_type, maxmind_db.metadata.build_epoch);

  ENVOY_LOG(info, "Loaded Maxmind database {} from file {}.", dbTypeName(db_type), db_path);
  return std::make_shared<MaxmindDb>(std::move(maxmind_db));
}

absl::Status DbFilesProvider::mmdbReload(MaxmindDbSharedPtr reloaded_db, GeoDbType db_type) {
  ASSERT(reloaded_db != nullptr);
  switch (db_type) {
  case GeoDbType::City:
    updateCityDb(std::move(reloaded_db));
    break;
  case GeoDbType::Isp:
    updateIspDb(std::move(reloaded_db));
    break;
  case GeoDbType::Anon:
    updateAnonDb(std::move(reloaded_db));
    break;
  case GeoDbType::Asn:
    updateAsnDb(std::move(reloaded_db));
    break;
  case GeoDbType::Country:
    updateCountryDb(std::move(reloaded_db));
    break;
  case GeoDbType::Count:
    IS_ENVOY_BUG("Unsupported maxmind db type");
    return absl::InvalidArgumentError("Unsupported maxmind db type");
  }
  incDbReloadSuccess(db_type);
  return absl::OkStatus();
}

MaxmindDbSharedPtr DbFilesProvider::getCityDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::ReaderMutexLock lock(mmdb_mutex_);
  return city_db_;
}

void DbFilesProvider::updateCityDb(MaxmindDbSharedPtr city_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::MutexLock lock(mmdb_mutex_);
  city_db_ = std::move(city_db);
}

MaxmindDbSharedPtr DbFilesProvider::getIspDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::ReaderMutexLock lock(mmdb_mutex_);
  return isp_db_;
}

void DbFilesProvider::updateIspDb(MaxmindDbSharedPtr isp_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::MutexLock lock(mmdb_mutex_);
  isp_db_ = std::move(isp_db);
}

MaxmindDbSharedPtr DbFilesProvider::getAsnDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::ReaderMutexLock lock(mmdb_mutex_);
  return asn_db_;
}

void DbFilesProvider::updateAsnDb(MaxmindDbSharedPtr asn_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::MutexLock lock(mmdb_mutex_);
  asn_db_ = std::move(asn_db);
}

MaxmindDbSharedPtr DbFilesProvider::getAnonDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::ReaderMutexLock lock(mmdb_mutex_);
  return anon_db_;
}

void DbFilesProvider::updateAnonDb(MaxmindDbSharedPtr anon_db) ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::MutexLock lock(mmdb_mutex_);
  anon_db_ = std::move(anon_db);
}

MaxmindDbSharedPtr DbFilesProvider::getCountryDb() const ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::ReaderMutexLock lock(mmdb_mutex_);
  return country_db_;
}

void DbFilesProvider::updateCountryDb(MaxmindDbSharedPtr country_db)
    ABSL_LOCKS_EXCLUDED(mmdb_mutex_) {
  absl::MutexLock lock(mmdb_mutex_);
  country_db_ = std::move(country_db);
}

absl::Status DbFilesProvider::onMaxmindDbUpdate(const std::string& db_path, GeoDbType db_type) {
  absl::StatusOr<MaxmindDbSharedPtr> reloaded_db = initMaxmindDb(db_path, db_type);
  absl::Status status = absl::OkStatus();
  if (reloaded_db.ok()) {
    status = mmdbReload(std::move(reloaded_db.value()), db_type);
  } else {
    status = reloaded_db.status();
  }

  if (!status.ok()) {
    // A failed reload is not fatal: the previously loaded database stays in place and keeps
    // serving lookups.
    ENVOY_LOG(error, "Failed to reload Maxmind database {}: {}", dbTypeName(db_type),
              status.message());
    incDbReloadError(db_type);
  }
  return status;
}

GeoipProvider::GeoipProvider(Singleton::InstanceSharedPtr owner,
                             GeoipProviderConfigSharedPtr config,
                             DbFilesProviderSharedPtr db_files_provider)
    : config_(std::move(config)), db_files_provider_(std::move(db_files_provider)),
      owner_(std::move(owner)) {}

GeoipProvider::~GeoipProvider() { ENVOY_LOG(debug, "Shutting down Maxmind geolocation provider"); }

void GeoipProvider::lookup(Geolocation::LookupRequest&& request,
                           Geolocation::LookupGeoHeadersCallback&& cb) const {
  auto& remote_address = request.remoteAddress();
  ASSERT(remote_address != nullptr && remote_address->ip() != nullptr,
         "geolocation lookup requires an IP address");
  auto lookup_result = absl::flat_hash_map<std::string, std::string>{};
  lookupInCountryDb(remote_address, lookup_result);
  lookupInCityDb(remote_address, lookup_result);
  lookupInAsnDb(remote_address, lookup_result);
  lookupInAnonDb(remote_address, lookup_result);
  lookupInIspDb(remote_address, lookup_result);
  cb(std::move(lookup_result));
}

void GeoipProvider::lookupInCityDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  // Country lookup falls back to City DB only if Country DB is not configured.
  const bool should_lookup_country_from_city_db =
      !db_files_provider_->hasDbSet(GeoDbType::Country) &&
      hasConfiguredField(*config_, COUNTRY_LOOKUP_FIELDS);
  if (hasConfiguredField(*config_, CITY_LOOKUP_FIELDS) || should_lookup_country_from_city_db) {
    int mmdb_error = MMDB_SUCCESS;
    auto city_db_ptr = db_files_provider_->getCityDb();
    // Used for testing.
    synchronizer_.syncPoint(std::string(CITY_DB_TYPE).append("_lookup_pre_complete"));
    if (!city_db_ptr) {
      IS_ENVOY_BUG("Maxmind city database must be initialised for performing lookups");
      return;
    }
    auto city_db = city_db_ptr.get();
    MMDB_lookup_result_s mmdb_lookup_result =
        MMDB_lookup_sockaddr(city_db->mmdb(), remote_address->sockAddr(), &mmdb_error);
    const size_t n_prev_hits = lookup_result.size();
    if (!mmdb_error && mmdb_lookup_result.found_entry) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result, CITY_LOOKUP_FIELDS);
        // Country lookup from City DB only when Country DB is not configured.
        if (should_lookup_country_from_city_db) {
          populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result,
                                   COUNTRY_LOOKUP_FIELDS);
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
  if (hasConfiguredField(*config_, ASN_LOOKUP_FIELDS)) {
    int mmdb_error = MMDB_SUCCESS;
    auto asn_db_ptr = db_files_provider_->getAsnDb();
    // Used for testing.
    synchronizer_.syncPoint(std::string(ASN_DB_TYPE).append("_lookup_pre_complete"));
    if (!asn_db_ptr) {
      if (db_files_provider_->hasDbSet(GeoDbType::Isp)) {
        // ASN information can be looked up from ISP database as well, so we don't need to
        // throw an error if is not set.
        return;
      }
      IS_ENVOY_BUG("Maxmind asn database must be initialised for performing lookups");
      return;
    }
    MMDB_lookup_result_s mmdb_lookup_result =
        MMDB_lookup_sockaddr(asn_db_ptr->mmdb(), remote_address->sockAddr(), &mmdb_error);
    const size_t n_prev_hits = lookup_result.size();
    if (!mmdb_error && mmdb_lookup_result.found_entry) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result, ASN_LOOKUP_FIELDS);

        MMDB_free_entry_data_list(entry_data_list);
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(ASN_DB_TYPE);
        }
      } else {
        config_->incLookupError(ASN_DB_TYPE);
      }
    }
    config_->incTotal(ASN_DB_TYPE);
  }
}

void GeoipProvider::lookupInAnonDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  if (hasConfiguredField(*config_, ANON_LOOKUP_FIELDS)) {
    int mmdb_error = MMDB_SUCCESS;
    auto anon_db_ptr = db_files_provider_->getAnonDb();
    // Used for testing.
    synchronizer_.syncPoint(std::string(ANON_DB_TYPE).append("_lookup_pre_complete"));
    if (!anon_db_ptr) {
      IS_ENVOY_BUG("Maxmind anon database must be initialised for performing lookups");
      return;
    }
    auto anon_db = anon_db_ptr.get();
    MMDB_lookup_result_s mmdb_lookup_result =
        MMDB_lookup_sockaddr(anon_db->mmdb(), remote_address->sockAddr(), &mmdb_error);
    const size_t n_prev_hits = lookup_result.size();
    if (!mmdb_error && mmdb_lookup_result.found_entry) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result, ANON_LOOKUP_FIELDS);
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

void GeoipProvider::lookupInIspDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  const bool should_lookup_asn_from_isp_db = !db_files_provider_->hasDbSet(GeoDbType::Asn) &&
                                             hasConfiguredField(*config_, ISP_ASN_LOOKUP_FIELDS);
  if (hasConfiguredField(*config_, ISP_LOOKUP_FIELDS) || should_lookup_asn_from_isp_db) {
    int mmdb_error = MMDB_SUCCESS;
    auto isp_db_ptr = db_files_provider_->getIspDb();
    // Used for testing.
    synchronizer_.syncPoint(std::string(ISP_DB_TYPE).append("_lookup_pre_complete"));
    if (!isp_db_ptr) {
      IS_ENVOY_BUG("Maxmind isp database must be initialised for performing lookups");
      return;
    }
    auto isp_db = isp_db_ptr.get();
    MMDB_lookup_result_s mmdb_lookup_result =
        MMDB_lookup_sockaddr(isp_db->mmdb(), remote_address->sockAddr(), &mmdb_error);
    const size_t n_prev_hits = lookup_result.size();
    if (!mmdb_error && mmdb_lookup_result.found_entry) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result, ISP_LOOKUP_FIELDS);
        if (should_lookup_asn_from_isp_db) {
          populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result,
                                   ISP_ASN_LOOKUP_FIELDS);
        }
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(ISP_DB_TYPE);
        }
        MMDB_free_entry_data_list(entry_data_list);
      } else {
        config_->incLookupError(ISP_DB_TYPE);
      }
    }
    config_->incTotal(ISP_DB_TYPE);
  }
}

void GeoipProvider::lookupInCountryDb(
    const Network::Address::InstanceConstSharedPtr& remote_address,
    absl::flat_hash_map<std::string, std::string>& lookup_result) const {
  if (hasConfiguredField(*config_, COUNTRY_LOOKUP_FIELDS)) {
    // Country DB takes precedence if configured, otherwise fall back to City DB.
    if (!db_files_provider_->hasDbSet(GeoDbType::Country)) {
      // Country lookup will be handled by lookupInCityDb.
      return;
    }
    int mmdb_error = MMDB_SUCCESS;
    auto country_db_ptr = db_files_provider_->getCountryDb();
    // Used for testing.
    synchronizer_.syncPoint(std::string(COUNTRY_DB_TYPE).append("_lookup_pre_complete"));
    if (!country_db_ptr) {
      if (db_files_provider_->hasDbSet(GeoDbType::City)) {
        // Country information can be looked up from City database as well, so we don't need to
        // throw an error if it is not set.
        return;
      }
      IS_ENVOY_BUG("Maxmind country database must be initialised for performing lookups");
      return;
    }
    auto country_db = country_db_ptr.get();
    MMDB_lookup_result_s mmdb_lookup_result =
        MMDB_lookup_sockaddr(country_db->mmdb(), remote_address->sockAddr(), &mmdb_error);
    const size_t n_prev_hits = lookup_result.size();
    if (!mmdb_error && mmdb_lookup_result.found_entry) {
      MMDB_entry_data_list_s* entry_data_list;
      int status = MMDB_get_entry_data_list(&mmdb_lookup_result.entry, &entry_data_list);
      if (status == MMDB_SUCCESS) {
        populateGeoLookupResults(*config_, mmdb_lookup_result, lookup_result,
                                 COUNTRY_LOOKUP_FIELDS);
        if (lookup_result.size() > n_prev_hits) {
          config_->incHit(COUNTRY_DB_TYPE);
        }
        MMDB_free_entry_data_list(entry_data_list);
      } else {
        config_->incLookupError(COUNTRY_DB_TYPE);
      }
    }
    config_->incTotal(COUNTRY_DB_TYPE);
  }
}

} // namespace Maxmind
} // namespace GeoipProviders
} // namespace Extensions
} // namespace Envoy
