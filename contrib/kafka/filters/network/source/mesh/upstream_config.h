#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "envoy/common/pure.h"

#include "source/common/common/logger.h"

#include "absl/types/optional.h"
#include "contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.h"
#include "contrib/envoy/extensions/filters/network/kafka_mesh/v3alpha/kafka_mesh.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

using KafkaMeshProtoConfig = envoy::extensions::filters::network::kafka_mesh::v3alpha::KafkaMesh;

// Minor helper structure that contains information about upstream Kafka clusters.
struct ClusterConfig {

  // Cluster name, as it appears in configuration input.
  std::string name_;

  // How many partitions do we expect for every one of the topics present in given upstream cluster.
  // Impl note: this could be replaced with creating (shared?) AdminClient and having it reach out
  // upstream to get configuration (or we could just send a correct request via codec). The response
  // would need to be cached (as this data is frequently requested).
  int32_t partition_count_;

  // The configuration that will be passed to upstream client for given cluster.
  // This allows us to reference different clusters with different configs (e.g. linger.ms).
  // This map always contains entry with key 'bootstrap.servers', as this is the only mandatory
  // producer property.
  std::map<std::string, std::string> upstream_producer_properties_;

  // This map always contains entries with keys 'bootstrap.servers' and 'group.id', as these are the
  // only mandatory consumer properties.
  std::map<std::string, std::string> upstream_consumer_properties_;

  bool operator==(const ClusterConfig& rhs) const {
    return name_ == rhs.name_ && partition_count_ == rhs.partition_count_ &&
           upstream_producer_properties_ == rhs.upstream_producer_properties_ &&
           upstream_consumer_properties_ == rhs.upstream_consumer_properties_;
  }
};

/**
 * Keeps the configuration related to upstream Kafka clusters.
 */
class UpstreamKafkaConfiguration {
public:
  virtual ~UpstreamKafkaConfiguration() = default;

  // Return this the host-port pair that's provided to Kafka clients.
  // This value needs to follow same rules as 'advertised.address' property of Kafka broker.
  virtual std::pair<std::string, int32_t> getAdvertisedAddress() const PURE;

  // Provides cluster for given Kafka topic, according to the rules contained within this
  // configuration object.
  virtual absl::optional<ClusterConfig>
  computeClusterConfigForTopic(const std::string& topic) const PURE;
};

using UpstreamKafkaConfigurationSharedPtr = std::shared_ptr<const UpstreamKafkaConfiguration>;

/**
 * Implementation that uses only topic-prefix to figure out which Kafka cluster to use.
 */
class UpstreamKafkaConfigurationImpl : public UpstreamKafkaConfiguration,
                                       private Logger::Loggable<Logger::Id::kafka> {
public:
  UpstreamKafkaConfigurationImpl(const KafkaMeshProtoConfig& config);

  // UpstreamKafkaConfiguration
  absl::optional<ClusterConfig>
  computeClusterConfigForTopic(const std::string& topic) const override;

  // UpstreamKafkaConfiguration
  std::pair<std::string, int32_t> getAdvertisedAddress() const override;

private:
  const std::pair<std::string, int32_t> advertised_address_;
  std::map<std::string, ClusterConfig> topic_prefix_to_cluster_config_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
