#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/init/manager.h"
#include "envoy/runtime/runtime.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.h"
#include "envoy/service/runtime/v3/rtds.pb.validate.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/store.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/v3/percent.pb.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/config/subscription_base.h"
#include "common/init/manager_impl.h"
#include "common/init/target_impl.h"
#include "common/singleton/threadsafe_singleton.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Runtime {

using RuntimeSingleton = ThreadSafeSingleton<Loader>;

/**
 * All runtime stats. @see stats_macros.h
 */
#define ALL_RUNTIME_STATS(COUNTER, GAUGE)                                                          \
  COUNTER(deprecated_feature_use)                                                                  \
  COUNTER(load_error)                                                                              \
  COUNTER(load_success)                                                                            \
  COUNTER(override_dir_exists)                                                                     \
  COUNTER(override_dir_not_exists)                                                                 \
  GAUGE(admin_overrides_active, NeverImport)                                                       \
  GAUGE(deprecated_feature_seen_since_process_start, NeverImport)                                  \
  GAUGE(num_keys, NeverImport)                                                                     \
  GAUGE(num_layers, NeverImport)

/**
 * Struct definition for all runtime stats. @see stats_macros.h
 */
struct RuntimeStats {
  ALL_RUNTIME_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Implementation of Snapshot whose source is the vector of layers passed to the constructor.
 */
class SnapshotImpl : public Snapshot, Logger::Loggable<Logger::Id::runtime> {
public:
  SnapshotImpl(Random::RandomGenerator& generator, RuntimeStats& stats,
               std::vector<OverrideLayerConstPtr>&& layers);

  // Runtime::Snapshot
  void countDeprecatedFeatureUse() const override;
  bool deprecatedFeatureEnabled(absl::string_view key, bool default_value) const override;
  bool runtimeFeatureEnabled(absl::string_view key) const override;
  bool featureEnabled(absl::string_view key, uint64_t default_value, uint64_t random_value,
                      uint64_t num_buckets) const override;
  bool featureEnabled(absl::string_view key, uint64_t default_value) const override;
  bool featureEnabled(absl::string_view key, uint64_t default_value,
                      uint64_t random_value) const override;
  bool featureEnabled(absl::string_view key,
                      const envoy::type::v3::FractionalPercent& default_value) const override;
  bool featureEnabled(absl::string_view key,
                      const envoy::type::v3::FractionalPercent& default_value,
                      uint64_t random_value) const override;
  ConstStringOptRef get(absl::string_view key) const override;
  uint64_t getInteger(absl::string_view key, uint64_t default_value) const override;
  double getDouble(absl::string_view key, double default_value) const override;
  bool getBoolean(absl::string_view key, bool value) const override;
  const std::vector<OverrideLayerConstPtr>& getLayers() const override;

  static Entry createEntry(const std::string& value);
  static Entry createEntry(const ProtobufWkt::Value& value);

private:
  static void resolveEntryType(Entry& entry) {
    if (parseEntryBooleanValue(entry)) {
      return;
    }

    if (parseEntryDoubleValue(entry) && entry.double_value_ >= 0 &&
        entry.double_value_ <= std::numeric_limits<uint64_t>::max()) {
      // Valid uint values will always be parseable as doubles, so we assign the value to both the
      // uint and double fields. In cases where the value is something like "3.1", we will floor the
      // number by casting it to a uint and assigning the uint value.
      entry.uint_value_ = entry.double_value_;
      return;
    }

    parseEntryFractionalPercentValue(entry);
  }

  static bool parseEntryBooleanValue(Entry& entry);
  static bool parseEntryDoubleValue(Entry& entry);
  static void parseEntryFractionalPercentValue(Entry& entry);

  const std::vector<OverrideLayerConstPtr> layers_;
  EntryMap values_;
  Random::RandomGenerator& generator_;
  RuntimeStats& stats_;
};

using SnapshotImplPtr = std::unique_ptr<SnapshotImpl>;

/**
 * Base implementation of OverrideLayer that by itself provides an empty values map.
 */
class OverrideLayerImpl : public Snapshot::OverrideLayer {
public:
  explicit OverrideLayerImpl(absl::string_view name) : name_{name} {}
  const Snapshot::EntryMap& values() const override { return values_; }
  const std::string& name() const override { return name_; }

protected:
  Snapshot::EntryMap values_;
  const std::string name_;
};

/**
 * Extension of OverrideLayerImpl that maintains an in-memory set of values. These values can be
 * modified programmatically via mergeValues(). AdminLayer is so named because it can be accessed
 * and manipulated by Envoy's admin interface.
 */
class AdminLayer : public OverrideLayerImpl {
public:
  explicit AdminLayer(absl::string_view name, RuntimeStats& stats)
      : OverrideLayerImpl{name}, stats_{stats} {}
  /**
   * Copy-constructible so that it can snapshotted.
   */
  AdminLayer(const AdminLayer& admin_layer) : AdminLayer{admin_layer.name_, admin_layer.stats_} {
    values_ = admin_layer.values();
  }

  /**
   * Merge the provided values into our entry map. An empty value indicates that a key should be
   * removed from our map.
   */
  void mergeValues(const std::unordered_map<std::string, std::string>& values);

private:
  RuntimeStats& stats_;
};

using AdminLayerPtr = std::unique_ptr<AdminLayer>;

/**
 * Extension of OverrideLayerImpl that loads values from the file system upon construction.
 */
class DiskLayer : public OverrideLayerImpl, Logger::Loggable<Logger::Id::runtime> {
public:
  DiskLayer(absl::string_view name, const std::string& path, Api::Api& api);

private:
  void walkDirectory(const std::string& path, const std::string& prefix, uint32_t depth,
                     Api::Api& api);

  const std::string path_;
  const Filesystem::WatcherPtr watcher_;
};

/**
 * Extension of OverrideLayerImpl that loads values from a proto Struct representation.
 */
class ProtoLayer : public OverrideLayerImpl, Logger::Loggable<Logger::Id::runtime> {
public:
  ProtoLayer(absl::string_view name, const ProtobufWkt::Struct& proto);

private:
  void walkProtoValue(const ProtobufWkt::Value& v, const std::string& prefix);
};

class LoaderImpl;

struct RtdsSubscription : Envoy::Config::SubscriptionBase<envoy::service::runtime::v3::Runtime>,
                          Logger::Loggable<Logger::Id::runtime> {
  RtdsSubscription(LoaderImpl& parent,
                   const envoy::config::bootstrap::v3::RuntimeLayer::RtdsLayer& rtds_layer,
                   Stats::Store& store, ProtobufMessage::ValidationVisitor& validation_visitor);

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& resources,
                      const std::string& version_info) override;
  void onConfigUpdate(const std::vector<Config::DecodedResourceRef>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override;

  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;

  void start();
  void validateUpdateSize(uint32_t num_resources);
  void createSubscription();

  LoaderImpl& parent_;
  const envoy::config::core::v3::ConfigSource config_source_;
  Stats::Store& store_;
  Config::SubscriptionPtr subscription_;
  std::string resource_name_;
  Init::TargetImpl init_target_;
  ProtobufWkt::Struct proto_;
};

using RtdsSubscriptionPtr = std::unique_ptr<RtdsSubscription>;

/**
 * Implementation of Loader that provides Snapshots of values added via mergeValues().
 * A single snapshot is shared among all threads and referenced by shared_ptr such that
 * a new runtime can be swapped in by the main thread while workers are still using the previous
 * version.
 */
class LoaderImpl : public Loader, Logger::Loggable<Logger::Id::runtime> {
public:
  LoaderImpl(Event::Dispatcher& dispatcher, ThreadLocal::SlotAllocator& tls,
             const envoy::config::bootstrap::v3::LayeredRuntime& config,
             const LocalInfo::LocalInfo& local_info, Stats::Store& store,
             Random::RandomGenerator& generator,
             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Runtime::Loader
  void initialize(Upstream::ClusterManager& cm) override;
  const Snapshot& snapshot() override;
  SnapshotConstSharedPtr threadsafeSnapshot() override;
  void mergeValues(const std::unordered_map<std::string, std::string>& values) override;
  void startRtdsSubscriptions(ReadyCallback on_done) override;
  Stats::Scope& getRootScope() override;

private:
  friend RtdsSubscription;

  // Create a new Snapshot
  SnapshotImplPtr createNewSnapshot();
  // Load a new Snapshot into TLS
  void loadNewSnapshot();
  RuntimeStats generateStats(Stats::Store& store);
  void onRdtsReady();

  Random::RandomGenerator& generator_;
  RuntimeStats stats_;
  AdminLayerPtr admin_layer_;
  ThreadLocal::SlotPtr tls_;
  const envoy::config::bootstrap::v3::LayeredRuntime config_;
  const std::string service_cluster_;
  Filesystem::WatcherPtr watcher_;
  Api::Api& api_;
  ReadyCallback on_rtds_initialized_;
  Init::WatcherImpl init_watcher_;
  Init::ManagerImpl init_manager_{"RTDS"};
  std::vector<RtdsSubscriptionPtr> subscriptions_;
  Upstream::ClusterManager* cm_{};
  Stats::Store& store_;

  absl::Mutex snapshot_mutex_;
  SnapshotConstSharedPtr thread_safe_snapshot_ ABSL_GUARDED_BY(snapshot_mutex_);
};

} // namespace Runtime
} // namespace Envoy
