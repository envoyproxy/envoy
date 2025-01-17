#include "contrib/kafka/filters/network/source/mesh/command_handlers/list_offsets.h"

#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

ListOffsetsRequestHolder::ListOffsetsRequestHolder(
    AbstractRequestListener& filter, const std::shared_ptr<Request<ListOffsetsRequest>> request)
    : BaseInFlightRequest{filter}, request_{request} {}

void ListOffsetsRequestHolder::startProcessing() { notifyFilter(); }

bool ListOffsetsRequestHolder::finished() const { return true; }

AbstractResponseSharedPtr ListOffsetsRequestHolder::computeAnswer() const {
  const auto& header = request_->request_header_;
  const ResponseMetadata metadata = {header.api_key_, header.api_version_, header.correlation_id_};

  // The response contains all the requested topics (we do not do any filtering here).
  const auto& topics = request_->data_.topics_;
  std::vector<ListOffsetsTopicResponse> topic_responses;
  topic_responses.reserve(topics.size());
  for (const auto& topic : topics) {
    const auto& partitions = topic.partitions_;
    std::vector<ListOffsetsPartitionResponse> partition_responses;
    partition_responses.reserve(partitions.size());
    for (const auto& partition : partitions) {
      const int16_t error_code = 0;
      const int64_t timestamp = 0;
      /* As we are going to ignore consumer offset requests, we can reply with dummy values. */
      const int64_t offset = 0;
      const ListOffsetsPartitionResponse partition_response = {partition.partition_index_,
                                                               error_code, timestamp, offset};
      partition_responses.push_back(partition_response);
    }
    const ListOffsetsTopicResponse topic_response = {topic.name_, partition_responses};
    topic_responses.push_back(topic_response);
  }

  const ListOffsetsResponse data = {topic_responses};
  return std::make_shared<Response<ListOffsetsResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
