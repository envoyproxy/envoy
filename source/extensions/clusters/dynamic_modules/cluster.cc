#include "source/extensions/clusters/dynamic_modules/cluster.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/config/endpoint/v3/endpoint_components.pb.h"

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

    Upstream::LoadBalancerPtr create(Upstream::LoadBalancerParams) override {
      return std::make_unique<DynamicModuleLoadBalancer>(handle_);
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

absl::StatusOr<std::shared_ptr<DynamicModuleClusterConfig>>
DynamicModuleClusterConfig::create(const std::string& cluster_name,
                                   const std::string& cluster_config,
                                   Envoy::Extensions::DynamicModules::DynamicModulePtr module) {
  auto config = std::shared_ptr<DynamicModuleClusterConfig>(
      new DynamicModuleClusterConfig(cluster_name, cluster_config, std::move(module)));

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

  return config;
}

DynamicModuleClusterConfig::DynamicModuleClusterConfig(
    const std::string& cluster_name, const std::string& cluster_config,
    Envoy::Extensions::DynamicModules::DynamicModulePtr module)
    : cluster_name_(cluster_name), cluster_config_(cluster_config),
      dynamic_module_(std::move(module)) {}

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
  Event::Dispatcher& dispatcher = cluster->dispatcher_;
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
      in_module_cluster_(nullptr),
      dispatcher_(context.serverFactoryContext().mainThreadDispatcher()) {

  // Create the in-module cluster instance.
  in_module_cluster_ = config_->on_cluster_new_(config_->in_module_config_, this);
  if (in_module_cluster_ == nullptr) {
    creation_status = absl::InvalidArgumentError("Failed to create in-module cluster instance");
    return;
  }

  // Initialize the priority set with an empty host set at priority 0.
  priority_set_.getOrCreateHostSet(0);
}

DynamicModuleCluster::~DynamicModuleCluster() {
  ASSERT_IS_MAIN_OR_TEST_THREAD();
  if (in_module_cluster_ != nullptr && config_->on_cluster_destroy_ != nullptr) {
    config_->on_cluster_destroy_(in_module_cluster_);
  }
}

void DynamicModuleCluster::startPreInit() {
  // Call the module's init function. The module is expected to call
  // envoy_dynamic_module_callback_cluster_pre_init_complete when ready.
  config_->on_cluster_init_(in_module_cluster_, this);
}

void DynamicModuleCluster::preInitComplete() { onPreInitComplete(); }

bool DynamicModuleCluster::addHosts(const std::vector<std::string>& addresses,
                                    const std::vector<uint32_t>& weights,
                                    std::vector<Upstream::HostSharedPtr>& result_hosts) {
  ASSERT(addresses.size() == weights.size());
  result_hosts.clear();
  result_hosts.reserve(addresses.size());

  auto cluster_info = info();

  // First pass: validate all hosts and create them. If any fail, return false without modifying
  // the cluster state.
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

    auto host_result = Upstream::HostImpl::create(
        cluster_info, cluster_info->name() + addresses[i], std::move(resolved_address), nullptr,
        nullptr, weights[i], std::make_shared<envoy::config::core::v3::Locality>(),
        envoy::config::endpoint::v3::Endpoint::HealthCheckConfig().default_instance(), 0,
        envoy::config::core::v3::UNKNOWN);
    if (!host_result.ok()) {
      ENVOY_LOG(error, "Failed to create host for address: {}.", addresses[i]); // LCOV_EXCL_LINE
      return false;                                                             // LCOV_EXCL_LINE
    }
    result_hosts.emplace_back(std::move(host_result.value()));
  }

  // Second pass: store all host mappings and update the priority set once.
  {
    absl::WriterMutexLock lock(&host_map_lock_);
    for (const auto& host : result_hosts) {
      host_map_[host.get()] = host;
    }
  }

  ASSERT(priority_set_.hostSetsPerPriority().size() >= 1);
  const auto& first_host_set = priority_set_.getOrCreateHostSet(0);
  Upstream::HostVectorSharedPtr all_hosts(new Upstream::HostVector(first_host_set.hosts()));
  Upstream::HostVector added_hosts;
  for (const auto& host : result_hosts) {
    all_hosts->emplace_back(host);
    added_hosts.emplace_back(host);
  }
  priority_set_.updateHosts(
      0, Upstream::HostSetImpl::partitionHosts(all_hosts, Upstream::HostsPerLocalityImpl::empty()),
      {}, added_hosts, {}, absl::nullopt, absl::nullopt);

  ENVOY_LOG(debug, "Added {} hosts to dynamic module cluster.", result_hosts.size());
  return true;
}

Upstream::HostSharedPtr DynamicModuleCluster::findHost(void* raw_host_ptr) {
  absl::ReaderMutexLock lock(&host_map_lock_);
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
    absl::WriterMutexLock lock(&host_map_lock_);
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
  ASSERT(priority_set_.hostSetsPerPriority().size() >= 1);
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
  priority_set_.updateHosts(0,
                            Upstream::HostSetImpl::partitionHosts(
                                remaining_hosts, Upstream::HostsPerLocalityImpl::empty()),
                            {}, {}, removed_hosts, absl::nullopt, absl::nullopt);

  ENVOY_LOG(debug, "Removed {} hosts from dynamic module cluster.", removed_hosts.size());
  return removed_hosts.size();
}

// =================================================================================================
// DynamicModuleLoadBalancer
// =================================================================================================

DynamicModuleLoadBalancer::DynamicModuleLoadBalancer(
    const DynamicModuleClusterHandleSharedPtr& handle)
    : handle_(handle), in_module_lb_(nullptr) {
  in_module_lb_ =
      handle_->cluster_->config()->on_cluster_lb_new_(handle_->cluster_->inModuleCluster(), this);
}

DynamicModuleLoadBalancer::~DynamicModuleLoadBalancer() {
  if (in_module_lb_ != nullptr && handle_->cluster_->config()->on_cluster_lb_destroy_ != nullptr) {
    handle_->cluster_->config()->on_cluster_lb_destroy_(in_module_lb_);
  }
}

Upstream::HostSelectionResponse
DynamicModuleLoadBalancer::chooseHost(Upstream::LoadBalancerContext* context) {
  if (in_module_lb_ == nullptr) {
    return {nullptr};
  }

  auto* host_ptr = handle_->cluster_->config()->on_cluster_lb_choose_host_(in_module_lb_, context);
  if (host_ptr == nullptr) {
    return {nullptr};
  }

  // Look up the host shared pointer from the raw pointer.
  auto host = handle_->cluster_->findHost(host_ptr);
  return {host};
}

const Upstream::PrioritySet& DynamicModuleLoadBalancer::prioritySet() const {
  return handle_->cluster_->prioritySet();
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
    const auto& any_config = proto_config.cluster_config();
    const std::string& type_url = any_config.type_url();

    // Handle well-known types that can be passed directly as bytes.
    if (type_url == "type.googleapis.com/google.protobuf.StringValue") {
      Protobuf::StringValue string_value;
      if (!any_config.UnpackTo(&string_value)) {
        return absl::InvalidArgumentError("Failed to unpack StringValue");
      }
      cluster_config_bytes = string_value.value();
    } else if (type_url == "type.googleapis.com/google.protobuf.BytesValue") {
      Protobuf::BytesValue bytes_value;
      if (!any_config.UnpackTo(&bytes_value)) {
        return absl::InvalidArgumentError("Failed to unpack BytesValue");
      }
      cluster_config_bytes = bytes_value.value();
    } else if (type_url == "type.googleapis.com/google.protobuf.Struct") {
      Protobuf::Struct struct_value;
      if (!any_config.UnpackTo(&struct_value)) {
        return absl::InvalidArgumentError("Failed to unpack Struct");
      }
      cluster_config_bytes = MessageUtil::getJsonStringFromMessageOrError(struct_value, false);
    } else {
      // For unknown types, use the serialized bytes.
      cluster_config_bytes = any_config.value();
    }
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
      proto_config.cluster_name(), cluster_config_bytes, std::move(module_or_error.value()));
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
