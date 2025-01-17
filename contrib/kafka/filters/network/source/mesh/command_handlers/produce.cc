#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce.h"

#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

constexpr static int16_t NO_ERROR = 0;

ProduceRequestHolder::ProduceRequestHolder(AbstractRequestListener& filter,
                                           UpstreamKafkaFacade& kafka_facade,
                                           const std::shared_ptr<Request<ProduceRequest>> request)
    : ProduceRequestHolder{filter, kafka_facade, RecordExtractorImpl{}, request} {};

ProduceRequestHolder::ProduceRequestHolder(AbstractRequestListener& filter,
                                           UpstreamKafkaFacade& kafka_facade,
                                           const RecordExtractor& record_extractor,
                                           const std::shared_ptr<Request<ProduceRequest>> request)
    : BaseInFlightRequest{filter}, kafka_facade_{kafka_facade}, request_{request} {
  outbound_records_ = record_extractor.extractRecords(request_->data_.topic_data_);
  expected_responses_ = outbound_records_.size();
}

void ProduceRequestHolder::startProcessing() {
  // Main part of the proxy: for each outbound record we get the appropriate sink (effectively a
  // facade for upstream Kafka cluster), and send the record to it.
  for (const OutboundRecord& outbound_record : outbound_records_) {
    KafkaProducer& producer = kafka_facade_.getProducerForTopic(outbound_record.topic_);
    // We need to provide our object as first argument, as we will want to be notified when the
    // delivery finishes.
    producer.send(shared_from_this(), outbound_record);
  }
  // Corner case handling:
  // If we ever receive produce request without records, we need to notify the filter we are ready,
  // because otherwise no notification will ever come from the real Kafka producer.
  if (finished()) {
    notifyFilter();
  }
}

bool ProduceRequestHolder::finished() const { return 0 == expected_responses_; }

// Find a record that matches provided delivery confirmation coming from Kafka producer.
// If all the records got their delivery data filled in, we are done, and can notify the origin
// filter.
bool ProduceRequestHolder::accept(const DeliveryMemento& memento) {
  for (auto& outbound_record : outbound_records_) {
    if (outbound_record.value_.data() == memento.data_) {
      // We have matched the downstream request that matches our confirmation from upstream Kafka.
      outbound_record.error_code_ = memento.error_code_;
      outbound_record.saved_offset_ = memento.offset_;
      --expected_responses_;
      if (finished()) {
        // All elements had their responses matched.
        ENVOY_LOG(trace, "All deliveries finished for produce request {}",
                  request_->request_header_.correlation_id_);
        notifyFilter();
      }
      return true;
    }
  }
  return false;
}

AbstractResponseSharedPtr ProduceRequestHolder::computeAnswer() const {

  // Header.
  const RequestHeader& rh = request_->request_header_;
  ResponseMetadata metadata = {rh.api_key_, rh.api_version_, rh.correlation_id_};

  // Real answer.
  using ErrorCodeAndOffset = std::pair<int16_t, uint32_t>;
  std::map<std::string, std::map<int32_t, ErrorCodeAndOffset>> topic_to_partition_responses;
  for (const auto& outbound_record : outbound_records_) {
    auto& partition_map = topic_to_partition_responses[outbound_record.topic_];
    auto it = partition_map.find(outbound_record.partition_);
    if (it == partition_map.end()) {
      partition_map[outbound_record.partition_] = {outbound_record.error_code_,
                                                   outbound_record.saved_offset_};
    } else {
      // Proxy logic - aggregating multiple upstream answers into single downstream answer.
      // Let's fail if anything fails, otherwise use the lowest offset (like Kafka would have done).
      ErrorCodeAndOffset& curr = it->second;
      if (NO_ERROR == curr.first) {
        curr.first = outbound_record.error_code_;
        curr.second = std::min(curr.second, outbound_record.saved_offset_);
      }
    }
  }

  std::vector<TopicProduceResponse> topic_responses;
  for (const auto& topic_entry : topic_to_partition_responses) {
    std::vector<PartitionProduceResponse> partition_responses;
    for (const auto& partition_entry : topic_entry.second) {
      const int32_t& partition = partition_entry.first;
      const int16_t& error_code = partition_entry.second.first;
      const int64_t& offset = partition_entry.second.second;
      partition_responses.emplace_back(partition, error_code, offset);
    }
    const std::string& topic = topic_entry.first;
    topic_responses.emplace_back(topic, partition_responses);
  }

  ProduceResponse data = {topic_responses, 0};
  return std::make_shared<Response<ProduceResponse>>(metadata, data);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
