#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

#include "extensions/filters/network/kafka/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

ProduceRequestHolder::ProduceRequestHolder(AbstractRequestListener& filter,
                                           const std::shared_ptr<Request<ProduceRequest>> request)
    : AbstractInFlightRequest{filter}, request_{request} {
  for (auto const& topic_data : request_->data_.topics_) {
    for (auto const& partition_data : topic_data.partitions_) {
      if (partition_data.records_) {
        const auto footmarks = computeFootmarks(topic_data.name_, partition_data.partition_index_,
                                                *(partition_data.records_));
        footmarks_.insert(footmarks_.end(), footmarks.begin(), footmarks.end());
      }
    }
  }
  expected_responses_ = footmarks_.size();
}

absl::string_view comsumeBytes(absl::string_view& input) {
  VarInt32Deserializer length_deserializer;
  length_deserializer.feed(input);
  const int32_t length = length_deserializer.get();
  if (length >= 0) {
    const absl::string_view result = {input.data(),
                                      static_cast<absl::string_view::size_type>(length)};
    input = {input.data() + length, input.length() - length};
    return result;
  } else {
    return {};
  }
}

std::vector<ProduceRequestHolder::RecordFootmark>
ProduceRequestHolder::computeFootmarks(const std::string& topic, const int32_t partition,
                                       const Bytes& records) {
  // org.apache.kafka.common.record.DefaultRecordBatch.writeHeader(ByteBuffer, long, int, int, byte,
  // CompressionType, TimestampType, long, long, long, short, int, boolean, boolean, int, int)
  const char* ptr = reinterpret_cast<const char*>(records.data());
  absl::string_view sv = {ptr, records.size()};

  unsigned int step = /* BaseOffset */ 8 + /* Length */ 4 + /* PartitionLeaderEpoch */ 4;
  if (sv.length() < step) {
    ENVOY_LOG(error, "bad data 1");
    return {};
  }
  sv = {sv.data() + step, sv.length() - step};

  /* Magic */
  Int8Deserializer magic_deserializer;
  magic_deserializer.feed(sv);

  if (magic_deserializer.ready()) {
    int8_t magic = magic_deserializer.get();
    switch (magic) {
      case 0:
      case 1:
        ENVOY_LOG(error, "bad data 2 - magic not equal to 2: {}", magic);
        return {};
      case 2:
        return processMagic2(topic, partition, sv);
      default:
        ENVOY_LOG(error, "bad data 2 - magic not equal to 2: {}", magic);
        return {};
    }
  }

  ENVOY_LOG(error, "bad data 3 - no magic present");
  return {};
}

std::vector<ProduceRequestHolder::RecordFootmark> ProduceRequestHolder::processMagic2(const std::string& topic, const int32_t partition, absl::string_view sv) {

  unsigned int step2 = /* CRC */ 4 + /* Attributes */ 2 + /* LastOffsetDelta */ 4 + /* FirstTimestamp */ 8 + /* MaxTimestamp */ 8 + /* ProducerId */ 8 + /* ProducerEpoch */ 2 + /* BaseSequence */ 4;
  if (sv.length() < step2) {
    ENVOY_LOG(error, "bad data 3");
    return {};
  }
  sv = {sv.data() + step2, sv.length() - step2};

  Int32Deserializer count_des;
  count_des.feed(sv);
  const int32_t record_count = count_des.get();
  ENVOY_LOG(trace, "found {} records in records byte-array", record_count);

  std::vector<ProduceRequestHolder::RecordFootmark> result;
  while (!sv.empty()) {

    // org.apache.kafka.common.record.DefaultRecord.writeTo(DataOutputStream, int, long, ByteBuffer,
    // ByteBuffer, Header[])

    VarInt32Deserializer length;
    length.feed(sv);
    const int32_t len = length.get();
    // ENVOY_LOG(trace, "record len = {}", len);

    const absl::string_view expected_end_of_record = {sv.data() + len, sv.length() - len};

    Int8Deserializer attributes;
    attributes.feed(sv);
    VarInt64Deserializer tsDelta;
    tsDelta.feed(sv);
    VarUInt32Deserializer offsetDelta;
    offsetDelta.feed(sv);

    absl::string_view key = comsumeBytes(sv);
    absl::string_view value = comsumeBytes(sv);

    VarInt32Deserializer headers_count_deserializer;
    headers_count_deserializer.feed(sv);
    const int32_t headers_count = headers_count_deserializer.get();
    if (headers_count < 0) {
      // boom
    }
    for (int32_t i = 0; i < headers_count; ++i) {
      comsumeBytes(sv); // header key
      comsumeBytes(sv); // header value
    }

    // ENVOY_LOG(trace, "key = [{}], value = [{}]", key, value);

    if (sv != expected_end_of_record) {
      ENVOY_LOG(error, "bad data 4 - they differ {} {}", sv.length(),
                expected_end_of_record.length());
      return {};
    }

    result.emplace_back(topic, partition, key, value);
  }
  return result;
}

void ProduceRequestHolder::invoke(UpstreamKafkaFacade& kafka_facade) {
  for (auto& fm : footmarks_) {
    KafkaProducerWrapper& producer = kafka_facade.getProducerForTopic(fm.topic_);
    producer.send(shared_from_this(), fm.topic_, fm.partition_, fm.key_, fm.value_);
  }
}

bool ProduceRequestHolder::finished() const { return 0 == expected_responses_; }

bool ProduceRequestHolder::accept(const DeliveryMemento& memento) {
  ENVOY_LOG(warn, "ProduceRequestHolder - accept: {}/{}", memento.error_code_, memento.offset_);
  for (auto& footmark : footmarks_) {
    if (footmark.value_.data() == memento.data_) {
      // We have matched the downstream request that matches our confirmation from upstream Kafka.
      ENVOY_LOG(trace, "ProduceRequestHolder - accept - match found for {} in request {}",
                reinterpret_cast<long>(memento.data_), request_->request_header_.correlation_id_);
      footmark.error_code_ = memento.error_code_;
      footmark.saved_offset_ = memento.offset_;
      --expected_responses_;
      if (finished()) {
        // All elements had their responses matched.
        // We can notify the filter to check if it can send another response.
        ENVOY_LOG(warn, "ProduceRequestHolder - accept - notifying filter for {}",
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
  for (const auto& footmark : footmarks_) {
    auto& partition_map = topic_to_partition_responses[footmark.topic_];
    auto it = partition_map.find(footmark.partition_);
    if (it == partition_map.end()) {
      partition_map[footmark.partition_] = {footmark.error_code_, footmark.saved_offset_};
    } else {
      // Proxy logic - aggregating multiple upstream answers into single downstream answer.
      // Let's fail if anything fails, otherwise use the lowest offset (like Kafka would have done).
      ErrorCodeAndOffset& curr = it->second;
      if (RdKafka::ErrorCode::ERR_NO_ERROR == curr.first) {
        curr.first = footmark.error_code_;
        curr.second = std::min(curr.second, footmark.saved_offset_);
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
