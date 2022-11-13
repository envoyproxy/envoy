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
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"

#include "absl/container/node_hash_map.h"
#include "absl/container/node_hash_set.h"
#include "absl/flags/flag.h"
#include "absl/strings/match.h"
#include "absl/strings/numbers.h"

#ifdef ENVOY_ENABLE_QUIC
#include "quiche_platform_impl/quiche_flags_impl.h"
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
  absl::flat_hash_map<std::string, bool> quiche_flags_override;
  for (const auto& it : flag_map) {
#ifdef ENVOY_ENABLE_QUIC
    if (absl::StartsWith(it.first, quiche::EnvoyQuicheReloadableFlagPrefix) &&
        it.second.bool_value_.has_value()) {
      quiche_flags_override[it.first.substr(quiche::EnvoyFeaturePrefix.length())] =
          it.second.bool_value_.value();
    }
#endif
    if (it.second.bool_value_.has_value() && isRuntimeFeature(it.first)) {
      maybeSetRuntimeGuard(it.first, it.second.bool_value_.value());
    }
  }
#ifdef ENVOY_ENABLE_QUIC
  quiche::FlagRegistry::getInstance().updateReloadableFlags(quiche_flags_override);
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

SnapshotImpl::Entry SnapshotImpl::createEntry(const std::string& value) {
  Entry entry;
  entry.raw_string_value_ = value;

  // As a perf optimization, attempt to parse the entry's string and store it inside the struct. If
  // we don't succeed that's fine.
  resolveEntryType(entry);

  return entry;
}

SnapshotImpl::Entry SnapshotImpl::createEntry(const ProtobufWkt::Value& value) {
  // This isn't the smartest way to do it; we're round-tripping via YAML, this should be optimized
  // if runtime parsing becomes performance sensitive.
  return createEntry(MessageUtil::getYamlStringFromMessage(value, false, false));
}

bool SnapshotImpl::parseEntryBooleanValue(Entry& entry) {
  absl::string_view stripped = entry.raw_string_value_;
  stripped = absl::StripAsciiWhitespace(stripped);

  uint64_t parse_int;
  if (absl::SimpleAtoi(stripped, &parse_int)) {
    entry.bool_value_ = (parse_int != 0);
    // This is really an integer, so return false here not because of failure, but so we continue to
    // parse doubles/int.
    return false;
  } else if (absl::EqualsIgnoreCase(stripped, "true")) {
    entry.bool_value_ = true;
    return true;
  } else if (absl::EqualsIgnoreCase(stripped, "false")) {
    entry.bool_value_ = false;
    return true;
  }
  return false;
}

bool SnapshotImpl::parseEntryDoubleValue(Entry& entry) {
  double converted_double;
  if (absl::SimpleAtod(entry.raw_string_value_, &converted_double)) {
    entry.double_value_ = converted_double;
    return true;
  }
  return false;
}

void SnapshotImpl::parseEntryFractionalPercentValue(Entry& entry) {
  envoy::type::v3::FractionalPercent converted_fractional_percent;
  TRY_ASSERT_MAIN_THREAD {
    MessageUtil::loadFromYamlAndValidate(entry.raw_string_value_, converted_fractional_percent,
                                         ProtobufMessage::getStrictValidationVisitor());
  }
  END_TRY
  catch (const ProtoValidationException& ex) {
    ENVOY_LOG(error, "unable to validate fraction percent runtime proto: {}", ex.what());
    return;
  }
  catch (const EnvoyException& ex) {
    // An EnvoyException is thrown when we try to parse a bogus string as a protobuf. This is fine,
    // since there was no expectation that the raw string was a valid proto.
    return;
  }

  entry.fractional_percent_value_ = converted_fractional_percent;
}

void AdminLayer::mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
  for (const auto& kv : values) {
    values_.erase(kv.first);
    if (!kv.second.empty()) {
      values_.emplace(kv.first, SnapshotImpl::createEntry(kv.second));
    }
  }
  stats_.admin_overrides_active_.set(values_.empty() ? 0 : 1);
}

DiskLayer::DiskLayer(absl::string_view name, const std::string& path, Api::Api& api)
    : OverrideLayerImpl{name} {
  walkDirectory(path, "", 1, api);
}

void DiskLayer::walkDirectory(const std::string& path, const std::string& prefix, uint32_t depth,
                              Api::Api& api) {
  // Maximum recursion depth for walkDirectory().
  static constexpr uint32_t MaxWalkDepth = 16;

  ENVOY_LOG(debug, "walking directory: {}", path);
  if (depth > MaxWalkDepth) {
    throw EnvoyException(absl::StrCat("Walk recursion depth exceeded ", MaxWalkDepth));
  }
  // Check if this is an obviously bad path.
  if (api.fileSystem().illegalPath(path)) {
    throw EnvoyException(absl::StrCat("Invalid path: ", path));
  }

  Filesystem::Directory directory(path);
  for (const Filesystem::DirectoryEntry& entry : directory) {
    std::string full_path = path + "/" + entry.name_;
    std::string full_prefix;
    if (prefix.empty()) {
      full_prefix = entry.name_;
    } else {
      full_prefix = prefix + "." + entry.name_;
    }

    if (entry.type_ == Filesystem::FileType::Directory && entry.name_ != "." &&
        entry.name_ != "..") {
      walkDirectory(full_path, full_prefix, depth + 1, api);
    } else if (entry.type_ == Filesystem::FileType::Regular) {
      // Suck the file into a string. This is not very efficient but it should be good enough
      // for small files. Also, as noted elsewhere, none of this is non-blocking which could
      // theoretically lead to issues.
      ENVOY_LOG(debug, "reading file: {}", full_path);
      std::string value;

      // Read the file and remove any comments. A comment is a line starting with a '#' character.
      // Comments are useful for placeholder files with no value.
      const std::string text_file{api.fileSystem().fileReadToEnd(full_path)};
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
      values_.insert({full_prefix, SnapshotImpl::createEntry(value)});
    }
  }
}

ProtoLayer::ProtoLayer(absl::string_view name, const ProtobufWkt::Struct& proto)
    : OverrideLayerImpl{name} {
  for (const auto& f : proto.fields()) {
    walkProtoValue(f.second, f.first);
  }
}

void ProtoLayer::walkProtoValue(const ProtobufWkt::Value& v, const std::string& prefix) {
  switch (v.kind_case()) {
  case ProtobufWkt::Value::KIND_NOT_SET:
  case ProtobufWkt::Value::kListValue:
  case ProtobufWkt::Value::kNullValue:
    throw EnvoyException(absl::StrCat("Invalid runtime entry value for ", prefix));
    break;
  case ProtobufWkt::Value::kStringValue:
    values_.emplace(prefix, SnapshotImpl::createEntry(v.string_value()));
    break;
  case ProtobufWkt::Value::kNumberValue:
  case ProtobufWkt::Value::kBoolValue:
    if (hasRuntimePrefix(prefix) && !isRuntimeFeature(prefix)) {
      IS_ENVOY_BUG(absl::StrCat(
          "Using a removed guard ", prefix,
          ". In future version of Envoy this will be treated as invalid configuration"));
    }
    values_.emplace(prefix, SnapshotImpl::createEntry(v));
    break;
  case ProtobufWkt::Value::kStructValue: {
    const ProtobufWkt::Struct& s = v.struct_value();
    if (s.fields().empty() || s.fields().find("numerator") != s.fields().end() ||
        s.fields().find("denominator") != s.fields().end()) {
      values_.emplace(prefix, SnapshotImpl::createEntry(v));
      break;
    }
    for (const auto& f : s.fields()) {
      walkProtoValue(f.second, prefix + "." + f.first);
    }
    break;
  }
  }
}

LoaderImpl::LoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
                       const envoy::config::bootstrap::v3::LayeredRuntime& config,
                       const LocalInfo::LocalInfo& local_info, Stats::Store& store,
                       Random::RandomGenerator& generator,
                       ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : generator_(generator), stats_(generateStats(store)), tls_(tls.allocateSlot()),
      config_(config), service_cluster_(local_info.clusterName()), api_(api),
      init_watcher_("RTDS", [this]() { onRtdsReady(); }), store_(store) {
  absl::node_hash_set<std::string> layer_names;
  for (const auto& layer : config_.layers()) {
    auto ret = layer_names.insert(layer.name());
    if (!ret.second) {
      throw EnvoyException(absl::StrCat("Duplicate layer name: ", layer.name()));
    }
    switch (layer.layer_specifier_case()) {
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kStaticLayer:
      // Nothing needs to be done here.
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kAdminLayer:
      if (admin_layer_ != nullptr) {
        throw EnvoyException(
            "Too many admin layers specified in LayeredRuntime, at most one may be specified");
      }
      admin_layer_ = std::make_unique<AdminLayer>(layer.name(), stats_);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kDiskLayer:
      if (watcher_ == nullptr) {
        watcher_ = dispatcher.createFilesystemWatcher();
      }
      watcher_->addWatch(layer.disk_layer().symlink_root(), Filesystem::Watcher::Events::MovedTo,
                         [this](uint32_t) -> void { loadNewSnapshot(); });
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kRtdsLayer:
      subscriptions_.emplace_back(
          std::make_unique<RtdsSubscription>(*this, layer.rtds_layer(), store, validation_visitor));
      init_manager_.add(subscriptions_.back()->init_target_);
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::LAYER_SPECIFIER_NOT_SET:
      throw EnvoyException("layer specifier not set");
    }
  }

  loadNewSnapshot();
}

void LoaderImpl::initialize(Upstream::ClusterManager& cm) {
  cm_ = &cm;

  for (const auto& s : subscriptions_) {
    s->createSubscription();
  }
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

void RtdsSubscription::createSubscription() {
  const auto resource_name = getResourceName();
  subscription_ = parent_.cm_->subscriptionFactory().subscriptionFromConfigSource(
      config_source_, Grpc::Common::typeUrl(resource_name), *stats_scope_, *this, resource_decoder_,
      {});
}

void RtdsSubscription::onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                                      const std::string&) {
  validateUpdateSize(resources.size(), 0);
  const auto& runtime =
      dynamic_cast<const envoy::service::runtime::v3::Runtime&>(resources[0].get().resource());
  if (runtime.name() != resource_name_) {
    throw EnvoyException(
        fmt::format("Unexpected RTDS runtime (expecting {}): {}", resource_name_, runtime.name()));
  }
  ENVOY_LOG(debug, "Reloading RTDS snapshot for onConfigUpdate");
  proto_.CopyFrom(runtime.layer());
  parent_.loadNewSnapshot();
  init_target_.ready();
}

void RtdsSubscription::onConfigUpdate(
    const std::vector<Config::DecodedResourceRef>& added_resources,
    const Protobuf::RepeatedPtrField<std::string>& removed_resources, const std::string&) {
  validateUpdateSize(added_resources.size(), removed_resources.size());

  // This is a singleton subscription, so we can only have the subscribed resource added or removed,
  // but not both.
  if (!added_resources.empty()) {
    onConfigUpdate(added_resources, added_resources[0].get().version());
  } else {
    onConfigRemoved(removed_resources);
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

void RtdsSubscription::validateUpdateSize(uint32_t added_resources_num,
                                          uint32_t removed_resources_num) {
  if (added_resources_num + removed_resources_num != 1) {
    init_target_.ready();
    throw EnvoyException(fmt::format("Unexpected RTDS resource length, number of added recources "
                                     "{}, number of removed recources {}",
                                     added_resources_num, removed_resources_num));
  }
}

void RtdsSubscription::onConfigRemoved(
    const Protobuf::RepeatedPtrField<std::string>& removed_resources) {
  if (removed_resources[0] != resource_name_) {
    throw EnvoyException(
        fmt::format("Unexpected removal of unknown RTDS runtime layer {}, expected {}",
                    removed_resources[0], resource_name_));
  }
  ENVOY_LOG(debug, "Clear RTDS snapshot for onConfigUpdate");
  proto_.Clear();
  parent_.loadNewSnapshot();
  init_target_.ready();
}

void LoaderImpl::loadNewSnapshot() {
  std::shared_ptr<SnapshotImpl> ptr = createNewSnapshot();
  tls_->set([ptr](Event::Dispatcher&) -> ThreadLocal::ThreadLocalObjectSharedPtr {
    return std::static_pointer_cast<ThreadLocal::ThreadLocalObject>(ptr);
  });

  refreshReloadableFlags(ptr->values());

  {
    absl::MutexLock lock(&snapshot_mutex_);
    thread_safe_snapshot_ = ptr;
  }
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

void LoaderImpl::mergeValues(const absl::node_hash_map<std::string, std::string>& values) {
  if (admin_layer_ == nullptr) {
    throw EnvoyException("No admin layer specified");
  }
  admin_layer_->mergeValues(values);
  loadNewSnapshot();
}

Stats::Scope& LoaderImpl::getRootScope() { return store_; }

void LoaderImpl::countDeprecatedFeatureUse() const { countDeprecatedFeatureUseInternal(stats_); }

RuntimeStats LoaderImpl::generateStats(Stats::Store& store) {
  std::string prefix = "runtime.";
  RuntimeStats stats{
      ALL_RUNTIME_STATS(POOL_COUNTER_PREFIX(store, prefix), POOL_GAUGE_PREFIX(store, prefix))};
  return stats;
}

SnapshotImplPtr LoaderImpl::createNewSnapshot() {
  std::vector<Snapshot::OverrideLayerConstPtr> layers;
  uint32_t disk_layers = 0;
  uint32_t error_layers = 0;
  uint32_t rtds_layer = 0;
  for (const auto& layer : config_.layers()) {
    switch (layer.layer_specifier_case()) {
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kStaticLayer:
      layers.emplace_back(std::make_unique<const ProtoLayer>(layer.name(), layer.static_layer()));
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kDiskLayer: {
      std::string path =
          layer.disk_layer().symlink_root() + "/" + layer.disk_layer().subdirectory();
      if (layer.disk_layer().append_service_cluster()) {
        path += "/" + service_cluster_;
      }
      if (api_.fileSystem().directoryExists(path)) {
        TRY_ASSERT_MAIN_THREAD {
          layers.emplace_back(std::make_unique<DiskLayer>(layer.name(), path, api_));
          ++disk_layers;
        }
        END_TRY
        catch (EnvoyException& e) {
          // TODO(htuch): Consider latching here, rather than ignoring the
          // layer. This would be consistent with filesystem RTDS.
          ++error_layers;
          ENVOY_LOG(debug, "error loading runtime values for layer {} from disk: {}",
                    layer.DebugString(), e.what());
        }
      }
      break;
    }
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kAdminLayer:
      layers.push_back(std::make_unique<AdminLayer>(*admin_layer_));
      break;
    case envoy::config::bootstrap::v3::RuntimeLayer::LayerSpecifierCase::kRtdsLayer: {
      auto* subscription = subscriptions_[rtds_layer++].get();
      layers.emplace_back(std::make_unique<const ProtoLayer>(layer.name(), subscription->proto_));
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
