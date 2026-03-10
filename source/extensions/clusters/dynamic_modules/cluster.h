#pragma once

#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/optref.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.validate.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/stats/utility.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

class DynamicModuleCluster;
class DynamicModuleClusterScheduler;
class DynamicModuleClusterTestPeer;

// Function pointer types for the cluster ABI event hooks.
using OnClusterConfigNewType = decltype(&envoy_dynamic_module_on_cluster_config_new);
using OnClusterConfigDestroyType = decltype(&envoy_dynamic_module_on_cluster_config_destroy);
using OnClusterNewType = decltype(&envoy_dynamic_module_on_cluster_new);
using OnClusterInitType = decltype(&envoy_dynamic_module_on_cluster_init);
using OnClusterDestroyType = decltype(&envoy_dynamic_module_on_cluster_destroy);
using OnClusterLbNewType = decltype(&envoy_dynamic_module_on_cluster_lb_new);
using OnClusterLbDestroyType = decltype(&envoy_dynamic_module_on_cluster_lb_destroy);
using OnClusterLbChooseHostType = decltype(&envoy_dynamic_module_on_cluster_lb_choose_host);
using OnClusterScheduledType = decltype(&envoy_dynamic_module_on_cluster_scheduled);
using OnClusterServerInitializedType =
    decltype(&envoy_dynamic_module_on_cluster_server_initialized);
using OnClusterDrainStartedType = decltype(&envoy_dynamic_module_on_cluster_drain_started);
using OnClusterShutdownType = decltype(&envoy_dynamic_module_on_cluster_shutdown);

/**
 * Configuration for a dynamic module cluster. This holds the loaded dynamic module, resolved
 * function pointers, the in-module configuration, and metrics storage.
 */
class DynamicModuleClusterConfig {
public:
  /**
   * Creates a new DynamicModuleClusterConfig.
   *
   * @param cluster_name the name identifying the cluster implementation in the module.
   * @param cluster_config the configuration bytes to pass to the module.
   * @param module the loaded dynamic module.
   * @param stats_scope the stats scope for creating custom metrics.
   * @return a shared pointer to the config, or an error status.
   */
  static absl::StatusOr<std::shared_ptr<DynamicModuleClusterConfig>>
  create(const std::string& cluster_name, const std::string& cluster_config,
         Envoy::Extensions::DynamicModules::DynamicModulePtr module, Stats::Scope& stats_scope);

  ~DynamicModuleClusterConfig();

  // Function pointers resolved from the dynamic module.
  OnClusterConfigNewType on_cluster_config_new_ = nullptr;
  OnClusterConfigDestroyType on_cluster_config_destroy_ = nullptr;
  OnClusterNewType on_cluster_new_ = nullptr;
  OnClusterInitType on_cluster_init_ = nullptr;
  OnClusterDestroyType on_cluster_destroy_ = nullptr;
  OnClusterLbNewType on_cluster_lb_new_ = nullptr;
  OnClusterLbDestroyType on_cluster_lb_destroy_ = nullptr;
  OnClusterLbChooseHostType on_cluster_lb_choose_host_ = nullptr;
  OnClusterScheduledType on_cluster_scheduled_ = nullptr;
  OnClusterServerInitializedType on_cluster_server_initialized_ = nullptr;
  OnClusterDrainStartedType on_cluster_drain_started_ = nullptr;
  OnClusterShutdownType on_cluster_shutdown_ = nullptr;

  // The in-module configuration pointer.
  envoy_dynamic_module_type_cluster_config_module_ptr in_module_config_ = nullptr;

  // ----------------------------- Metrics Support -----------------------------

  class ModuleCounterHandle {
  public:
    ModuleCounterHandle(Stats::Counter& counter) : counter_(counter) {}
    void add(uint64_t value) const { counter_.add(value); }

  private:
    Stats::Counter& counter_;
  };

  class ModuleCounterVecHandle {
  public:
    ModuleCounterVecHandle(Stats::StatName name, Stats::StatNameVec label_names)
        : name_(name), label_names_(label_names) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::counterFromElements(scope, {name_}, tags).add(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
  };

  class ModuleGaugeHandle {
  public:
    ModuleGaugeHandle(Stats::Gauge& gauge) : gauge_(gauge) {}
    void add(uint64_t value) const { gauge_.add(value); }
    void sub(uint64_t value) const { gauge_.sub(value); }
    void set(uint64_t value) const { gauge_.set(value); }

  private:
    Stats::Gauge& gauge_;
  };

  class ModuleGaugeVecHandle {
  public:
    ModuleGaugeVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                         Stats::Gauge::ImportMode import_mode)
        : name_(name), label_names_(label_names), import_mode_(import_mode) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void add(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).add(amount);
    }
    void sub(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).sub(amount);
    }
    void set(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags, uint64_t amount) const {
      ASSERT(tags.has_value());
      Stats::Utility::gaugeFromElements(scope, {name_}, import_mode_, tags).set(amount);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Gauge::ImportMode import_mode_;
  };

  class ModuleHistogramHandle {
  public:
    ModuleHistogramHandle(Stats::Histogram& histogram) : histogram_(histogram) {}
    void recordValue(uint64_t value) const { histogram_.recordValue(value); }

  private:
    Stats::Histogram& histogram_;
  };

  class ModuleHistogramVecHandle {
  public:
    ModuleHistogramVecHandle(Stats::StatName name, Stats::StatNameVec label_names,
                             Stats::Histogram::Unit unit)
        : name_(name), label_names_(label_names), unit_(unit) {}
    const Stats::StatNameVec& getLabelNames() const { return label_names_; }
    void recordValue(Stats::Scope& scope, Stats::StatNameTagVectorOptConstRef tags,
                     uint64_t value) const {
      ASSERT(tags.has_value());
      Stats::Utility::histogramFromElements(scope, {name_}, unit_, tags).recordValue(value);
    }

  private:
    Stats::StatName name_;
    Stats::StatNameVec label_names_;
    Stats::Histogram::Unit unit_;
  };

// We use 1-based IDs for the metrics in the ABI, so we need to convert them to 0-based indices
// for our internal storage. These helper functions do that conversion.
#define ID_TO_INDEX(id) ((id) - 1)

  size_t addCounter(ModuleCounterHandle&& counter) {
    counters_.push_back(std::move(counter));
    return counters_.size();
  }
  size_t addCounterVec(ModuleCounterVecHandle&& counter) {
    counter_vecs_.push_back(std::move(counter));
    return counter_vecs_.size();
  }

  size_t addGauge(ModuleGaugeHandle&& gauge) {
    gauges_.push_back(std::move(gauge));
    return gauges_.size();
  }
  size_t addGaugeVec(ModuleGaugeVecHandle&& gauge) {
    gauge_vecs_.push_back(std::move(gauge));
    return gauge_vecs_.size();
  }

  size_t addHistogram(ModuleHistogramHandle&& histogram) {
    histograms_.push_back(std::move(histogram));
    return histograms_.size();
  }
  size_t addHistogramVec(ModuleHistogramVecHandle&& histogram) {
    histogram_vecs_.push_back(std::move(histogram));
    return histogram_vecs_.size();
  }

  OptRef<const ModuleCounterHandle> getCounterById(size_t id) const {
    if (id == 0 || id > counters_.size()) {
      return {};
    }
    return counters_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleCounterVecHandle> getCounterVecById(size_t id) const {
    if (id == 0 || id > counter_vecs_.size()) {
      return {};
    }
    return counter_vecs_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleGaugeHandle> getGaugeById(size_t id) const {
    if (id == 0 || id > gauges_.size()) {
      return {};
    }
    return gauges_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleGaugeVecHandle> getGaugeVecById(size_t id) const {
    if (id == 0 || id > gauge_vecs_.size()) {
      return {};
    }
    return gauge_vecs_[ID_TO_INDEX(id)];
  }

  OptRef<const ModuleHistogramHandle> getHistogramById(size_t id) const {
    if (id == 0 || id > histograms_.size()) {
      return {};
    }
    return histograms_[ID_TO_INDEX(id)];
  }
  OptRef<const ModuleHistogramVecHandle> getHistogramVecById(size_t id) const {
    if (id == 0 || id > histogram_vecs_.size()) {
      return {};
    }
    return histogram_vecs_[ID_TO_INDEX(id)];
  }

#undef ID_TO_INDEX

  const Stats::ScopeSharedPtr stats_scope_;
  Stats::StatNamePool stat_name_pool_;

private:
  DynamicModuleClusterConfig(const std::string& cluster_name, const std::string& cluster_config,
                             Envoy::Extensions::DynamicModules::DynamicModulePtr module,
                             Stats::Scope& stats_scope);

  const std::string cluster_name_;
  const std::string cluster_config_;
  Envoy::Extensions::DynamicModules::DynamicModulePtr dynamic_module_;

  std::vector<ModuleCounterHandle> counters_;
  std::vector<ModuleCounterVecHandle> counter_vecs_;
  std::vector<ModuleGaugeHandle> gauges_;
  std::vector<ModuleGaugeVecHandle> gauge_vecs_;
  std::vector<ModuleHistogramHandle> histograms_;
  std::vector<ModuleHistogramVecHandle> histogram_vecs_;
};

using DynamicModuleClusterConfigSharedPtr = std::shared_ptr<DynamicModuleClusterConfig>;

/**
 * Handle object to ensure that the destructor of DynamicModuleCluster is called on the main
 * thread.
 */
class DynamicModuleClusterHandle {
public:
  DynamicModuleClusterHandle(std::shared_ptr<DynamicModuleCluster> cluster)
      : cluster_(std::move(cluster)) {}
  ~DynamicModuleClusterHandle();

private:
  std::shared_ptr<DynamicModuleCluster> cluster_;
  friend class DynamicModuleCluster;
  friend class DynamicModuleLoadBalancer;
};

using DynamicModuleClusterHandleSharedPtr = std::shared_ptr<DynamicModuleClusterHandle>;

/**
 * The DynamicModuleCluster delegates host discovery and load balancing to a dynamic module.
 * The module manages hosts via add/remove callbacks and provides its own load balancer.
 */
class DynamicModuleCluster : public Upstream::ClusterImplBase,
                             public std::enable_shared_from_this<DynamicModuleCluster> {
public:
  ~DynamicModuleCluster() override;

  // Upstream::Cluster
  Upstream::Cluster::InitializePhase initializePhase() const override {
    return Upstream::Cluster::InitializePhase::Primary;
  }

  // Methods called by the dynamic module via ABI callbacks.
  bool addHosts(const std::vector<std::string>& addresses, const std::vector<uint32_t>& weights,
                std::vector<Upstream::HostSharedPtr>& result_hosts);
  size_t removeHosts(const std::vector<Upstream::HostSharedPtr>& hosts);
  Upstream::HostSharedPtr findHost(void* raw_host_ptr);
  void preInitComplete();

  /**
   * Called when an event is scheduled via DynamicModuleClusterScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  // Accessors.
  const DynamicModuleClusterConfigSharedPtr& config() const { return config_; }
  envoy_dynamic_module_type_cluster_module_ptr inModuleCluster() const {
    return in_module_cluster_;
  }

protected:
  DynamicModuleCluster(const envoy::config::cluster::v3::Cluster& cluster,
                       DynamicModuleClusterConfigSharedPtr config,
                       Upstream::ClusterFactoryContext& context, absl::Status& creation_status);

  // Upstream::ClusterImplBase.
  void startPreInit() override;

private:
  /**
   * Registers server lifecycle callbacks (server_initialized, drain, shutdown).
   */
  void registerLifecycleCallbacks();

  friend class DynamicModuleClusterFactory;
  friend class DynamicModuleClusterScheduler;
  friend class DynamicModuleClusterTestPeer;
  friend class DynamicModuleClusterHandle;
  friend class DynamicModuleLoadBalancer;

  DynamicModuleClusterConfigSharedPtr config_;
  envoy_dynamic_module_type_cluster_module_ptr in_module_cluster_;
  Event::Dispatcher& dispatcher_;
  Server::Configuration::ServerFactoryContext& server_context_;

  // Map from raw host pointer to shared pointer for lookup in chooseHost.
  absl::Mutex host_map_lock_;
  absl::flat_hash_map<void*, Upstream::HostSharedPtr> host_map_ ABSL_GUARDED_BY(host_map_lock_);

  // Handle for the drain close callback registration. Dropped on destruction to unregister.
  Envoy::Common::CallbackHandlePtr drain_handle_;

  // Handle for the shutdown lifecycle callback registration.
  Server::ServerLifecycleNotifier::HandlePtr shutdown_handle_;

  // Handle for the server initialized lifecycle callback registration.
  Server::ServerLifecycleNotifier::HandlePtr server_initialized_handle_;
};

/**
 * This class is used to schedule a cluster event hook from a different thread than the main thread.
 * This is created via envoy_dynamic_module_callback_cluster_scheduler_new and deleted via
 * envoy_dynamic_module_callback_cluster_scheduler_delete.
 */
class DynamicModuleClusterScheduler {
public:
  /**
   * Creates a new scheduler for the given cluster.
   */
  static DynamicModuleClusterScheduler* create(DynamicModuleCluster* cluster) {
    return new DynamicModuleClusterScheduler(cluster->weak_from_this(), cluster->dispatcher_);
  }

  void commit(uint64_t event_id) {
    dispatcher_.post([cluster = cluster_, event_id]() {
      if (std::shared_ptr<DynamicModuleCluster> cluster_shared = cluster.lock()) {
        cluster_shared->onScheduled(event_id);
      }
    });
  }

private:
  DynamicModuleClusterScheduler(std::weak_ptr<DynamicModuleCluster> cluster,
                                Event::Dispatcher& dispatcher)
      : cluster_(std::move(cluster)), dispatcher_(dispatcher) {}

  // Using a weak pointer to avoid unnecessarily extending the lifetime of the cluster.
  std::weak_ptr<DynamicModuleCluster> cluster_;
  Event::Dispatcher& dispatcher_;
};

/**
 * Load balancer that delegates to the dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer {
public:
  DynamicModuleLoadBalancer(const DynamicModuleClusterHandleSharedPtr& handle);
  ~DynamicModuleLoadBalancer() override;

  // Upstream::LoadBalancer.
  Upstream::HostSelectionResponse chooseHost(Upstream::LoadBalancerContext* context) override;
  Upstream::HostConstSharedPtr peekAnotherHost(Upstream::LoadBalancerContext*) override {
    return nullptr;
  }
  absl::optional<Upstream::SelectedPoolAndConnection>
  selectExistingConnection(Upstream::LoadBalancerContext*, const Upstream::Host&,
                           std::vector<uint8_t>&) override {
    return absl::nullopt;
  }
  OptRef<Envoy::Http::ConnectionPool::ConnectionLifetimeCallbacks> lifetimeCallbacks() override {
    return {};
  }

  // Access the priority set for lb callbacks.
  const Upstream::PrioritySet& prioritySet() const;

private:
  const DynamicModuleClusterHandleSharedPtr handle_;
  envoy_dynamic_module_type_cluster_lb_module_ptr in_module_lb_;
};

/**
 * Factory for creating DynamicModuleCluster instances.
 */
class DynamicModuleClusterFactory
    : public Upstream::ConfigurableClusterFactoryBase<
          envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig> {
public:
  DynamicModuleClusterFactory()
      : ConfigurableClusterFactoryBase("envoy.clusters.dynamic_modules") {}

private:
  friend class DynamicModuleClusterFactoryTestPeer;
  absl::StatusOr<
      std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
  createClusterWithConfig(
      const envoy::config::cluster::v3::Cluster& cluster,
      const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
      Upstream::ClusterFactoryContext& context) override;
};

DECLARE_FACTORY(DynamicModuleClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
