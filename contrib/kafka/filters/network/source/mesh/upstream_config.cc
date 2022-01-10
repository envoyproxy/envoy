#include "contrib/kafka/filters/network/source/mesh/upstream_config.h"

#include "envoy/common/exception.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using KafkaClusterDefinition =
    envoy::extensions::filters::network::kafka_mesh::v3alpha::KafkaClusterDefinition;
using ForwardingRule = envoy::extensions::filters::network::kafka_mesh::v3alpha::ForwardingRule;

UpstreamKafkaConfigurationImpl::UpstreamKafkaConfigurationImpl(const KafkaMeshProtoConfig& config)
    : advertised_address_{config.advertised_host(), config.advertised_port()} {

  // Processing cluster data.
  const auto& upstream_clusters = config.upstream_clusters();
  if (upstream_clusters.empty()) {
    throw EnvoyException("kafka-mesh filter needs to have at least one upstream Kafka cluster");
  }

  // Processing cluster configuration.
  std::map<std::string, ClusterConfig> cluster_name_to_cluster_config;
  for (const auto& upstream_cluster_definition : upstream_clusters) {
    const std::string& cluster_name = upstream_cluster_definition.cluster_name();

    // No duplicates are allowed.
    if (cluster_name_to_cluster_config.find(cluster_name) != cluster_name_to_cluster_config.end()) {
      throw EnvoyException(
          absl::StrCat("kafka-mesh filter has multiple Kafka clusters referenced by the same name",
                       cluster_name));
    }

    // Upstream client configuration - use all the optional custom configs provided, and then use
    // the target IPs.
    std::map<std::string, std::string> producer_configs = {
        upstream_cluster_definition.producer_config().begin(),
        upstream_cluster_definition.producer_config().end()};
    producer_configs["bootstrap.servers"] = upstream_cluster_definition.bootstrap_servers();
    ClusterConfig cluster_config = {cluster_name, upstream_cluster_definition.partition_count(),
                                    producer_configs};
    cluster_name_to_cluster_config[cluster_name] = cluster_config;
  }

  // Processing forwarding rules.
  const auto& forwarding_rules = config.forwarding_rules();
  if (forwarding_rules.empty()) {
    throw EnvoyException("kafka-mesh filter needs to have at least one forwarding rule");
  }

  for (const auto& rule : forwarding_rules) {
    const std::string& target_cluster = rule.target_cluster();
    ASSERT(rule.trigger_case() == ForwardingRule::TriggerCase::kTopicPrefix);
    ENVOY_LOG(trace, "Setting up forwarding rule: {} -> {}", rule.topic_prefix(), target_cluster);
    // Each forwarding rule needs to reference a cluster.
    if (cluster_name_to_cluster_config.find(target_cluster) ==
        cluster_name_to_cluster_config.end()) {
      throw EnvoyException(absl::StrCat(
          "kafka-mesh filter forwarding rule is referencing unknown upstream Kafka cluster: ",
          target_cluster));
    }
    topic_prefix_to_cluster_config_[rule.topic_prefix()] =
        cluster_name_to_cluster_config[target_cluster];
  }
}

absl::optional<ClusterConfig>
UpstreamKafkaConfigurationImpl::computeClusterConfigForTopic(const std::string& topic) const {
  // We find the first matching prefix (this is why ordering is important).
  for (const auto& it : topic_prefix_to_cluster_config_) {
    if (topic.rfind(it.first, 0) == 0) {
      const ClusterConfig cluster_config = it.second;
      return absl::make_optional(cluster_config);
    }
  }
  return absl::nullopt;
}

std::pair<std::string, int32_t> UpstreamKafkaConfigurationImpl::getAdvertisedAddress() const {
  return advertised_address_;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
