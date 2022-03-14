#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce_record_extractor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

std::vector<OutboundRecord>
RecordExtractorImpl::extractRecords(const std::vector<TopicProduceData>& data) const {
  std::vector<OutboundRecord> result;
  for (const auto& topic_data : data) {
    for (const auto& partition_data : topic_data.partition_data_) {
      // Kafka protocol allows nullable data.
      if (partition_data.records_) {
        const auto topic_result = extractPartitionRecords(topic_data.name_, partition_data.index_,
                                                          *(partition_data.records_));
        std::copy(topic_result.begin(), topic_result.end(), std::back_inserter(result));
      }
    }
  }
  return result;
}

// Fields common to any record batch payload.
// See:
// https://github.com/apache/kafka/blob/2.4.1/clients/src/main/java/org/apache/kafka/common/record/DefaultRecordBatch.java#L46
constexpr unsigned int RECORD_BATCH_COMMON_FIELDS_SIZE = /* BaseOffset */ sizeof(int64_t) +
                                                         /* Length */ sizeof(int32_t) +
                                                         /* PartitionLeaderEpoch */ sizeof(int32_t);

// Magic format introduced around Kafka 1.0.0 and still used with Kafka 2.4.
// We can extract records out of record batches that use this magic.
constexpr int8_t SUPPORTED_MAGIC = 2;

// Reference implementation:
// https://github.com/apache/kafka/blob/2.4.1/clients/src/main/java/org/apache/kafka/common/record/DefaultRecordBatch.java#L443
std::vector<OutboundRecord> RecordExtractorImpl::extractPartitionRecords(const std::string& topic,
                                                                         const int32_t partition,
                                                                         const Bytes& bytes) const {

  absl::string_view data = {reinterpret_cast<const char*>(bytes.data()), bytes.size()};

  // Let's skip these common fields, because we are not using them.
  if (data.length() < RECORD_BATCH_COMMON_FIELDS_SIZE) {
    throw EnvoyException(fmt::format("record batch for [{}-{}] is too short (no common fields): {}",
                                     topic, partition, data.length()));
  }
  data = {data.data() + RECORD_BATCH_COMMON_FIELDS_SIZE,
          data.length() - RECORD_BATCH_COMMON_FIELDS_SIZE};

  // Extract magic - it what is the format of records present in the bytes provided.
  Int8Deserializer magic_deserializer;
  magic_deserializer.feed(data);
  if (!magic_deserializer.ready()) {
    throw EnvoyException(
        fmt::format("magic byte is not present in record batch for [{}-{}]", topic, partition));
  }

  // Old client sending old magic, or Apache Kafka introducing new magic.
  const int8_t magic = magic_deserializer.get();
  if (SUPPORTED_MAGIC != magic) {
    throw EnvoyException(fmt::format("unknown magic value in record batch for [{}-{}]: {}", topic,
                                     partition, magic));
  }

  // We have received a record batch with good magic.
  return processRecordBatch(topic, partition, data);
}

// Record batch fields we are going to ignore (because we rip it up and send its contents).
// See:
// https://github.com/apache/kafka/blob/2.4.1/clients/src/main/java/org/apache/kafka/common/record/DefaultRecordBatch.java#L50
// and:
// https://github.com/apache/kafka/blob/2.4.1/clients/src/main/java/org/apache/kafka/common/record/DefaultRecordBatch.java#L471
constexpr unsigned int IGNORED_FIELDS_SIZE =
    /* CRC */ sizeof(int32_t) + /* Attributes */ sizeof(int16_t) +
    /* LastOffsetDelta */ sizeof(int32_t) + /* FirstTimestamp */ sizeof(int64_t) +
    /* MaxTimestamp */ sizeof(int64_t) + /* ProducerId */ sizeof(int64_t) +
    /* ProducerEpoch */ sizeof(int16_t) + /* BaseSequence */ sizeof(int32_t) +
    /* RecordCount */ sizeof(int32_t);

std::vector<OutboundRecord> RecordExtractorImpl::processRecordBatch(const std::string& topic,
                                                                    const int32_t partition,
                                                                    absl::string_view data) const {

  if (data.length() < IGNORED_FIELDS_SIZE) {
    throw EnvoyException(
        fmt::format("record batch for [{}-{}] is too short (no attribute fields): {}", topic,
                    partition, data.length()));
  }
  data = {data.data() + IGNORED_FIELDS_SIZE, data.length() - IGNORED_FIELDS_SIZE};

  // We have managed to consume all the fancy bytes, now it's time to get to records.
  std::vector<OutboundRecord> result;
  while (!data.empty()) {
    const OutboundRecord record = extractRecord(topic, partition, data);
    result.push_back(record);
  }
  return result;
}

// Reference implementation:
// https://github.com/apache/kafka/blob/2.4.1/clients/src/main/java/org/apache/kafka/common/record/DefaultRecord.java#L179
OutboundRecord RecordExtractorImpl::extractRecord(const std::string& topic, const int32_t partition,
                                                  absl::string_view& data) const {

  VarInt32Deserializer length;
  length.feed(data);
  if (!length.ready()) {
    throw EnvoyException(
        fmt::format("record for [{}-{}] is too short (no length)", topic, partition));
  }
  const int32_t len = length.get();
  if (len < 0) {
    throw EnvoyException(
        fmt::format("record for [{}-{}] has invalid length: {}", topic, partition, len));
  }
  if (static_cast<uint32_t>(len) > data.length()) {
    throw EnvoyException(fmt::format("record for [{}-{}] is too short (not enough bytes provided)",
                                     topic, partition));
  }

  const absl::string_view expected_end_of_record = {data.data() + len, data.length() - len};

  // We throw away the following batch fields: attributes, timestamp delta, offset delta (cannot do
  // an easy jump, as some are variable-length).
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

  // Record key and value.
  const absl::string_view key = extractByteArray(data);
  const absl::string_view value = extractByteArray(data);

  // Headers.
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
  std::vector<Header> headers;
  headers.reserve(headers_count);
  for (int32_t i = 0; i < headers_count; ++i) {
    const absl::string_view header_key = extractByteArray(data);
    const absl::string_view header_value = extractByteArray(data);
    headers.emplace_back(header_key, header_value);
  }

  if (data == expected_end_of_record) {
    // We have consumed everything nicely.
    return OutboundRecord{topic, partition, key, value, headers};
  } else {
    // Bad data - there are bytes left.
    throw EnvoyException(fmt::format("data left after consuming record for [{}-{}]: {}", topic,
                                     partition, data.length()));
  }
}

absl::string_view RecordExtractorImpl::extractByteArray(absl::string_view& input) {

  // Get the length.
  VarInt32Deserializer length_deserializer;
  length_deserializer.feed(input);
  if (!length_deserializer.ready()) {
    throw EnvoyException("byte array length not present");
  }
  const int32_t length = length_deserializer.get();

  // Length can be -1 (null value was published by client).
  if (-1 == length) {
    return {};
  }

  // Otherwise, length cannot be negative.
  if (length < 0) {
    throw EnvoyException(fmt::format("byte array length less than -1: {}", length));
  }

  // Underflow handling.
  if (static_cast<absl::string_view::size_type>(length) > input.size()) {
    throw EnvoyException(
        fmt::format("byte array length larger than data provided: {} vs {}", length, input.size()));
  }

  // We have enough data to return it.
  const absl::string_view result = {input.data(),
                                    static_cast<absl::string_view::size_type>(length)};
  input = {input.data() + length, input.length() - length};
  return result;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
