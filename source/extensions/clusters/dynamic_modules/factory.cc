#include "source/extensions/clusters/dynamic_modules/factory.h"

#include "envoy/extensions/clusters/dynamic_modules/v3/cluster.pb.h"

#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

namespace {

/**
 * Thread-aware load balancer that creates DynamicModuleLoadBalancer instances.
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

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
DynamicModuleClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {

  // Validate that CLUSTER_PROVIDED LB policy is used.
  if (cluster.lb_policy() != envoy::config::cluster::v3::Cluster::CLUSTER_PROVIDED) {
    return absl::InvalidArgumentError(
        fmt::format("cluster: LB policy {} is not valid for cluster type DYNAMIC_MODULES. Only "
                    "'CLUSTER_PROVIDED' is allowed.",
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
  auto config_or_error = DynamicModuleClusterConfig::newDynamicModuleClusterConfig(
      proto_config.cluster_name(), cluster_config_bytes, std::move(module_or_error.value()));
  if (!config_or_error.ok()) {
    return config_or_error.status();
  }

  // Parse cleanup interval.
  std::chrono::milliseconds cleanup_interval(5000);
  if (proto_config.has_cleanup_interval()) {
    cleanup_interval = std::chrono::milliseconds(
        DurationUtil::durationToMilliseconds(proto_config.cleanup_interval()));
  }

  // Get max hosts.
  uint32_t max_hosts = proto_config.max_hosts() > 0 ? proto_config.max_hosts() : 1024;

  // Create the cluster.
  absl::Status creation_status = absl::OkStatus();
  auto new_cluster = std::shared_ptr<DynamicModuleCluster>(new DynamicModuleCluster(
      cluster, std::move(config_or_error.value()), proto_config.dynamic_host_discovery(),
      cleanup_interval, max_hosts, context, creation_status));
  RETURN_IF_NOT_OK(creation_status);

  // Create the load balancer.
  auto handle = std::make_shared<DynamicModuleClusterHandle>(new_cluster);
  auto lb = std::make_unique<DynamicModuleThreadAwareLoadBalancer>(handle);

  return std::make_pair(std::move(new_cluster), std::move(lb));
}

REGISTER_FACTORY(DynamicModuleClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
