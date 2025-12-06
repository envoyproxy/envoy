#include "source/extensions/clusters/dynamic_modules/factory.h"

#include "envoy/config/cluster/v3/cluster.pb.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/clusters/dynamic_modules/cluster.h"
#include "source/extensions/clusters/dynamic_modules/cluster_config.h"
#include "source/extensions/dynamic_modules/dynamic_modules.h"

namespace Envoy {
namespace Extensions {
namespace Clusters {
namespace DynamicModules {

absl::StatusOr<std::pair<Upstream::ClusterImplBaseSharedPtr, Upstream::ThreadAwareLoadBalancerPtr>>
DynamicModuleClusterFactory::createClusterWithConfig(
    const envoy::config::cluster::v3::Cluster& cluster,
    const envoy::extensions::clusters::dynamic_modules::v3::ClusterConfig& proto_config,
    Upstream::ClusterFactoryContext& context) {

  // Extract the cluster configuration.
  std::string cluster_config_str;
  if (proto_config.has_cluster_config()) {
    const auto& config_any = proto_config.cluster_config();

    // Handle different protobuf types similar to HTTP filters.
    if (config_any.type_url() == "type.googleapis.com/google.protobuf.StringValue") {
      google::protobuf::StringValue string_value;
      if (!config_any.UnpackTo(&string_value)) {
        return absl::InvalidArgumentError("Failed to unpack StringValue");
      }
      cluster_config_str = string_value.value();
    } else if (config_any.type_url() == "type.googleapis.com/google.protobuf.BytesValue") {
      google::protobuf::BytesValue bytes_value;
      if (!config_any.UnpackTo(&bytes_value)) {
        return absl::InvalidArgumentError("Failed to unpack BytesValue");
      }
      cluster_config_str = bytes_value.value();
    } else {
      // For other types, serialize as JSON.
      Protobuf::Struct struct_config;
      if (config_any.UnpackTo(&struct_config)) {
        cluster_config_str = MessageUtil::getJsonStringFromMessageOrError(struct_config);
      } else {
        // Just pass the serialized Any.
        cluster_config_str = config_any.SerializeAsString();
      }
    }
  }

  // Load the dynamic module.
  const auto& module_config = proto_config.dynamic_module_config();
  auto dynamic_module = Extensions::DynamicModules::newDynamicModuleByName(
      module_config.name(), module_config.do_not_close(), module_config.load_globally());
  RETURN_IF_NOT_OK(dynamic_module.status());

  // Create the cluster configuration.
  auto cluster_module_config = newDynamicModuleClusterConfig(
      proto_config.cluster_name(), cluster_config_str, std::move(dynamic_module.value()),
      context.serverFactoryContext());
  RETURN_IF_NOT_OK(cluster_module_config.status());

  // Create the cluster instance.
  absl::Status creation_status = absl::OkStatus();
  auto cluster_impl = std::make_shared<DynamicModuleCluster>(
      cluster, proto_config, cluster_module_config.value(), context, creation_status);

  RETURN_IF_NOT_OK(creation_status);

  // Create the thread-aware load balancer.
  auto lb = std::make_unique<DynamicModuleCluster::ThreadAwareLoadBalancer>(cluster_impl);

  return std::make_pair(cluster_impl, std::move(lb));
}

REGISTER_FACTORY(DynamicModuleClusterFactory, Upstream::ClusterFactory);

} // namespace DynamicModules
} // namespace Clusters
} // namespace Extensions
} // namespace Envoy
