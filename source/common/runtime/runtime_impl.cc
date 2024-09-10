#include "source/common/runtime/runtime_impl.h"

#include <cstdint>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/event/dispatcher.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/type/v3/percent.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/config/api_version.h"
#include "source/common/filesystem/directory.h"
#include "source/common/grpc/common.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"
#include "re2/re2.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche_platform_impl/quiche_flags_impl.h"
#include "quiche/common/platform/api/quiche_flags.h"
#endif

namespace Envoy {
namespace Runtime {

namespace {

void countDeprecatedFeatureUseInternal(const RuntimeStats& stats) {
  stats.deprecated_feature_use_.inc();
  // Similar to the above, but a gauge that isn't imported during a hot restart.
  stats.deprecated_feature_seen_since_process_start_.inc();
}

void refreshReloadableFlags(const Snapshot::EntryMap& flag_map) {
  for (const auto& it : flag_map) {
    if (it.second.bool_value_.has_value() && isRuntimeFeature(it.first)) {
      maybeSetRuntimeGuard(it.first, it.second.bool_value_.value());
    }
  }
#ifdef ENVOY_ENABLE_QUIC
  absl::flat_hash_map<std::string, bool> quiche_flags_override;
  for (const auto& it : flag_map) {
    if (absl::StartsWith(it.first, quiche::EnvoyQuicheReloadableFlagPrefix) &&
        it.second.bool_value_.has_value()) {
      quiche_flags_override[it.first.substr(quiche::EnvoyFeaturePrefix.length())] =
          it.second.bool_value_.value();
    }
  }

  quiche::FlagRegistry::getInstance().updateReloadableFlags(quiche_flags_override);

  // Because this is a QUICHE protocol flag, this behavior can't be flipped with the above
  // code, so it needs its own runtime flag and code to set it.
  SetQuicheFlag(quic_always_support_server_preferred_address,
                Runtime::runtimeFeatureEnabled(
                    "envoy.reloadable_features.quic_send_server_preferred_address_to_all_clients"));

#endif
  // Make sure ints are parsed after the flag allowing deprecated ints is parsed.
  for (const auto& it : flag_map) {
    if (it.second.uint_value_.has_value()) {
      maybeSetDeprecatedInts(it.first, it.second.uint_value_.value());
    }
  }
  markRuntimeInitialized();
}

} // namespace

bool SnapshotImpl::deprecatedFeatureEnabled(absl::string_view key, bool default_value) const {
  // A deprecated feature is enabled if at least one of the following conditions holds:
  // 1. A boolean runtime entry <key> doesn't exist, and default_value is true.
  // 2. A boolean runtime entry <key> exists, with a value of "true".
  // 3. A boolean runtime entry "envoy.features.enable_all_deprecated_features" with a value of
  //    "true" exists, and there isn't a boolean runtime entry <key> with a value of "false".

  if (!getBoolean(key,
                  getBoolean("envoy.features.enable_all_deprecated_features", default_value))) {
    return false;
  }

  // The feature is allowed. It is assumed this check is called when the feature
  // is about to be used, so increment the feature use stat.
  countDeprecatedFeatureUseInternal(stats_);

#ifdef ENVOY_DISABLE_DEPRECATED_FEATURES
  return false;
#endif

  return true;
}

bool SnapshotImpl::runtimeFeatureEnabled(absl::string_view key) const {
  // If the value is not explicitly set as a runtime boolean, the default value is based on
  // the underlying value.
  return getBoolean(key, Runtime::runtimeFeatureEnabled(key));
}

bool SnapshotImpl::featureEnabled(absl::string_view key, uint64_t default_value,
                                  uint64_t random_value, uint64_t num_buckets) const {
  return random_value % num_buckets < std::min(getInteger(key, default_value), num_buckets);
}

bool SnapshotImpl::featureEnabled(absl::string_view key, uint64_t default_value) const {
  // Avoid PRNG if we know we don't need it.
  uint64_t cutoff = std::min(getInteger(key, default_value), static_cast<uint64_t>(100));
  if (cutoff == 0) {
    return false;
  } else if (cutoff == 100) {
    return true;
  } else {
    return generator_.random() % 100 < cutoff;
  }
}

bool SnapshotImpl::featureEnabled(absl::string_view key, uint64_t default_value,
                                  uint64_t random_value) const {
  return featureEnabled(key, default_value, random_value, 100);
}

Snapshot::ConstStringOptRef SnapshotImpl::get(absl::string_view key) const {
  ASSERT(!isRuntimeFeature(key)); // Make sure runtime guarding is only used for getBoolean
  auto entry = key.empty() ? values_.end() : values_.find(key);
  if (entry == values_.end()) {
    return absl::nullopt;
  } else {
    return entry->second.raw_string_value_;
  }
}

bool SnapshotImpl::featureEnabled(absl::string_view key,
                                  const envoy::type::v3::FractionalPercent& default_value) const {
  return featureEnabled(key, default_value, generator_.random());
}

bool SnapshotImpl::featureEnabled(absl::string_view key,
                                  const envoy::type::v3::FractionalPercent& default_value,
                                  uint64_t random_value) const {
  const auto& entry = key.empty() ? values_.end() : values_.find(key);
  envoy::type::v3::FractionalPercent percent;
  if (entry != values_.end() && entry->second.fractional_percent_value_.has_value()) {
    percent = entry->second.fractional_percent_value_.value();
  } else if (entry != values_.end() && entry->second.uint_value_.has_value()) {
    // Check for > 100 because the runtime value is assumed to be specified as
    // an integer, and it also ensures that truncating the uint64_t runtime
    // value into a uint32_t percent numerator later is safe
    if (entry->second.uint_value_.value() > 100) {
      return true;
    }

    // The runtime value was specified as an integer rather than a fractional
    // percent proto. To preserve legacy semantics, we treat it as a percentage
    // (i.e. denominator of 100).
    percent.set_numerator(entry->second.uint_value_.value());
    percent.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  } else {
    percent = default_value;
  }

  // When numerator > denominator condition is always evaluates to TRUE
  // It becomes hard to debug why configuration does not work in case of wrong numerator.
  // Log debug message that numerator is invalid.
  uint64_t denominator_value =
      ProtobufPercentHelper::fractionalPercentDenominatorToInt(percent.denominator());
  if (percent.numerator() > denominator_value) {
    ENVOY_LOG(debug,
              "WARNING runtime key '{}': numerator ({}) > denominator ({}), condition always "
              "evaluates to true",
              key, percent.numerator(), denominator_value);
  }

  return ProtobufPercentHelper::evaluateFractionalPercent(percent, random_value);
}

uint64_t SnapshotImpl::getInteger(absl::string_view key, uint64_t default_value) const {
  ASSERT(!isRuntimeFeature(key));
  const auto& entry = key.empty() ? values_.end() : values_.find(key);
  if (entry == values_.end() || !entry->second.uint_value_) {
    return default_value;
  } else {
    return entry->second.uint_value_.value();
  }
}

double SnapshotImpl::getDouble(absl::string_view key, double default_value) const {
  ASSERT(!isRuntimeFeature(key)); // Make sure runtime guarding is only used for getBoolean
  const auto& entry = key.empty() ? values_.end() : values_.find(key);
  if (entry == values_.end() || !entry->second.double_value_) {
    return default_value;
  } else {
    return entry->second.double_value_.value();
  }
}

bool SnapshotImpl::getBoolean(absl::string_view key, bool default_value) const {
  const auto& entry = key.empty() ? values_.end() : values_.find(key);
  if (entry == values_.end() || !entry->second.bool_value_.has_value()) {
    return default_value;
  } else {
    return entry->second.bool_value_.value();
  }
}

const std::vector<Snapshot::OverrideLayerConstPtr>& SnapshotImpl::getLayers() const {
  return layers_;
}

const Snapshot::EntryMap& SnapshotImpl::values() const { return values_; }

SnapshotImpl::SnapshotImpl(Random::RandomGenerator& generator, RuntimeStats& stats,
                           std::vector<OverrideLayerConstPtr>&& layers)
    : layers_{std::move(layers)}, generator_{generator}, stats_{stats} {
  for (const auto& layer : layers_) {
    for (const auto& kv : layer->values()) {
      values_.erase(kv.first);
      values_.emplace(kv.first, kv.second);
    }
  }
  stats.num_keys_.set(values_.size());
}

void parseFractionValue(SnapshotImpl::Entry& entry, const ProtobufWkt::Struct& value) {
  envoy::type::v3::FractionalPercent percent;
  static_assert(envoy::type::v3::FractionalPercent::MILLION ==
                envoy::type::v3::FractionalPercent::DenominatorType_MAX);
  percent.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
  for (const auto& f : value.fields()) {
    if (f.first == "numerator") {
      if (f.second.has_number_value()) {
        percent.set_numerator(f.second.number_value());
      }
    } else if (f.first == "denominator" && f.second.has_string_value()) {
      if (f.second.string_value() == "HUNDRED") {
        percent.set_denominator(envoy::type::v3::FractionalPercent::HUNDRED);
      } else if (f.second.string_value() == "TEN_THOUSAND") {
        percent.set_denominator(envoy::type::v3::FractionalPercent::TEN_THOUSAND);
      } else if (f.second.string_value() == "MILLION") {
        percent.set_denominator(envoy::type::v3::FractionalPercent::MILLION);
      } else {
        return;
      }
    } else {
      return;
    }
  }

  entry.fractional_percent_value_ = percent;
}

void setNumberValue(Envoy::Runtime::Snapshot::Entry& entry, double value) {
  entry.double_value_ = value;
  if (value < std::numeric_limits<int>::max() && value == static_cast<int>(value)) {
    entry.bool_value_ = value != 0;
  }
  if (entry.double_value_ >= 0 && entry.double_value_ <= std::numeric_limits<uint64_t>::max()) {
    // Valid uint values will always be parseable as doubles, so we assign the value to both the
    // uint and double fields. In cases where the value is something like "3.1", we will floor the
    // number by casting it to a uint and assigning the uint value.
    entry.uint_value_ = entry.double_value_;
  }
}

// Handle corner cases in parsing: negatives and decimals aren't always parsed as doubles.
bool parseEntryDoubleValue(Envoy::Runtime::Snapshot::Entry& entry) {
  double converted_double;
  if (absl::SimpleAtod(entry.raw_string_value_, &converted_double)) {
    setNumberValue(entry, converted_double);
    return true;
  }
  return false;
}

// Handle an awful corner case where we explicitly shove a yaml percent in a proto string
// value. Basically due to prior parsing logic we have to handle any combination
// of numerator: #### [denominator Y] with quotes braces etc that could possibly be valid json.
// E.g. "final_value": "{\"numerator\": 10000, \"denominator\": \"TEN_THOUSAND\"}",
bool parseEntryFractionalPercentValue(Envoy::Runtime::Snapshot::Entry& entry) {
  if (!absl::StrContains(entry.raw_string_value_, "numerator")) {
    return false;
  }

  const re2::RE2 numerator_re(".*numerator[^\\d]+(\\d+)[^\\d]*");

  std::string match_string;
  if (!re2::RE2::FullMatch(entry.raw_string_value_.c_str(), numerator_re, &match_string)) {
    return false;
  }

  uint32_t numerator;
  if (!absl::SimpleAtoi(match_string, &numerator)) {
    return false;
  }
  envoy::type::v3::FractionalPercent converted_fractional_percent;
  converted_fractional_percent.set_numerator(numerator);
  entry.fractional_percent_value_ = converted_fractional_percent;

  if (!absl::StrContains(entry.raw_string_value_, "denominator")) {
    return true;
  }
  if (absl::StrContains(entry.raw_string_value_, "TEN_THOUSAND")) {
    entry.fractional_percent_value_->set_denominator(
        envoy::type::v3::FractionalPercent::TEN_THOUSAND);
  }
  if (absl::StrContains(entry.raw_string_value_, "MILLION")) {
    entry.fractional_percent_value_->set_denominator(envoy::type::v3::FractionalPercent::MILLION);
  }
  return true;
}

// Handle corner cases in non-yaml parsing: mixed case strings aren't parsed as booleans.
bool parseEntryBooleanValue(Envoy::Runtime::Snapshot::Entry& entry) {
  absl::string_view stripped = entry.raw_string_value_;
  stripped = absl::StripAsciiWhitespace(stripped);

  if (absl::EqualsIgnoreCase(stripped, "true")) {
    entry.bool_value_ = true;
    return true;
  } else if (absl::EqualsIgnoreCase(stripped, "false")) {
    entry.bool_value_ = false;
    return true;
  }
  return false;
}

void SnapshotImpl::addEntry(Snapshot::EntryMap& values, const std::string& key,
                            const ProtobufWkt::Value& value, absl::string_view raw_string) {
  const char* error_message = nullptr;
  values.emplace(key, SnapshotImpl::createEntry(value, raw_string, error_message));
  if (error_message != nullptr) {
    IS_ENVOY_BUG(
        absl::StrCat(error_message, "\n[ key:", key, ", value: ", value.DebugString(), "]"));
  }
}

static const char* kBoolError =
    "Runtime YAML appears to be setting booleans as strings. Support for this is planned "
    "to removed in an upcoming release. If you can not fix your YAML and need this to continue "
    "working "
    "please ping on https://github.com/envoyproxy/envoy/issues/27434";
static const char* kFractionError =
    "Runtime YAML appears to be setting fractions as strings. Support for this is planned "
    "to removed in an upcoming release. If you can not fix your YAML and need this to continue "
    "working "
    "please ping on https://github.com/envoyproxy/envoy/issues/27434";

SnapshotImpl::Entry SnapshotImpl::createEntry(const ProtobufWkt::Value& value,
                                              absl::string_view raw_string,
                                              const char*& error_message) {
  Entry entry;
  entry.raw_string_value_ = value.string_value();
  if (!raw_string.empty()) {
    entry.raw_string_value_ = raw_string;
  }
  switch (value.kind_case()) {
  case ProtobufWkt::Value::kNumberValue:
    setNumberValue(entry, value.number_value());
    if (entry.raw_string_value_.empty()) {
      entry.raw_string_value_ = absl::StrCat(value.number_value());
    }
    break;
  case ProtobufWkt::Value::kBoolValue:
    entry.bool_value_ = value.bool_value();
    if (entry.raw_string_value_.empty()) {
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.boolean_to_string_fix")) {
        // Convert boolean to "true"/"false"
        entry.raw_string_value_ = value.bool_value() ? "true" : "false";
      } else {
        // Use absl::StrCat for backward compatibility, which converts to "1"/"0"
        entry.raw_string_value_ = absl::StrCat(value.bool_value());
      }
    }
    break;
  case ProtobufWkt::Value::kStructValue:
    if (entry.raw_string_value_.empty()) {
      entry.raw_string_value_ = value.struct_value().DebugString();
    }
    parseFractionValue(entry, value.struct_value());
    break;
  case ProtobufWkt::Value::kStringValue:
    parseEntryDoubleValue(entry);
    if (!Runtime::runtimeFeatureEnabled("envoy.reloadable_features.reject_invalid_yaml")) {
      if (parseEntryBooleanValue(entry)) {
        error_message = kBoolError;
      }
      if (parseEntryFractionalPercentValue(entry)) {
        error_message = kFractionError;
      }
    }
  default:
    break;
  }

  return entry;
}

absl::Status AdminLayer::mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
#ifdef ENVOY_ENABLE_YAML
  for (const auto& kv : values) {
    values_.erase(kv.first);
    if (!kv.second.empty()) {
      SnapshotImpl::addEntry(values_, kv.first, ValueUtil::loadFromYaml(kv.second), kv.second);
    }
  }
  stats_.admin_overrides_active_.set(values_.empty() ? 0 : 1);
  return absl::OkStatus();
#else
  UNREFERENCED_PARAMETER(values);
  return absl::InvalidArgumentError("Runtime admin reload requires YAML support");
#endif
}

DiskLayer::DiskLayer(absl::string_view name, const std::string& path, Api::Api& api,
                     absl::Status& creation_status)
    : OverrideLayerImpl{name} {
  creation_status = walkDirectory(path, "", 1, api);
}

absl::Status DiskLayer::walkDirectory(const std::string& path, const std::string& prefix,
                                      uint32_t depth, Api::Api& api) {
  // Maximum recursion depth for walkDirectory().
  static constexpr uint32_t MaxWalkDepth = 16;

  ENVOY_LOG(debug, "walking directory: {}", path);
  if (depth > MaxWalkDepth) {
    return absl::InvalidArgumentError(absl::StrCat("Walk recursion depth exceeded ", MaxWalkDepth));
  }
  // Check if this is an obviously bad path.
  if (api.fileSystem().illegalPath(path)) {
    return absl::InvalidArgumentError(absl::StrCat("Invalid path: ", path));
  }

  Filesystem::Directory directory(path);
  Filesystem::DirectoryIteratorImpl it = directory.begin();
  RETURN_IF_NOT_OK_REF(it.status());
  for (; it != directory.end(); ++it) {
    RETURN_IF_NOT_OK_REF(it.status());
    Filesystem::DirectoryEntry entry = *it;
    std::string full_path = path + "/" + entry.name_;
    std::string full_prefix;
    if (prefix.empty()) {
      full_prefix = entry.name_;
    } else {
      full_prefix = prefix + "." + entry.name_;
    }

    if (entry.type_ == Filesystem::FileType::Directory && entry.name_ != "." &&
        entry.name_ != "..") {
      absl::Status status = walkDirectory(full_path, full_prefix, depth + 1, api);
      RETURN_IF_NOT_OK(status);
    } else if (entry.type_ == Filesystem::FileType::Regular) {
      // Suck the file into a string. This is not very efficient but it should be good enough
      // for small files. Also, as noted elsewhere, none of this is non-blocking which could
      // theoretically lead to issues.
      ENVOY_LOG(debug, "reading file: {}", full_path);
      std::string value;

      // Read the file and remove any comments. A comment is a line starting with a '#' character.
      // Comments are useful for placeholder files with no value.
      auto file_or_error = api.fileSystem().fileReadToEnd(full_path);
      RETURN_IF_NOT_OK_REF(file_or_error.status());
      const std::string text_file{file_or_error.value()};

      const auto lines = StringUtil::splitToken(text_file, "\n");
      for (const auto& line : lines) {
        if (!line.empty() && line.front() == '#') {
          continue;
        }
        if (line == lines.back()) {
          const absl::string_view trimmed = StringUtil::rtrim(line);
          value.append(trimmed.data(), trimmed.size());
        } else {
          value.append(std::string{line} + "\n");
        }
      }
      // Separate erase/insert calls required due to the value type being constant; this prevents
      // the use of the [] operator. Can leverage insert_or_assign in C++17 in the future.
      values_.erase(full_prefix);
#ifdef ENVOY_ENABLE_YAML
      SnapshotImpl::addEntry(values_, full_prefix, ValueUtil::loadFromYaml(value), value);
#else
      IS_ENVOY_BUG("Runtime admin reload requires YAML support");
      UNREFERENCED_PARAMETER(value);
      return absl::OkStatus();
#endif
    }
  }
  RETURN_IF_NOT_OK_REF(it.status());
  return absl::OkStatus();
}

ProtoLayer::ProtoLayer(absl::string_view name, const ProtobufWkt::Struct& proto,
                       absl::Status& creation_status)
    : OverrideLayerImpl{name} {
  creation_status = absl::OkStatus();
  for (const auto& f : proto.fields()) {
    creation_status = walkProtoValue(f.second, f.first);
    if (!creation_status.ok()) {
      return;
    }
  }
}

absl::Status ProtoLayer::walkProtoValue(const ProtobufWkt::Value& v, const std::string& prefix) {
  switch (v.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kListValue:
  case ProtobufWkt::Value::kNullValue:
    return absl::InvalidArgumentError(absl::StrCat("Invalid runtime entry value for ", prefix));
    break;
  case ProtobufWkt::Value::kStringValue:
    SnapshotImpl::addEntry(values_, prefix, v, "");
    break;
  case ProtobufWkt::Value::kNumberValue:
  case ProtobufWkt::Value::kBoolValue:
    if (hasRuntimePrefix(prefix) && !isRuntimeFeature(prefix)) {
      IS_ENVOY_BUG(absl::StrCat(
          "Using a removed guard ", prefix,
          ". In future version of Envoy this will be treated as invalid configuration"));
    }
    SnapshotImpl::addEntry(values_, prefix, v, "");
    break;
  case ProtobufWkt::Value::kStructValue: {
    const ProtobufWkt::Struct& s = v.struct_value();
    if (s.fields().empty() || s.fields().find("numerator") != s.fields().end() ||
        s.fields().find("denominator") != s.fields().end()) {
      SnapshotImpl::addEntry(values_, prefix, v, "");
      break;
    }
    for (const auto& f : s.fields()) {
      absl::Status status = walkProtoValue(f.second, prefix + "." + f.first);
      RETURN_IF_NOT_OK(status);
    }
    break;
  }
  }
  return absl::OkStatus();
}

LoaderImpl::LoaderImpl(ThreadLocal::SlotAllocator& tls,
                       const envoy::config::bootstrap::v3::LayeredRuntime& config,
                       const LocalInfo::LocalInfo& local_info, Stats::Store& store,
                       Random::RandomGenerator& generator, Api::Api& api)
    : generator_(generator), stats_(generateStats(store)), tls_(tls.allocateSlot()),
      config_(config), service_cluster_(local_info.clusterName()), api_(api),
      init_watcher_("RTDS", [this]() { onRtdsReady(); }), store_(store) {}

absl::StatusOr<std::unique_ptr<LoaderImpl>>
LoaderImpl::create(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                   const envoy::config::bootstrap::v3::LayeredRuntime& config,
                   const LocalInfo::LocalInfo& local_info, Stats::Store& store,
                   Random::RandomGenerator& generator,
                   ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api) {
  auto loader =
      std::unique_ptr<LoaderImpl>(new LoaderImpl(tls, config, local_info, store, generator, api));
  auto result = loader->initLayers(dispatcher, validation_visitor);
  RETURN_IF_NOT_OK(result);
  return loader;
}

absl::Status LoaderImpl::initLayers(Event::Dispatcher& dispatcher,
                                    ProtobufMessage::ValidationVisitor& validation_visitor) {
  absl::Status creation_status;
  absl::node_hash_set<std::string> layer_names;
  for (const auto& layer : config_.layers()) {
    auto ret = layer_names.insert(layer.name());
    if (!ret.second) {
      return absl::InvalidArgumentError(absl::StrCat("Duplicate layer name: ", layer.name()));
    }
    switch (layer.layer_specifier_case()) {
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kStaticLayer:
      // Nothing needs to be done here.
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kAdminLayer:
      if (admin_layer_ != nullptr) {
        return absl::InvalidArgumentError(
            "Too many admin layers specified in LayeredRuntime, at most one may be specified");
      }
      admin_layer_ = std::make_unique<AdminLayer>(layer.name(), stats_);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kDiskLayer:
      if (watcher_ == nullptr) {
        watcher_ = dispatcher.createFilesystemWatcher();
      }
      creation_status = watcher_->addWatch(layer.disk_layer().symlink_root(),
                                           Filesystem::Watcher::Events::MovedTo,
                                           [this](uint32_t) { return loadNewSnapshot(); });
      RETURN_IF_NOT_OK(creation_status);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kRtdsLayer:
      subscriptions_.emplace_back(std::make_unique<RtdsSubscription>(*this, layer.rtds_layer(),
                                                                     store_, validation_visitor));
      init_manager_.add(subscriptions_.back()->init_target_);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::LAYER_SPECIFIER_NOT_SET:
      return absl::InvalidArgumentError("layer specifier not set");
    }
  }

  return loadNewSnapshot();
}

absl::Status LoaderImpl::initialize(Upstream::ClusterManager& cm) {
  cm_ = &cm;

  for (const auto& s : subscriptions_) {
    RETURN_IF_NOT_OK(s->createSubscription());
  }
  return absl::OkStatus();
}

void LoaderImpl::startRtdsSubscriptions(ReadyCallback on_done) {
  on_rtds_initialized_ = on_done;
  init_manager_.initialize(init_watcher_);
}

void LoaderImpl::onRtdsReady() {
  ENVOY_LOG(info, "RTDS has finished initialization");
  on_rtds_initialized_();
}

RtdsSubscription::RtdsSubscription(
    LoaderImpl& parent, const envoy::config::bootstrap::v3::RuntimeLayer::RtdsLayer& rtds_layer,
    Stats::Store& store, ProtobufMessage::ValidationVisitor& validation_visitor)
    : Envoy::Config::SubscriptionBase<envoy::service::runtime::v3::Runtime>(validation_visitor,
                                                                            "name"),
      parent_(parent), config_source_(rtds_layer.rtds_config()), store_(store),
      stats_scope_(store_.createScope("runtime")), resource_name_(rtds_layer.name()),
      init_target_("RTDS " + resource_name_, [this]() { start(); }) {}

absl::Status RtdsSubscription::createSubscription() {
  const auto resource_name = getResourceName();
  auto subscription_or_error = parent_.cm_->subscriptionFactory().subscriptionFromConfigSource(
      config_source_, Grpc::Common::typeUrl(resource_name), *stats_scope_, *this, resource_decoder_,
      {});
  RETURN_IF_NOT_OK(subscription_or_error.status());
  subscription_ = std::move(*subscription_or_error);
  return absl::OkStatus();
}

absl::Status
RtdsSubscription::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                 const std::string&) {
  absl::Status valid = validateUpdateSize(resources.size(), 0);
  if (!valid.ok()) {
    return valid;
  }
  const auto& runtime =
      dynamic_cast<const envoy::service::runtime::v3::Runtime&>(resources[0].get().resource());
  if (runtime.name() != resource_name_) {
    return absl::InvalidArgumentError(
        fmt::format("Unexpected RTDS runtime (expecting {}): {}", resource_name_, runtime.name()));
  }
  ENVOY_LOG(debug, "Reloading RTDS snapshot for onConfigUpdate");
  proto_.CopyFrom(runtime.layer());
  RETURN_IF_NOT_OK(parent_.loadNewSnapshot());
  init_target_.ready();
  return absl::OkStatus();
}

absl::Status
RtdsSubscription::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                                 const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                                 const std::string&) {
  absl::Status valid = validateUpdateSize(added_resources.size(), removed_resources.size());
  if (!valid.ok()) {
    return valid;
  }

  // This is a singleton subscription, so we can only have the subscribed resource added or removed,
  // but not both.
  if (!added_resources.empty()) {
    return onConfigUpdate(added_resources, added_resources[0].get().version());
  } else {
    return onConfigRemoved(removed_resources);
  }
}

void RtdsSubscription::onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                                            const EnvoyException*) {
  ASSERT(Envoy::Config::ConfigUpdateFailureReason::ConnectionFailure != reason);
  // We need to allow server startup to continue, even if we have a bad
  // config.
  init_target_.ready();
}

void RtdsSubscription::start() { subscription_->start({resource_name_}); }

absl::Status RtdsSubscription::validateUpdateSize(uint32_t added_resources_num,
                                                  uint32_t removed_resources_num) {
  if (added_resources_num + removed_resources_num != 1) {
    init_target_.ready();
    return absl::InvalidArgumentError(
        fmt::format("Unexpected RTDS resource length, number of added resources "
                    "{}, number of removed resources {}",
                    added_resources_num, removed_resources_num));
  }
  return absl::OkStatus();
}

absl::Status RtdsSubscription::onConfigRemoved(
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  if (removed_resources[0] != resource_name_) {
    return absl::InvalidArgumentError(
        fmt::format("Unexpected removal of unknown RTDS runtime layer {}, expected {}",
                    removed_resources[0], resource_name_));
  }
  ENVOY_LOG(debug, "Clear RTDS snapshot for onConfigUpdate");
  proto_.Clear();
  RETURN_IF_NOT_OK(parent_.loadNewSnapshot());
  init_target_.ready();
  return absl::OkStatus();
}

absl::Status LoaderImpl::loadNewSnapshot() {
  auto snapshot_or_error = createNewSnapshot();
  RETURN_IF_NOT_OK_REF(snapshot_or_error.status());
  std::shared_ptr<SnapshotImpl> ptr = std::move(snapshot_or_error.value());
  tls_->set([ptr](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::static_pointer_cast<ThreadLocal::ThreadLocalObject>(ptr);
  });

  refreshReloadableFlags(ptr->values());

  {
    absl::MutexLock lock(&snapshot_mutex_);
    thread_safe_snapshot_ = ptr;
  }
  return absl::OkStatus();
}

const Snapshot& LoaderImpl::snapshot() {
  ASSERT(tls_->currentThreadRegistered(),
         "snapshot can only be called from a worker thread or after the main thread is registered");
  return tls_->getTyped<Snapshot>();
}

SnapshotConstSharedPtr LoaderImpl::threadsafeSnapshot() {
  if (tls_->currentThreadRegistered()) {
    return std::dynamic_pointer_cast<const Snapshot>(tls_->get());
  }

  {
    absl::ReaderMutexLock lock(&snapshot_mutex_);
    return thread_safe_snapshot_;
  }
}

absl::Status LoaderImpl::mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
  if (admin_layer_ == nullptr) {
    return absl::InvalidArgumentError("No admin layer specified");
  }
  RETURN_IF_NOT_OK(admin_layer_->mergeValues(values));
  return loadNewSnapshot();
}

Stats::Scope& LoaderImpl::getRootScope() { return *store_.rootScope(); }

void LoaderImpl::countDeprecatedFeatureUse() const { countDeprecatedFeatureUseInternal(stats_); }

RuntimeStats LoaderImpl::generateStats(Stats::Store& store) {
  std::string prefix = "runtime.";
  RuntimeStats stats{
      ALL_RUNTIME_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix))};
  return stats;
}

absl::StatusOr<SnapshotImplPtr> LoaderImpl::createNewSnapshot() {
  std::vector<Snapshot::OverrideLayerConstPtr> layers;
  uint32_t disk_layers = 0;
  uint32_t error_layers = 0;
  uint32_t rtds_layer = 0;
  absl::Status creation_status;
  for (const auto& layer : config_.layers()) {
    switch (layer.layer_specifier_case()) {
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kStaticLayer:
      layers.emplace_back(
          std::make_unique<const ProtoLayer>(layer.name(), layer.static_layer(), creation_status));
      RETURN_IF_NOT_OK(creation_status);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kDiskLayer: {
      std::string path =
          layer.disk_layer().symlink_root() + "/" + layer.disk_layer().subdirectory();
      if (layer.disk_layer().append_service_cluster()) {
        absl::StrAppend(&path, "/", service_cluster_);
      }
      if (api_.fileSystem().directoryExists(path)) {
        std::unique_ptr<DiskLayer> disk_layer;
        std::string error;
        TRY_ASSERT_MAIN_THREAD {
          absl::Status creation_status;
          disk_layer = std::make_unique<DiskLayer>(layer.name(), path, api_, creation_status);
          if (!creation_status.ok()) {
            error = creation_status.message();
          }
          END_TRY
        }
        CATCH(EnvoyException & e, { error = e.what(); });
        if (error.empty()) {
          layers.emplace_back(std::move(disk_layer));
          ++disk_layers;
        } else {
          // TODO(htuch): Consider latching here, rather than ignoring the
          // layer. This would be consistent with filesystem RTDS.
          ++error_layers;
          ENVOY_LOG(debug, "error loading runtime values for layer {} from disk: {}",
                    layer.DebugString(), error);
        }
      }
      break;
    }
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kAdminLayer:
      layers.push_back(std::make_unique<AdminLayer>(*admin_layer_));
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kRtdsLayer: {
      auto* subscription = subscriptions_[rtds_layer++].get();
      layers.emplace_back(
          std::make_unique<const ProtoLayer>(layer.name(), subscription->proto_, creation_status));
      RETURN_IF_NOT_OK(creation_status);
      break;
    }
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::LAYER_SPECIFIER_NOT_SET:
      PANIC_DUE_TO_PROTO_UNSET;
    }
  }
  stats_.num_layers_.set(layers.size());
  if (error_layers == 0) {
    stats_.load_success_.inc();
  } else {
    stats_.load_error_.inc();
  }
  if (disk_layers > 1) {
    stats_.override_dir_exists_.inc();
  } else {
    stats_.override_dir_not_exists_.inc();
  }
  return std::make_unique<SnapshotImpl>(generator_, stats_, std::move(layers));
}

} // namespace Runtime
} // namespace Envoy
