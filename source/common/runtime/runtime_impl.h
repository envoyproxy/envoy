#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "envoy/api/api.h"
#include "envoy/common/exception.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/init/manager.h"
#include "envoy/runtime/runtime.h"
#include "envoy/service/discovery/v2/rtds.pb.validate.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/store.h"
#include "envoy/thread_local/thread_local.h"
#include "envoy/type/percent.pb.validate.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/empty_string.h"
#include "common/common/logger.h"
#include "common/common/thread.h"
#include "common/init/target_impl.h"
#include "common/runtime/runtime_features.h"
#include "common/singleton/threadsafe_singleton.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Runtime {

using RuntimeSingleton = ThreadSafeSingleton<Loader>;

/**
 * Implementation of RandomGenerator that uses per-thread RANLUX generators seeded with current
 * time.
 */
class RandomGeneratorImpl : public RandomGenerator {
public:
  // Runtime::RandomGenerator
  uint64_t random() override;
  std::string uuid() override;

  static const size_t UUID_LENGTH;
};

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
class SnapshotImpl : public Snapshot,
                     public ThreadLocal::ThreadLocalObject,
                     Logger::Loggable<Logger::Id::runtime> {
public:
  SnapshotImpl(RandomGenerator& generator, RuntimeStats& stats,
               std::vector<OverrideLayerConstPtr>&& layers);

  // Runtime::Snapshot
  bool deprecatedFeatureEnabled(const std::string& key) const override;
  bool runtimeFeatureEnabled(absl::string_view key) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value, uint64_t random_value,
                      uint64_t num_buckets) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value) const override;
  bool featureEnabled(const std::string& key, uint64_t default_value,
                      uint64_t random_value) const override;
  bool featureEnabled(const std::string& key,
                      const envoy::type::FractionalPercent& default_value) const override;
  bool featureEnabled(const std::string& key, const envoy::type::FractionalPercent& default_value,
                      uint64_t random_value) const override;
  const std::string& get(const std::string& key) const override;
  uint64_t getInteger(const std::string& key, uint64_t default_value) const override;
  double getDouble(const std::string& key, double default_value) const override;
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
  RandomGenerator& generator_;
  RuntimeStats& stats_;
};

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

struct RtdsSubscription : Config::SubscriptionCallbacks, Logger::Loggable<Logger::Id::runtime> {
  RtdsSubscription(LoaderImpl& parent,
                   const envoy::config::bootstrap::v2::RuntimeLayer::RtdsLayer& rtds_layer,
                   Stats::Store& store, ProtobufMessage::ValidationVisitor& validation_visitor);

  // Config::SubscriptionCallbacks
  void onConfigUpdate(const Protobuf::RepeatedPtrField<ProtobufWkt::Any>& resources,
                      const std::string&) override;
  void onConfigUpdate(const Protobuf::RepeatedPtrField<envoy::api::v2::Resource>& added_resources,
                      const Protobuf::RepeatedPtrField<std::string>& removed_resources,
                      const std::string&) override;

  void onConfigUpdateFailed(Envoy::Config::ConfigUpdateFailureReason reason,
                            const EnvoyException* e) override;
  std::string resourceName(const ProtobufWkt::Any& resource) override {
    return MessageUtil::anyConvert<envoy::service::discovery::v2::Runtime>(resource).name();
  }

  void start();
  void validateUpdateSize(uint32_t num_resources);

  LoaderImpl& parent_;
  const envoy::api::v2::core::ConfigSource config_source_;
  Stats::Store& store_;
  Config::SubscriptionPtr subscription_;
  std::string resource_name_;
  Init::TargetImpl init_target_;
  ProtobufWkt::Struct proto_;
  ProtobufMessage::ValidationVisitor& validation_visitor_;
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
             const envoy::config::bootstrap::v2::LayeredRuntime& config,
             const LocalInfo::LocalInfo& local_info, Init::Manager& init_manager,
             Stats::Store& store, RandomGenerator& generator,
             ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api);

  // Runtime::Loader
  void initialize(Upstream::ClusterManager& cm) override;
  const Snapshot& snapshot() override;
  std::shared_ptr<const Snapshot> threadsafeSnapshot() override;
  void mergeValues(const std::unordered_map<std::string, std::string>& values) override;

private:
  friend RtdsSubscription;

  // Create a new Snapshot
  virtual std::unique_ptr<SnapshotImpl> createNewSnapshot();
  // Load a new Snapshot into TLS
  void loadNewSnapshot();
  RuntimeStats generateStats(Stats::Store& store);

  RandomGenerator& generator_;
  RuntimeStats stats_;
  AdminLayerPtr admin_layer_;
  ThreadLocal::SlotPtr tls_;
  const envoy::config::bootstrap::v2::LayeredRuntime config_;
  const std::string service_cluster_;
  Filesystem::WatcherPtr watcher_;
  Api::Api& api_;
  std::vector<RtdsSubscriptionPtr> subscriptions_;
  Upstream::ClusterManager* cm_{};

  absl::Mutex snapshot_mutex_;
  std::shared_ptr<const Snapshot> thread_safe_snapshot_ ABSL_GUARDED_BY(snapshot_mutex_);
};

} // namespace Runtime
} // namespace Envoy
