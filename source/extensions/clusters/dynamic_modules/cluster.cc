#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"
#include "envoy/network/connection.h"
#include "envoy/network/drain_decision.h"
#include "envoy/upstream/locality.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/thread.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/upstream/upstream_impl.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

namespace {

/**
 * Thread-aware load balancer that creates DynamicModuleLoadBalancer instances per worker.
 */
struct DynamicModuleThreadAwareLoadBalancer : public Upstream::ThreadAwareLoadBalancer {
  DynamicModuleThreadAwareLoadBalancer(DynamicModuleClusterHandleSharedPtr handle)
      : handle_(std::move(handle)) {}

  struct LoadBalancerFactory : public Upstream::LoadBalancerFactory {
    LoadBalancerFactory(DynamicModuleClusterHandleSharedPtr handle) : handle_(std::move(handle)) {}

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams params) override {
      return std::make_unique<DynamicModuleLoadBalancer>(handle_, params.priority_set);
    }

    DynamicModuleClusterHandleSharedPtr handle_;
  };

  Upstream::LoadBalancerFactorySharedPtr factory() override {
    return std::make_shared<LoadBalancerFactory>(handle_);
  }

  absl::Status initialize() override { return absl::OkStatus(); }

  DynamicModuleClusterHandleSharedPtr handle_;
};

} // namespace

// =================================================================================================
// DynamicModuleClusterConfig
// =================================================================================================

absl::StatusOr<std::shared_ptr<DynamicModuleClusterConfig>> DynamicModuleClusterConfig::create(
    const std::string& cluster_name, const std::string& cluster_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module, Stats::Scope& stats_scope) {
  auto config = std::shared_ptr<DynamicModuleClusterConfig>(
      new DynamicModuleClusterConfig(cluster_name, cluster_config, std::move(module), stats_scope));

  // Resolve all required function pointers from the dynamic module.
#define RESOLVE_SYMBOL(name, type, member)                                                         \
  {                                                                                                \
    auto symbol_or_error = config->dynamic_module_->getFunctionPointer<type>(name);                \
    if (!symbol_or_error.ok()) {                                                                   \
      return symbol_or_error.status();                                                             \
    }                                                                                              \
    config->member = symbol_or_error.value();                                                      \
  }

  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_config_new", OnClusterConfigNewType,
                 on_cluster_config_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_config_destroy", OnClusterConfigDestroyType,
                 on_cluster_config_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_new", OnClusterNewType, on_cluster_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_init", OnClusterInitType, on_cluster_init_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_destroy", OnClusterDestroyType,
                 on_cluster_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_lb_new", OnClusterLbNewType, on_cluster_lb_new_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_lb_destroy", OnClusterLbDestroyType,
                 on_cluster_lb_destroy_);
  RESOLVE_SYMBOL("envoy_dynamic_module_on_cluster_lb_choose_host", OnClusterLbChooseHostType,
                 on_cluster_lb_choose_host_);

#undef RESOLVE_SYMBOL

  // Optional hooks. Modules that don't need async host selection or scheduling don't need to
  // implement these.
  auto on_cancel = config->dynamic_module_->getFunctionPointer<OnClusterLbCancelHostSelectionType>(
      "envoy_dynamic_module_on_cluster_lb_cancel_host_selection");
  config->on_cluster_lb_cancel_host_selection_ = on_cancel.ok() ? on_cancel.value() : nullptr;

  auto on_scheduled = config->dynamic_module_->getFunctionPointer<OnClusterScheduledType>(
      "envoy_dynamic_module_on_cluster_scheduled");
  config->on_cluster_scheduled_ = on_scheduled.ok() ? on_scheduled.value() : nullptr;

  // Lifecycle hooks are optional. Modules that don't need them don't need to implement them.
  auto on_server_initialized =
      config->dynamic_module_->getFunctionPointer<OnClusterServerInitializedType>(
          "envoy_dynamic_module_on_cluster_server_initialized");
  config->on_cluster_server_initialized_ =
      on_server_initialized.ok() ? on_server_initialized.value() : nullptr;

  auto on_drain_started = config->dynamic_module_->getFunctionPointer<OnClusterDrainStartedType>(
      "envoy_dynamic_module_on_cluster_drain_started");
  config->on_cluster_drain_started_ = on_drain_started.ok() ? on_drain_started.value() : nullptr;

  auto on_shutdown = config->dynamic_module_->getFunctionPointer<OnClusterShutdownType>(
      "envoy_dynamic_module_on_cluster_shutdown");
  config->on_cluster_shutdown_ = on_shutdown.ok() ? on_shutdown.value() : nullptr;

  auto on_http_callout_done =
      config->dynamic_module_->getFunctionPointer<OnClusterHttpCalloutDoneType>(
          "envoy_dynamic_module_on_cluster_http_callout_done");
  config->on_cluster_http_callout_done_ =
      on_http_callout_done.ok() ? on_http_callout_done.value() : nullptr;

  auto on_lb_membership_update =
      config->dynamic_module_->getFunctionPointer<OnClusterLbOnHostMembershipUpdateType>(
          "envoy_dynamic_module_on_cluster_lb_on_host_membership_update");
  config->on_cluster_lb_on_host_membership_update_ =
      on_lb_membership_update.ok() ? on_lb_membership_update.value() : nullptr;

  // Call on_cluster_config_new to get the in-module configuration.
  envoy_dynamic_module_type_envoy_buffer name_buffer = {config->cluster_name_.data(),
                                                        config->cluster_name_.size()};
  envoy_dynamic_module_type_envoy_buffer config_buffer = {config->cluster_config_.data(),
                                                          config->cluster_config_.size()};

  config->in_module_config_ =
      config->on_cluster_config_new_(config.get(), name_buffer, config_buffer);
  if (config->in_module_config_ == nullptr) {
    return absl::InvalidArgumentError("Failed to create in-module cluster configuration");
  }

  config->stat_creation_frozen_ = true;

  return config;
}

DynamicModuleClusterConfig::DynamicModuleClusterConfig(
    const std::string& cluster_name, const std::string& cluster_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module, Stats::Scope& stats_scope)
    : stats_scope_(stats_scope.createScope("dynamicmodulescustom.")),
      stat_name_pool_(stats_scope_->symbolTable()), cluster_name_(cluster_name),
      cluster_config_(cluster_config), dynamic_module_(std::move(module)) {}

DynamicModuleClusterConfig::~DynamicModuleClusterConfig() {
  if (in_module_config_ != nullptr && on_cluster_config_destroy_ != nullptr) {
    on_cluster_config_destroy_(in_module_config_);
  }
}

// =================================================================================================
// DynamicModuleClusterHandle
// =================================================================================================

DynamicModuleClusterHandle::~DynamicModuleClusterHandle() {
  std::shared_ptr<DynamicModuleCluster> cluster = std::move(cluster_);
  cluster_.reset();
  if (cluster == nullptr) {
    return;
  }
  Event::Dispatcher& dispatcher = cluster->dispatcher_;
  // The lifecycle handle resets unregister from main-thread-owned notifiers. When the handle is
  // destroyed on a worker thread, post the full teardown onto the main dispatcher.
  if (!Thread::MainThread::isMainThread() && !Thread::TestThread::isTestThread()) {
    dispatcher.post([cluster = std::move(cluster)]() mutable {
      cluster->server_initialized_handle_.reset();
      cluster->shutdown_handle_.reset();
      cluster->drain_handle_.reset();
      cluster.reset();
    });
    return;
  }
  cluster->server_initialized_handle_.reset();
  cluster->shutdown_handle_.reset();
  cluster->drain_handle_.reset();
  dispatcher.post([cluster = std::move(cluster)]() mutable { cluster.reset(); });
}

// =================================================================================================
// DynamicModuleCluster
// =================================================================================================

DynamicModuleCluster::DynamicModuleCluster(const envoy::config::cluster::v3::Cluster& cluster,
                                           DynamicModuleClusterConfigSharedPtr config,
                                           Upstream::ClusterFactoryContext& context,
                                           absl::Status& creation_status)
    : ClusterImplBase(cluster, context, creation_status), config_(std::move(config)),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()),
      server_context_(context.serverFactoryContext()) {

  // Create the in-module cluster instance.
  in_module_cluster_ = config_->on_cluster_new_(config_->in_module_config_, this);
  if (in_module_cluster_ == nullptr) {
    creation_status = absl::InvalidArgumentError("Failed to create in-module cluster instance");
    return;
  }

  // Initialize the priority set with an empty host set at priority 0.
  priority_set_.getOrCreateHostSet(0);

  registerLifecycleCallbacks();
}

DynamicModuleCluster::~DynamicModuleCluster() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  // Cancel any pending HTTP callouts before destroying the cluster.
  for (auto& callout : http_callouts_) {
    if (callout.second->request_ != nullptr) {
      callout.second->request_->cancel();
    }
  }
  http_callouts_.clear();

  if (in_module_cluster_ != nullptr && config_->on_cluster_destroy_ != nullptr) {
    config_->on_cluster_destroy_(in_module_cluster_);
  }
}

void DynamicModuleCluster::registerLifecycleCallbacks() {
  if (config_->on_cluster_server_initialized_ != nullptr) {
    server_initialized_handle_ = server_context_.lifecycleNotifier().registerCallback(
        Server::ServerLifecycleNotifier::Stage::PostInit, [this]() {
          if (in_module_cluster_ != nullptr) {
            ENVOY_LOG(debug, "dynamic module cluster server initialized");
            config_->on_cluster_server_initialized_(this, in_module_cluster_);
          }
        });
  }

  if (config_->on_cluster_drain_started_ != nullptr) {
    drain_handle_ = server_context_.drainManager().addOnDrainCloseCb(
        Network::DrainDirection::All, [this](std::chrono::milliseconds) -> absl::Status {
          if (in_module_cluster_ != nullptr) {
            ENVOY_LOG(debug, "dynamic module cluster drain started");
            config_->on_cluster_drain_started_(this, in_module_cluster_);
          }
          return absl::OkStatus();
        });
  }

  if (config_->on_cluster_shutdown_ != nullptr) {
    shutdown_handle_ = server_context_.lifecycleNotifier().registerCallback(
        Server::ServerLifecycleNotifier::Stage::ShutdownExit, [this](Event::PostCb completion_cb) {
          if (in_module_cluster_ != nullptr) {
            ENVOY_LOG(debug, "dynamic module cluster shutdown started");
            auto* completion = new Event::PostCb(std::move(completion_cb));
            config_->on_cluster_shutdown_(
                this, in_module_cluster_,
                [](void* context) {
                  auto* cb = static_cast<Event::PostCb*>(context);
                  (*cb)();
                  delete cb;
                },
                static_cast<void*>(completion));
          } else {
            completion_cb();
          }
        });
  }
}

void DynamicModuleCluster::startPreInit() {
  // Call the module's init function. The module is expected to call
  // envoy_dynamic_module_callback_cluster_pre_init_complete when ready.
  config_->on_cluster_init_(this, in_module_cluster_);
}

void DynamicModuleCluster::preInitComplete() { onPreInitComplete(); }

void DynamicModuleCluster::onScheduled(uint64_t event_id) {
  if (in_module_cluster_ != nullptr && config_->on_cluster_scheduled_ != nullptr) {
    config_->on_cluster_scheduled_(this, in_module_cluster_, event_id);
  }
}

namespace {
// Builds hosts-per-locality from a host vector using value-based locality comparison.
Upstream::HostsPerLocalityConstSharedPtr buildHostsPerLocality(const Upstream::HostVector& hosts) {
  absl::node_hash_map<envoy::config::core::v3::Locality, Upstream::HostVector,
                      Upstream::LocalityHash, Upstream::LocalityEqualTo>
      per_locality_hosts;
  for (const auto& host : hosts) {
    per_locality_hosts[host->locality()].push_back(host);
  }
  std::vector<Upstream::HostVector> locality_hosts;
  for (auto& [_, h] : per_locality_hosts) {
    locality_hosts.push_back(std::move(h));
  }
  return std::make_shared<Upstream::HostsPerLocalityImpl>(std::move(locality_hosts), false);
}
} // namespace

bool DynamicModuleCluster::addHosts(
    const std::vector<std::string>& addresses, const std::vector<uint32_t>& weights,
    const std::vector<std::string>& regions, const std::vector<std::string>& zones,
    const std::vector<std::string>& sub_zones,
    const std::vector<std::vector<std::tuple<std::string, std::string, std::string>>>& metadata,
    std::vector<Upstream::HostSharedPtr>& result_hosts, uint32_t priority) {
  ASSERT(addresses.size() == weights.size());
  ASSERT(addresses.size() == regions.size());
  ASSERT(addresses.size() == zones.size());
  ASSERT(addresses.size() == sub_zones.size());
  ASSERT(metadata.empty() || metadata.size() == addresses.size());
  result_hosts.clear();
  result_hosts.reserve(addresses.size());

  auto cluster_info = info();

  for (size_t i = 0; i < addresses.size(); ++i) {
    if (weights[i] == 0 || weights[i] > 128) {
      ENVOY_LOG(error, "Invalid weight {} for host {}.", weights[i], addresses[i]);
      return false;
    }

    Network::Address::InstanceConstSharedPtr resolved_address =
        Network::Utility::parseInternetAddressAndPortNoThrow(addresses[i], false);
    if (resolved_address == nullptr) {
      ENVOY_LOG(error, "Invalid address: {}.", addresses[i]);
      return false;
    }

    auto locality = std::make_shared<envoy::config::core::v3::Locality>();
    if (!regions[i].empty()) {
      locality->set_region(regions[i]);
    }
    if (!zones[i].empty()) {
      locality->set_zone(zones[i]);
    }
    if (!sub_zones[i].empty()) {
      locality->set_sub_zone(sub_zones[i]);
    }

    // Build endpoint metadata if provided.
    Upstream::MetadataConstSharedPtr endpoint_metadata = nullptr;
    if (!metadata.empty() && !metadata[i].empty()) {
      auto md = std::make_shared<envoy::config::core::v3::Metadata>();
      for (const auto& [filter_name, key, value] : metadata[i]) {
        auto& fields = (*md->mutable_filter_metadata())[filter_name];
        (*fields.mutable_fields())[key].set_string_value(value);
      }
      endpoint_metadata = std::move(md);
    }

    auto host_result = Upstream::HostImpl::create(
        cluster_info, cluster_info->name() + addresses[i], std::move(resolved_address),
        std::move(endpoint_metadata), nullptr, weights[i], std::move(locality),
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
        envoy::config::core::v3::UNKNOWN);
    if (!host_result.ok()) {
      ENVOY_LOG(error, "Failed to create host for address: {}.", addresses[i]); // LCOV_EXCL_LINE
      return false;                                                             // LCOV_EXCL_LINE
    }
    result_hosts.emplace_back(std::move(host_result.value()));
  }

  {
    absl::WriterMutexLock lock(host_map_lock_);
    for (const auto& host : result_hosts) {
      host_map_[host.get()] = host;
    }
  }

  const auto& host_set = priority_set_.getOrCreateHostSet(priority);
  Upstream::HostVectorSharedPtr all_hosts(new Upstream::HostVector(host_set.hosts()));
  Upstream::HostVector added_hosts;
  for (const auto& host : result_hosts) {
    all_hosts->emplace_back(host);
    added_hosts.emplace_back(host);
  }

  auto hosts_per_locality = buildHostsPerLocality(*all_hosts);

  priority_set_.updateHosts(
      priority, Upstream::HostSetImpl::partitionHosts(all_hosts, std::move(hosts_per_locality)), {},
      added_hosts, {}, absl::nullopt, absl::nullopt);

  ENVOY_LOG(debug, "Added {} hosts to dynamic module cluster at priority {}.", result_hosts.size(),
            priority);
  return true;
}

bool DynamicModuleCluster::updateHostHealth(Upstream::HostSharedPtr host,
                                            envoy_dynamic_module_type_host_health health_status) {
  if (host == nullptr) {
    return false;
  }

  // Clear existing EDS health flags and set the new status.
  host->healthFlagClear(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH);
  host->healthFlagClear(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);

  switch (health_status) {
  case envoy_dynamic_module_type_host_health_Unhealthy:
    host->healthFlagSet(Upstream::Host::HealthFlag::FAILED_EDS_HEALTH);
    break;
  case envoy_dynamic_module_type_host_health_Degraded:
    host->healthFlagSet(Upstream::Host::HealthFlag::DEGRADED_EDS_HEALTH);
    break;
  case envoy_dynamic_module_type_host_health_Healthy:
    break;
  }

  // Find the priority level that contains this host and trigger a priority set update to
  // propagate the health change to load balancers.
  const auto& host_sets = priority_set_.hostSetsPerPriority();
  for (uint32_t p = 0; p < host_sets.size(); ++p) {
    const auto& hosts = host_sets[p]->hosts();
    for (const auto& h : hosts) {
      if (h.get() == host.get()) {
        auto all_hosts = std::make_shared<Upstream::HostVector>(hosts);
        auto hosts_per_locality = buildHostsPerLocality(*all_hosts);
        priority_set_.updateHosts(
            p, Upstream::HostSetImpl::partitionHosts(all_hosts, std::move(hosts_per_locality)), {},
            {}, {}, absl::nullopt, absl::nullopt);
        ENVOY_LOG(debug, "Updated health status for host to {} at priority {}.",
                  static_cast<int>(health_status), p);
        return true;
      }
    }
  }

  ENVOY_LOG(error, "Host not found in any priority level during health update.");
  return false;
}

Upstream::HostSharedPtr DynamicModuleCluster::findHostByAddress(const std::string& address) {
  const auto host_map = prioritySet().crossPriorityHostMap();
  if (host_map == nullptr) {
    return nullptr;
  }
  const auto it = host_map->find(address);
  if (it == host_map->end()) {
    return nullptr;
  }
  return it->second;
}

Upstream::HostSharedPtr DynamicModuleCluster::findHost(void* raw_host_ptr) {
  absl::ReaderMutexLock lock(host_map_lock_);
  auto it = host_map_.find(raw_host_ptr);
  if (it == host_map_.end()) {
    return nullptr;
  }
  return it->second;
}

size_t DynamicModuleCluster::removeHosts(const std::vector<Upstream::HostSharedPtr>& hosts) {
  Upstream::HostVector removed_hosts;
  removed_hosts.reserve(hosts.size());

  // Remove all valid hosts from the map.
  {
    absl::WriterMutexLock lock(host_map_lock_);
    for (const auto& host : hosts) {
      if (host == nullptr) {
        continue;
      }
      auto it = host_map_.find(host.get());
      if (it != host_map_.end()) {
        removed_hosts.emplace_back(host);
        host_map_.erase(it);
      }
    }
  }

  if (removed_hosts.empty()) {
    return 0;
  }

  // Build the remaining host list and update the priority set once.
  ASSERT(!priority_set_.hostSetsPerPriority().empty());
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);

  // Build a set of removed host pointers for O(1) lookup.
  absl::flat_hash_set<Upstream::Host*> removed_set;
  removed_set.reserve(removed_hosts.size());
  for (const auto& h : removed_hosts) {
    removed_set.insert(h.get());
  }

  Upstream::HostVectorSharedPtr remaining_hosts(new Upstream::HostVector());
  for (const auto& h : first_host_set.hosts()) {
    if (removed_set.find(h.get()) == removed_set.end()) {
      remaining_hosts->emplace_back(h);
    }
  }

  auto hosts_per_locality = buildHostsPerLocality(*remaining_hosts);

  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(remaining_hosts, std::move(hosts_per_locality)), {},
      {}, removed_hosts, absl::nullopt, absl::nullopt);

  ENVOY_LOG(debug, "Removed {} hosts from dynamic module cluster.", removed_hosts.size());
  return removed_hosts.size();
}

envoy_dynamic_module_type_http_callout_init_result
DynamicModuleCluster::sendHttpCallout(uint64_t* callout_id_out, absl::string_view cluster_name,
                                      Http::RequestMessagePtr&& message,
                                      uint64_t timeout_milliseconds) {
  if (config_->on_cluster_http_callout_done_ == nullptr) {
    ENVOY_LOG(debug, "dynamic module cluster: HTTP callout requested but "
                     "on_cluster_http_callout_done is not implemented.");
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  Upstream::ThreadLocalCluster* cluster =
      server_context_.clusterManager().getThreadLocalCluster(cluster_name);
  if (!cluster) {
    return envoy_dynamic_module_type_http_callout_init_result_ClusterNotFound;
  }
  Http::AsyncClient::RequestOptions options;
  options.setTimeout(std::chrono::milliseconds(timeout_milliseconds));

  const uint64_t callout_id = getNextCalloutId();
  auto http_callout_callback =
      std::make_unique<DynamicModuleCluster::HttpCalloutCallback>(shared_from_this(), callout_id);
  DynamicModuleCluster::HttpCalloutCallback& callback = *http_callout_callback;

  auto request = cluster->httpAsyncClient().send(std::move(message), callback, options);
  if (!request) {
    return envoy_dynamic_module_type_http_callout_init_result_CannotCreateRequest;
  }

  callback.request_ = request;
  http_callouts_.emplace(callout_id, std::move(http_callout_callback));
  *callout_id_out = callout_id;

  return envoy_dynamic_module_type_http_callout_init_result_Success;
}

void DynamicModuleCluster::HttpCalloutCallback::onSuccess(const Http::AsyncClient::Request&,
                                                          Http::ResponseMessagePtr&& response) {
  // Move the cluster and callout id to local scope since on_cluster_http_callout_done_ might
  // result in operations that affect this callback's lifetime.
  std::shared_ptr<DynamicModuleCluster> cluster = std::move(cluster_);
  uint64_t callout_id = callout_id_;

  if (!cluster->in_module_cluster_) {
    cluster->http_callouts_.erase(callout_id);
    return;
  }

  absl::InlinedVector<envoy_dynamic_module_type_envoy_http_header, 16> headers_vector;
  headers_vector.reserve(response->headers().size());
  response->headers().iterate([&headers_vector](
                                  const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    headers_vector.emplace_back(envoy_dynamic_module_type_envoy_http_header{
        const_cast<char*>(header.key().getStringView().data()), header.key().getStringView().size(),
        const_cast<char*>(header.value().getStringView().data()),
        header.value().getStringView().size()});
    return Http::HeaderMap::Iterate::Continue;
  });

  Envoy::Buffer::RawSliceVector body = response->body().getRawSlices(std::nullopt);
  cluster->config_->on_cluster_http_callout_done_(
      cluster.get(), cluster->in_module_cluster_, callout_id,
      envoy_dynamic_module_type_http_callout_result_Success, headers_vector.data(),
      headers_vector.size(), reinterpret_cast<envoy_dynamic_module_type_envoy_buffer*>(body.data()),
      body.size());
  cluster->http_callouts_.erase(callout_id);
}

void DynamicModuleCluster::HttpCalloutCallback::onFailure(const Http::AsyncClient::Request&,
                                                          Http::AsyncClient::FailureReason reason) {
  // Move the cluster and callout id to local scope since on_cluster_http_callout_done_ might
  // result in operations that affect this callback's lifetime.
  std::shared_ptr<DynamicModuleCluster> cluster = std::move(cluster_);
  const uint64_t callout_id = callout_id_;

  if (!cluster->in_module_cluster_) {
    cluster->http_callouts_.erase(callout_id);
    return;
  }

  // request_ is not null if the callout is actually sent to the upstream cluster.
  // This allows us to avoid inlined calls to onFailure() method (which results in a reentrant to
  // the modules) when the async client immediately fails the callout.
  if (request_) {
    envoy_dynamic_module_type_http_callout_result result;
    switch (reason) {
    case Http::AsyncClient::FailureReason::Reset:
      result = envoy_dynamic_module_type_http_callout_result_Reset;
      break;
    case Http::AsyncClient::FailureReason::ExceedResponseBufferLimit:
      result = envoy_dynamic_module_type_http_callout_result_ExceedResponseBufferLimit;
      break;
    }
    cluster->config_->on_cluster_http_callout_done_(cluster.get(), cluster->in_module_cluster_,
                                                    callout_id, result, nullptr, 0, nullptr, 0);
  }

  cluster->http_callouts_.erase(callout_id);
}

// =================================================================================================
// DynamicModuleLoadBalancer
// =================================================================================================

namespace {
// Process-wide registry of live `DynamicModuleLoadBalancer` instances, consulted by the async
// host selection completion ABI callback to validate the raw pointer it receives from the module.
absl::Mutex& activeDynamicModuleLoadBalancersMutex() {
  static auto* m = new absl::Mutex();
  return *m;
}

absl::flat_hash_set<const DynamicModuleLoadBalancer*>& activeDynamicModuleLoadBalancers()
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(activeDynamicModuleLoadBalancersMutex()) {
  static auto* s = new absl::flat_hash_set<const DynamicModuleLoadBalancer*>();
  return *s;
}

void registerActiveDynamicModuleLoadBalancer(const DynamicModuleLoadBalancer* lb) {
  absl::MutexLock lock(activeDynamicModuleLoadBalancersMutex());
  activeDynamicModuleLoadBalancers().insert(lb);
}

void unregisterActiveDynamicModuleLoadBalancer(const DynamicModuleLoadBalancer* lb) {
  absl::MutexLock lock(activeDynamicModuleLoadBalancersMutex());
  activeDynamicModuleLoadBalancers().erase(lb);
}
} // namespace

bool DynamicModuleLoadBalancer::withActiveInstance(
    const DynamicModuleLoadBalancer* lb,
    absl::FunctionRef<void(const DynamicModuleLoadBalancer&)> f) {
  absl::MutexLock lock(activeDynamicModuleLoadBalancersMutex());
  if (!activeDynamicModuleLoadBalancers().contains(lb)) {
    return false;
  }
  f(*lb);
  return true;
}

DynamicModuleLoadBalancer::DynamicModuleLoadBalancer(
    const DynamicModuleClusterHandleSharedPtr& handle, const Upstream::PrioritySet& priority_set)
    : handle_(handle), priority_set_(priority_set) {
  // Register before invoking any module hook so a concurrent async host selection completion can
  // validate its raw pointer against a live instance.
  registerActiveDynamicModuleLoadBalancer(this);
  in_module_lb_ =
      handle_->cluster_->config()->on_cluster_lb_new_(handle_->cluster_->inModuleCluster(), this);

  // Register for host membership updates if the module implements the hook. Subscribe on the
  // worker local priority set so the callback list is only mutated on this worker thread.
  if (handle_->cluster_->config()->on_cluster_lb_on_host_membership_update_ != nullptr) {
    member_update_cb_ = priority_set_.addMemberUpdateCb(
        [this](const Upstream::HostVector& hosts_added, const Upstream::HostVector& hosts_removed) {
          hosts_added_ = &hosts_added;
          hosts_removed_ = &hosts_removed;
          handle_->cluster_->config()->on_cluster_lb_on_host_membership_update_(
              this, in_module_lb_, hosts_added.size(), hosts_removed.size());
          hosts_added_ = nullptr;
          hosts_removed_ = nullptr;
        });
  }
}

DynamicModuleLoadBalancer::~DynamicModuleLoadBalancer() {
  // Unregister before tearing down module-side state so any concurrent async host selection
  // completion observes the instance as gone and drops the event.
  unregisterActiveDynamicModuleLoadBalancer(this);
  if (in_module_lb_ != nullptr && handle_->cluster_->config()->on_cluster_lb_destroy_ != nullptr) {
    handle_->cluster_->config()->on_cluster_lb_destroy_(in_module_lb_);
  }
}

Upstream::HostSelectionResponse
DynamicModuleLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (in_module_lb_ == nullptr) {
    return {nullptr};
  }

  // Pre-capture the worker dispatcher and prepare the cancellation flag before calling into the
  // module. The module's choose_host may spawn a background thread that calls
  // async_host_selection_complete, which reads these fields. Setting them beforehand establishes
  // a happens-before relationship via the thread::spawn synchronization in the module.
  const auto* connection = context != nullptr ? context->downstreamConnection() : nullptr;
  active_async_dispatcher_ = connection != nullptr ? &connection->dispatcher() : nullptr;
  active_async_cancelled_ = std::make_shared<std::atomic<bool>>(false);

  envoy_dynamic_module_type_cluster_host_envoy_ptr host_ptr = nullptr;
  envoy_dynamic_module_type_cluster_lb_async_handle_module_ptr async_handle = nullptr;
  handle_->cluster_->config()->on_cluster_lb_choose_host_(in_module_lb_, context, &host_ptr,
                                                          &async_handle);

  if (async_handle != nullptr) {
    // Async pending: the module will call the completion callback later.
    auto cancelable = std::make_unique<DynamicModuleAsyncHostSelectionHandle>(
        async_handle, in_module_lb_,
        handle_->cluster_->config()->on_cluster_lb_cancel_host_selection_, active_async_cancelled_);
    return Upstream::HostSelectionResponse{nullptr, std::move(cancelable)};
  }

  // Synchronous result or no host. Clear the async state.
  active_async_dispatcher_ = nullptr;
  active_async_cancelled_ = nullptr;

  if (host_ptr == nullptr) {
    return {nullptr};
  }

  // Look up the host shared pointer from the raw pointer.
  auto host = handle_->cluster_->findHost(host_ptr);
  return {host};
}

DynamicModuleAsyncHostSelectionHandle::~DynamicModuleAsyncHostSelectionHandle() {
  // Free the module-side async handle. The cancel function takes ownership of the handle and
  // drops it, so this works for both cancellation and normal completion paths.
  if (async_handle_ != nullptr && cancel_fn_ != nullptr) {
    cancel_fn_(in_module_lb_, async_handle_);
    async_handle_ = nullptr;
  }
}

void DynamicModuleAsyncHostSelectionHandle::cancel() {
  cancelled_->store(true, std::memory_order_release);
}

const Upstream::PrioritySet& DynamicModuleLoadBalancer::prioritySet() const {
  return handle_->cluster_->prioritySet();
}

bool DynamicModuleLoadBalancer::setHostData(uint32_t priority, size_t index, uintptr_t data) {
  const auto& host_sets = prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return false;
  }
  if (data == 0) {
    per_host_data_.erase({priority, index});
  } else {
    per_host_data_[{priority, index}] = data;
  }
  return true;
}

bool DynamicModuleLoadBalancer::getHostData(uint32_t priority, size_t index,
                                            uintptr_t* data) const {
  const auto& host_sets = prioritySet().hostSetsPerPriority();
  if (priority >= host_sets.size()) {
    return false;
  }
  const auto& hosts = host_sets[priority]->hosts();
  if (index >= hosts.size()) {
    return false;
  }
  auto it = per_host_data_.find({priority, index});
  if (it != per_host_data_.end()) {
    *data = it->second;
  } else {
    *data = 0;
  }
  return true;
}

// =================================================================================================
// DynamicModuleClusterFactory
// =================================================================================================

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
DynamicModuleClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {

  // Validate that CLUSTER_PROVIDED LB policy is used.
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return absl::InvalidArgumentError(
        fmt::format("cluster: LB policy {} is not valid for cluster type "
                    "'envoy.clusters.dynamic_modules'. Only 'CLUSTER_PROVIDED' is allowed.",
                    envoy::config::cluster::v3::Cluster::LbPolicy_Name(cluster.lb_policy())));
  }

  // Extract cluster_config from the Any field.
  std::string cluster_config_bytes;
  if (proto_config.has_cluster_config()) {
    auto config_or_error = MessageUtil::knownAnyToBytes(proto_config.cluster_config());
    RETURN_IF_NOT_OK_REF(config_or_error.status());
    cluster_config_bytes = std::move(config_or_error.value());
  }

  // Load the dynamic module.
  const auto& module_config = proto_config.dynamic_module_config();
  auto module_or_error = Envoy::Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  if (!module_or_error.ok()) {
    return absl::InvalidArgumentError(fmt::format("Failed to load dynamic module '{}': {}",
                                                  module_config.name(),
                                                  module_or_error.status().message()));
  }

  // Create the cluster configuration.
  auto config_or_error = DynamicModuleClusterConfig::create(
      proto_config.cluster_name(), cluster_config_bytes, std::move(module_or_error.value()),
      context.serverFactoryContext().serverScope());
  if (!config_or_error.ok()) {
    return config_or_error.status();
  }

  // Create the cluster.
  absl::Status creation_status = absl::OkStatus();
  auto new_cluster = std::shared_ptr<DynamicModuleCluster>(new DynamicModuleCluster(
      cluster, std::move(config_or_error.value()), context, creation_status));
  RETURN_IF_NOT_OK(creation_status);

  // Create the thread-aware load balancer.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(new_cluster);
  auto lb = std::make_unique<DynamicModuleThreadAwareLoadBalancer>(handle);

  return std::make_pair(std::move(new_cluster), std::move(lb));
}

REGISTER_FACTORY(DynamicModuleClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
