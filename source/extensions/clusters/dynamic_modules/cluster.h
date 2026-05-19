#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "envoy/common/callback.h"
#include "envoy/common/optref.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"
#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.validate.h"
#include "envoy/http/async_client.h"
#include "envoy/server/lifecycle_notifier.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/upstream.h"

#include "source/common/common/logger.h"
#include "source/common/http/message_impl.h"
#include "source/common/stats/utility.h"
#include "source/common/upstream/cluster_factory_impl.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/dynamic_modules/abi/abi.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

#include "absl/container/flat_hash_map.h"
#include "absl/container/flat_hash_set.h"
#include "absl/functional/function_ref.h"

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
using OnClusterLbCancelHostSelectionType =
    decltype(&envoy_dynamic_module_on_cluster_lb_cancel_host_selection);
using OnClusterScheduledType = decltype(&envoy_dynamic_module_on_cluster_scheduled);
using OnClusterServerInitializedType =
    decltype(&envoy_dynamic_module_on_cluster_server_initialized);
using OnClusterDrainStartedType = decltype(&envoy_dynamic_module_on_cluster_drain_started);
using OnClusterShutdownType = decltype(&envoy_dynamic_module_on_cluster_shutdown);
using OnClusterHttpCalloutDoneType = decltype(&envoy_dynamic_module_on_cluster_http_callout_done);
using OnClusterLbOnHostMembershipUpdateType =
    decltype(&envoy_dynamic_module_on_cluster_lb_on_host_membership_update);

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
  OnClusterLbCancelHostSelectionType on_cluster_lb_cancel_host_selection_ = nullptr;
  OnClusterScheduledType on_cluster_scheduled_ = nullptr;
  OnClusterServerInitializedType on_cluster_server_initialized_ = nullptr;
  OnClusterDrainStartedType on_cluster_drain_started_ = nullptr;
  OnClusterShutdownType on_cluster_shutdown_ = nullptr;
  OnClusterHttpCalloutDoneType on_cluster_http_callout_done_ = nullptr;
  OnClusterLbOnHostMembershipUpdateType on_cluster_lb_on_host_membership_update_ = nullptr;

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
  // We only allow the module to create stats during on_cluster_config_new, and not later from
  // worker threads, so that we don't have to wrap stat_name_pool_ in a lock. Per-request label
  // values use a stack-local Stats::StatNameDynamicPool in the increment callbacks (see
  // abi_impl.cc).
  bool stat_creation_frozen_ = false;

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

  // Access the cluster for host lookup during async completion.
  DynamicModuleCluster* cluster() const { return cluster_.get(); }

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
  bool addHosts(
      const std::vector<std::string>& addresses, const std::vector<uint32_t>& weights,
      const std::vector<std::string>& regions, const std::vector<std::string>& zones,
      const std::vector<std::string>& sub_zones,
      const std::vector<std::vector<std::tuple<std::string, std::string, std::string>>>& metadata,
      std::vector<Upstream::HostSharedPtr>& result_hosts, uint32_t priority = 0);
  size_t removeHosts(const std::vector<Upstream::HostSharedPtr>& hosts);
  bool updateHostHealth(Upstream::HostSharedPtr host,
                        envoy_dynamic_module_type_host_health health_status);
  Upstream::HostSharedPtr findHost(void* raw_host_ptr);
  Upstream::HostSharedPtr findHostByAddress(const std::string& address);
  void preInitComplete();

  /**
   * Called when an event is scheduled via DynamicModuleClusterScheduler::commit.
   */
  void onScheduled(uint64_t event_id);

  /**
   * Sends an HTTP callout to the specified cluster with the given message.
   * This must be called on the main thread.
   *
   * @param callout_id_out is a pointer to a variable where the callout ID will be stored.
   * @param cluster_name is the name of the cluster to which the callout is sent.
   * @param message is the HTTP request message to send.
   * @param timeout_milliseconds is the timeout for the callout in milliseconds.
   * @return the result of the callout initialization.
   */
  envoy_dynamic_module_type_http_callout_init_result
  sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                  Http::RequestMessagePtr&& message, uint64_t timeout_milliseconds);

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

  /**
   * This implementation of the AsyncClient::Callbacks handles the response from the HTTP callout.
   */
  class HttpCalloutCallback : public Http::AsyncClient::Callbacks {
  public:
    HttpCalloutCallback(std::shared_ptr<DynamicModuleCluster> cluster, uint64_t id)
        : cluster_(std::move(cluster)), callout_id_(id) {}
    ~HttpCalloutCallback() override = default;

    void onSuccess(const Http::AsyncClient::Request& request,
                   Http::ResponseMessagePtr&& response) override;
    void onFailure(const Http::AsyncClient::Request& request,
                   Http::AsyncClient::FailureReason reason) override;
    void onBeforeFinalizeUpstreamSpan(Envoy::Tracing::Span&,
                                      const Http::ResponseHeaderMap*) override {};

    Http::AsyncClient::Request* request_ = nullptr;

  private:
    const std::shared_ptr<DynamicModuleCluster> cluster_;
    const uint64_t callout_id_{};
  };

  uint64_t getNextCalloutId() { return next_callout_id_++; }

  DynamicModuleClusterConfigSharedPtr config_;
  envoy_dynamic_module_type_cluster_module_ptr in_module_cluster_{nullptr};
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

  // HTTP callout tracking.
  uint64_t next_callout_id_ = 1; // 0 is reserved as an invalid id.
  absl::flat_hash_map<uint64_t, std::unique_ptr<HttpCalloutCallback>> http_callouts_;
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
    return new DynamicModuleClusterScheduler(cluster->weak_from_this());
  }

  void commit(uint64_t event_id) {
    // Lock the cluster so its dispatcher member stays valid across `post`.
    auto cluster_shared = cluster_.lock();
    if (!cluster_shared) {
      return;
    }
    cluster_shared->dispatcher_.post([cluster = cluster_, event_id]() {
      if (std::shared_ptr<DynamicModuleCluster> cs = cluster.lock()) {
        cs->onScheduled(event_id);
      }
    });
  }

private:
  explicit DynamicModuleClusterScheduler(std::weak_ptr<DynamicModuleCluster> cluster)
      : cluster_(std::move(cluster)) {}

  // Using a weak pointer to avoid unnecessarily extending the lifetime of the cluster.
  std::weak_ptr<DynamicModuleCluster> cluster_;
};

/**
 * Async host selection handle that bridges the dynamic module's async host selection to Envoy's
 * LoadBalancerContext::onAsyncHostSelection. This is created when the module returns an async
 * pending result from choose_host, and destroyed after the module delivers the result or the
 * selection is canceled.
 */
class DynamicModuleAsyncHostSelectionHandle : public Upstream::AsyncHostSelectionHandle {
public:
  DynamicModuleAsyncHostSelectionHandle(
      envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr async_handle,
      envoy_dynamic_module_type_cluster_lb_module_ptr in_module_lb,
      OnClusterLbCancelHostSelectionType cancel_fn, std::shared_ptr<std::atomic<bool>> cancelled)
      : async_handle_(async_handle), in_module_lb_(in_module_lb), cancel_fn_(cancel_fn),
        cancelled_(std::move(cancelled)) {}

  ~DynamicModuleAsyncHostSelectionHandle() override;

  void cancel() override;

private:
  envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr async_handle_;
  envoy_dynamic_module_type_cluster_lb_module_ptr in_module_lb_{nullptr};
  OnClusterLbCancelHostSelectionType cancel_fn_;
  std::shared_ptr<std::atomic<bool>> cancelled_;
};

/**
 * Load balancer that delegates to the dynamic module.
 */
class DynamicModuleLoadBalancer : public Upstream::LoadBalancer {
public:
  // ``priority_set`` must be the worker local priority set supplied via
  // ``LoadBalancerParams::priority_set``. The membership update subscription is registered on it so
  // that the callback list is mutated only on the thread that owns this load balancer instance.
  DynamicModuleLoadBalancer(const DynamicModuleClusterHandleSharedPtr& handle,
                            const Upstream::PrioritySet& priority_set);
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

  // Access the handle for async host selection completion.
  const DynamicModuleClusterHandleSharedPtr& handle() const { return handle_; }

  /**
   * Returns the shared cancellation flag for the current async host selection. When the router
   * cancels the selection (e.g., stream timeout), the flag is set so the posted completion
   * callback becomes a no-op. Returns nullptr when there is no active async selection.
   */
  std::shared_ptr<std::atomic<bool>> activeAsyncCancelled() const {
    return active_async_cancelled_;
  }

  /**
   * Returns the worker thread's dispatcher captured during chooseHost. Used by the async
   * completion callback in abi_impl.cc to post to the correct worker thread without accessing
   * the LoadBalancerContext from a background thread.
   */
  Event::Dispatcher* activeAsyncDispatcher() const { return active_async_dispatcher_; }

  // Per-host custom data storage.
  bool setHostData(uint32_t priority, size_t index, uintptr_t data);
  bool getHostData(uint32_t priority, size_t index, uintptr_t* data) const;

  // Accessors for hosts added/removed during the on_host_membership_update callback.
  const Upstream::HostVector* hostsAdded() const { return hosts_added_; }
  const Upstream::HostVector* hostsRemoved() const { return hosts_removed_; }

  // Returns the priority set that this load balancer subscribes to for host membership updates.
  const Upstream::PrioritySet& memberUpdatePrioritySet() const { return priority_set_; }

  /**
   * Looks up `lb` in the process-wide registry of live instances. Returns true and invokes `f`
   * with the instance under the registry lock when found, false otherwise. Used by the async
   * host selection completion ABI callback, which receives a raw pointer from the module that
   * may outlive the load balancer.
   */
  static bool withActiveInstance(const DynamicModuleLoadBalancer* lb,
                                 absl::FunctionRef<void(const DynamicModuleLoadBalancer&)> f);

private:
  const DynamicModuleClusterHandleSharedPtr handle_;
  // Worker local priority set that backs the membership update subscription.
  const Upstream::PrioritySet& priority_set_;
  envoy_dynamic_module_type_cluster_lb_module_ptr in_module_lb_;

  // Shared cancellation flag for the active async host selection. Set in chooseHost when the
  // module returns AsyncPending, and read by the posted completion callback in abi_impl.cc.
  std::shared_ptr<std::atomic<bool>> active_async_cancelled_;

  // Worker thread dispatcher captured during chooseHost for async completion posting.
  Event::Dispatcher* active_async_dispatcher_{nullptr};

  // Per-host data storage keyed by (priority, index). This is per-LB-instance (per-worker).
  absl::flat_hash_map<std::pair<uint32_t, size_t>, uintptr_t> per_host_data_;

  // Temporary pointers to host vectors, valid only during on_host_membership_update callback.
  const Upstream::HostVector* hosts_added_{};
  const Upstream::HostVector* hosts_removed_{};

  // Membership update callback handle.
  Envoy::Common::CallbackHandlePtr member_update_cb_;
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
