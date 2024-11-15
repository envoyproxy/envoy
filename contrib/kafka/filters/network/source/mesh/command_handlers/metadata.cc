#include "contrib/kafka/filters/network/source/mesh/command_handlers/metadata.h"

#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

MetadataRequestHolder::MetadataRequestHolder(
    AbstractRequestListener& filter, const UpstreamKafkaConfiguration& configuration,
    const std::shared_ptr<Request<MetadataRequest>> request)
    : BaseInFlightRequest{filter}, configuration_{configuration}, request_{request} {}

// Metadata requests are immediately ready for answer (as they do not need to reach upstream).
void MetadataRequestHolder::startProcessing() { notifyFilter(); }

bool MetadataRequestHolder::finished() const { return true; }

constexpr int32_t ENVOY_BROKER_ID = 0;
constexpr int32_t NO_ERROR = 0;

// Cornerstone of how the mesh-filter actually works.
// We pretend to be one-node Kafka cluster, with Envoy instance being the only member.
// What means all the Kafka future traffic will go through this instance.
AbstractResponseSharedPtr MetadataRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  const ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  const auto advertised_address = configuration_.getAdvertisedAddress();
  MetadataResponseBroker broker = {ENVOY_BROKER_ID, advertised_address.first,
                                   advertised_address.second};
  std::vector<MetadataResponseTopic> response_topics;
  if (request_->data_.topics_) {
    for (const MetadataRequestTopic& topic : *(request_->data_.topics_)) {
      if (!topic.name_) {
        // The client sent request without topic name (UUID was sent instead).
        // We do not know how to handle it, so do not send any metadata.
        // This will cause failures in clients downstream.
        continue;
      }
      const std::string& topic_name = *(topic.name_);
      std::vector<MetadataResponsePartition> topic_partitions;
      const absl::optional<ClusterConfig> cluster_config =
          configuration_.computeClusterConfigForTopic(topic_name);
      if (!cluster_config) {
        // Someone is requesting topics that are not known to our configuration.
        // So we do not attach any metadata, this will cause clients failures downstream as they
        // will never be able to get metadata for these topics.
        continue;
      }
      for (int32_t partition_id = 0; partition_id < cluster_config->partition_count_;
           ++partition_id) {
        // Every partition is hosted by this proxy-broker.
        MetadataResponsePartition partition = {
            NO_ERROR, partition_id, broker.node_id_, {broker.node_id_}, {broker.node_id_}};
        topic_partitions.push_back(partition);
      }
      MetadataResponseTopic response_topic = {NO_ERROR, topic_name, false, topic_partitions};
      response_topics.push_back(response_topic);
    }
  }
  MetadataResponse data = {{broker}, broker.node_id_, response_topics};
  return std::make_shared<Response<MetadataResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
