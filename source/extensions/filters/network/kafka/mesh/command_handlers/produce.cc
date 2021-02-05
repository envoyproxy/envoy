#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

#include "extensions/filters/network/kafka/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

ProduceRequestHolder::ProduceRequestHolder(AbstractRequestListener& filter,
                                           const std::shared_ptr<Request<ProduceRequest>> request)
    : ProduceRequestHolder{filter, RecordExtractorImpl{}, request} {};

ProduceRequestHolder::ProduceRequestHolder(AbstractRequestListener& filter,
                                           const RecordExtractor& record_extractor,
                                           const std::shared_ptr<Request<ProduceRequest>> request)
    : BaseInFlightRequest{filter}, request_{request} {
  outbound_records_ = record_extractor.extractRecords(request_->data_.topics_);
  expected_responses_ = outbound_records_.size();
}

void ProduceRequestHolder::invoke(UpstreamKafkaFacade& kafka_facade) {
  // Main part of the proxy: for each outbound record we get the appropriate sink (effectively a
  // facade for upstream Kafka cluster), and send the record to it.
  for (auto& outbound_record : outbound_records_) {
    RecordSink& producer = kafka_facade.getProducerForTopic(outbound_record.topic_);
    // We need to provide our object as first argument, as we will want to be notified when the
    // delivery finishes.
    producer.send(shared_from_this(), outbound_record.topic_, outbound_record.partition_,
                  outbound_record.key_, outbound_record.value_);
  }
  // Corner case handling:
  // If we ever receive produce request without records, we need to notify the filter we are ready,
  // because otherwise no notification will ever come from the real Kafka producer.
  if (0 == expected_responses_) {
    notifyFilter();
  }
}

bool ProduceRequestHolder::finished() const { return 0 == expected_responses_; }

bool ProduceRequestHolder::accept(const DeliveryMemento& memento) {
  ENVOY_LOG(warn, "ProduceRequestHolder - accept: {}/{}", memento.error_code_, memento.offset_);
  for (auto& outbound_record : outbound_records_) {
    if (outbound_record.value_.data() == memento.data_) {
      // We have matched the downstream request that matches our confirmation from upstream Kafka.
      ENVOY_LOG(trace, "ProduceRequestHolder - accept - match found for {} in request {}",
                reinterpret_cast<long>(memento.data_), request_->request_header_.correlation_id_);
      outbound_record.error_code_ = memento.error_code_;
      outbound_record.saved_offset_ = memento.offset_;
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
      if (RdKafka::ErrorCode::ERR_NO_ERROR == curr.first) {
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

// Record extraction.

std::vector<OutboundRecord>
RecordExtractorImpl::extractRecords(const std::vector<TopicProduceData>& data) const {
  std::vector<OutboundRecord> result;
  for (auto const& topic_data : data) {
    for (auto const& partition_data : topic_data.partitions_) {
      if (partition_data.records_) {
        const auto topic_result = extractRecordsForTopic(
            topic_data.name_, partition_data.partition_index_, *(partition_data.records_));
        result.insert(result.end(), topic_result.begin(), topic_result.end());
      }
    }
  }
  return result;
}

std::vector<OutboundRecord> RecordExtractorImpl::extractRecordsForTopic(const std::string& topic,
                                                                        const int32_t partition,
                                                                        const Bytes& bytes) const {

  // Reference implementation:
  // org.apache.kafka.common.record.DefaultRecordBatch.writeHeader(ByteBuffer, long, int, int, byte,
  // CompressionType, TimestampType, long, long, long, short, int, boolean, boolean, int, int)
  absl::string_view data = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};

  // Fields common to any records payload. Magic will follow.
  const unsigned int common_fields_size =
      /* BaseOffset */ 8 + /* Length */ 4 + /* PartitionLeaderEpoch */ 4;
  if (data.length() < common_fields_size) {
    throw EnvoyException(fmt::format("record batch for [{}-{}] is too short (no common fields): {}",
                                     topic, partition, data.length()));
  }
  // Let's skip these common fields, because we are not using them.
  data = {data.data() + common_fields_size, data.length() - common_fields_size};

  // Extract magic.
  // Magic tells us what is the format of records present in the byte array.
  Int8Deserializer magic_deserializer;
  magic_deserializer.feed(data);
  if (magic_deserializer.ready()) {
    int8_t magic = magic_deserializer.get();
    if (2 == magic) {
      // Magic format introduced around Kafka 1.0.0 and still used with Kafka 2.4.
      // We can extract the records out of the record batch.
      return extractRecordsOutOfBatchWithMagicEqualTo2(topic, partition, data);
    } else {
      // Old client sending old magic, or Apache Kafka introducing new magic.
      throw EnvoyException(fmt::format("unknown magic value in record batch for [{}-{}]: {}", topic,
                                       partition, magic));
    }
  } else {
    throw EnvoyException(
        fmt::format("magic byte is not present in record batch for [{}-{}]", topic, partition));
  }
}

std::vector<OutboundRecord> RecordExtractorImpl::extractRecordsOutOfBatchWithMagicEqualTo2(
    const std::string& topic, const int32_t partition, absl::string_view data) const {

  // Not going to reuse the information in these fields, because we are going to republish.
  unsigned int ignored_fields_size =
      /* CRC */ 4 + /* Attributes */ 2 + /* LastOffsetDelta */ 4 +
      /* FirstTimestamp */ 8 + /* MaxTimestamp */ 8 + /* ProducerId */ 8 +
      /* ProducerEpoch */ 2 + /* BaseSequence */ 4 + /* RecordCount */ 4;

  if (data.length() < ignored_fields_size) {
    throw EnvoyException(
        fmt::format("record batch for [{}-{}] is too short (no attribute fields): {}", topic,
                    partition, data.length()));
  }
  data = {data.data() + ignored_fields_size, data.length() - ignored_fields_size};

  // We have managed to consume all the fancy bytes, now it's time to get to records.

  std::vector<OutboundRecord> result;
  while (!data.empty()) {
    const OutboundRecord record = extractRecord(topic, partition, data);
    result.push_back(record);
  }
  return result;
}

OutboundRecord RecordExtractorImpl::extractRecord(const std::string& topic, const int32_t partition,
                                                  absl::string_view& data) const {
  // The reference implementation is:
  // org.apache.kafka.common.record.DefaultRecord.writeTo(DataOutputStream, int, long, ByteBuffer,
  // ByteBuffer, Header[])

  VarInt32Deserializer length;
  length.feed(data);
  if (!length.ready()) {
    throw EnvoyException(
        fmt::format("record for [{}-{}] is too short (no length)", topic, partition));
  }
  const int32_t len = length.get();

  const absl::string_view expected_end_of_record = {data.data() + len, data.length() - len};

  Int8Deserializer attributes;
  attributes.feed(data);
  VarInt64Deserializer tsDelta;
  tsDelta.feed(data);
  VarUInt32Deserializer offsetDelta;
  offsetDelta.feed(data);
  if (!attributes.ready() || !tsDelta.ready() || !offsetDelta.ready()) {
    throw EnvoyException(
        fmt::format("attributes not present in record for [{}-{}]", topic, partition));
  }

  absl::string_view key = extractElement(data);
  absl::string_view value = extractElement(data);

  VarInt32Deserializer headers_count_deserializer;
  headers_count_deserializer.feed(data);
  if (!headers_count_deserializer.ready()) {
    throw EnvoyException(
        fmt::format("header count not present in record for [{}-{}]", topic, partition));
  }
  const int32_t headers_count = headers_count_deserializer.get();
  if (headers_count < 0) {
    throw EnvoyException(fmt::format("invalid header count in record for [{}-{}]: {}", topic,
                                     partition, headers_count));
  }
  for (int32_t i = 0; i < headers_count; ++i) {
    extractElement(data); // header key
    extractElement(data); // header value
  }

  if (data == expected_end_of_record) {
    // We have consumed everything nicely.
    return OutboundRecord{topic, partition, key, value};
  } else {
    // Bad data - there are bytes left.
    throw EnvoyException(fmt::format("data left after consuming record for [{}-{}]: {}", topic,
                                     partition, data.length()));
  }
}

// Most of the fields in records are kept as variable-encoded length and following bytes.
// So here we have a helper function to get the data (such as key, value) out of given input.
absl::string_view RecordExtractorImpl::extractElement(absl::string_view& input) {
  VarInt32Deserializer length_deserializer;
  length_deserializer.feed(input);
  if (!length_deserializer.ready()) {
    throw EnvoyException("byte array length not present");
  }
  const int32_t length = length_deserializer.get();
  // Length can be negative (null value was published by client).
  if (-1 == length) {
    return {};
  }

  if (length >= 0) {
    if (static_cast<absl::string_view::size_type>(length) > input.size()) {
      throw EnvoyException(fmt::format("byte array length larger than data provided: {} vs {}",
                                       length, input.size()));
    }
    const absl::string_view result = {input.data(),
                                      static_cast<absl::string_view::size_type>(length)};
    input = {input.data() + length, input.length() - length};
    return result;
  } else {
    throw EnvoyException(fmt::format("byte array length less than -1: {}", length));
  }
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
