#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch_record_converter.h"

#include "contrib/kafka/filters/network/source/serialization.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

const FetchRecordConverter& FetchRecordConverterImpl::getDefaultInstance() {
  CONSTRUCT_ON_FIRST_USE(FetchRecordConverterImpl);
}

std::vector<FetchableTopicResponse>
FetchRecordConverterImpl::convert(const InboundRecordsMap& arg) const {

  // Compute record batches.
  std::map<KafkaPartition, Bytes> record_batches;
  for (const auto& partition_and_records : arg) {
    const KafkaPartition& kp = partition_and_records.first;
    const std::vector<InboundRecordSharedPtr>& partition_records = partition_and_records.second;
    const Bytes batch = renderRecordBatch(partition_records);
    record_batches[kp] = batch;
  }

  // Transform our maps into the Kafka structs.
  std::map<std::string, std::vector<FetchResponseResponsePartitionData>> topic_to_frrpd;
  for (const auto& record_batch : record_batches) {
    const std::string& topic_name = record_batch.first.first;
    const int32_t partition = record_batch.first.second;

    std::vector<FetchResponseResponsePartitionData>& frrpds = topic_to_frrpd[topic_name];
    const int16_t error_code = 0;
    const int64_t high_watermark = 0;
    const auto frrpd = FetchResponseResponsePartitionData{partition, error_code, high_watermark,
                                                          absl::make_optional(record_batch.second)};

    frrpds.push_back(frrpd);
  }

  std::vector<FetchableTopicResponse> result;
  for (const auto& partition_and_records : topic_to_frrpd) {
    const std::string& topic_name = partition_and_records.first;
    const auto ftr = FetchableTopicResponse{topic_name, partition_and_records.second};
    result.push_back(ftr);
  }
  return result;
}

// Magic format introduced around Kafka 1.0.0 and still used with Kafka 3.3.
constexpr int8_t MAGIC = 2;

Bytes FetchRecordConverterImpl::renderRecordBatch(
    const std::vector<InboundRecordSharedPtr>& records) const {

  Bytes result = {};

  // Base offset (bytes 0..7).
  const int64_t base_offset = htobe64(0);
  const unsigned char* base_offset_b = reinterpret_cast<const unsigned char*>(&base_offset);
  result.insert(result.end(), base_offset_b, base_offset_b + sizeof(base_offset));

  // Batch length placeholder (bytes 8..11).
  result.insert(result.end(), {0, 0, 0, 0});

  // All other attributes (spans partitionLeaderEpoch .. baseSequence) (bytes 12..56).
  const std::vector zeros(45, 0);
  result.insert(result.end(), zeros.begin(), zeros.end());

  // Last offset delta.
  // -1 means we always claim that we are at the beginning of partition.
  const int32_t last_offset_delta = htobe32(-1);
  const unsigned char* last_offset_delta_bytes =
      reinterpret_cast<const unsigned char*>(&last_offset_delta);
  const auto last_offset_delta_pos = result.begin() + 8 + 4 + 11;
  std::copy(last_offset_delta_bytes, last_offset_delta_bytes + sizeof(last_offset_delta),
            last_offset_delta_pos);

  // Records (count) (bytes 57..60).
  const int32_t record_count = htobe32(records.size());
  const unsigned char* record_count_b = reinterpret_cast<const unsigned char*>(&record_count);
  result.insert(result.end(), record_count_b, record_count_b + sizeof(record_count));

  // Records (data) (bytes 61+).
  for (const auto& record : records) {
    appendRecord(*record, result);
  }

  // Set batch length.
  const int32_t batch_len = htobe32(result.size() - (sizeof(base_offset) + sizeof(batch_len)));
  const unsigned char* batch_len_bytes = reinterpret_cast<const unsigned char*>(&batch_len);
  std::copy(batch_len_bytes, batch_len_bytes + sizeof(batch_len),
            result.begin() + sizeof(base_offset));

  // Set magic.
  const uint32_t magic_offset = sizeof(base_offset) + sizeof(batch_len) + sizeof(int32_t);
  result[magic_offset] = MAGIC;

  // Compute and set CRC.
  const uint32_t crc_offset = magic_offset + 1;
  const auto crc_data_start = result.data() + crc_offset + sizeof(int32_t);
  const auto crc_data_len = result.size() - (crc_offset + sizeof(int32_t));
  const Bytes crc = renderCrc32c(crc_data_start, crc_data_len);
  std::copy(crc.begin(), crc.end(), result.begin() + crc_offset);

  return result;
}

void FetchRecordConverterImpl::appendRecord(const InboundRecord& record, Bytes& out) const {

  Bytes tmp = {};
  // This is not precise maths, as we could be over-reserving a little due to var-length fields.
  tmp.reserve(sizeof(int8_t) + sizeof(int64_t) + sizeof(int32_t) + record.dataLengthEstimate());

  // attributes: int8
  const int8_t attributes = 0;
  tmp.push_back(static_cast<unsigned char>(attributes));

  // timestampDelta: varlong
  const int64_t timestamp_delta = 0;
  VarlenUtils::writeVarlong(timestamp_delta, tmp);

  // offsetDelta: varint
  const int32_t offset_delta = record.offset_;
  VarlenUtils::writeVarint(offset_delta, tmp);

  // Impl note: compared to requests/responses, records serialize byte arrays as varint length +
  // bytes (and not length + 1, then bytes). So we cannot use EncodingContext from serialization.h.

  // keyLength: varint
  // key: byte[]
  const NullableBytes& key = record.key_;
  if (key.has_value()) {
    VarlenUtils::writeVarint(key->size(), tmp);
    tmp.insert(tmp.end(), key->begin(), key->end());
  } else {
    VarlenUtils::writeVarint(-1, tmp);
  }

  // valueLen: varint
  // value: byte[]
  const NullableBytes& value = record.value_;
  if (value.has_value()) {
    VarlenUtils::writeVarint(value->size(), tmp);
    tmp.insert(tmp.end(), value->begin(), value->end());
  } else {
    VarlenUtils::writeVarint(-1, tmp);
  }

  // TODO (adam.kotwasinski) Headers are not supported yet.
  const int32_t header_count = 0;
  VarlenUtils::writeVarint(header_count, tmp);

  // Put tmp's length into 'out'.
  VarlenUtils::writeVarint(tmp.size(), out);

  // Put tmp's contents into 'out'.
  out.insert(out.end(), tmp.begin(), tmp.end());
}

// XXX (adam.kotwasinski) Instead of computing it naively, either link against librdkafka's
// implementation or generate it.
// https://github.com/confluentinc/librdkafka/blob/v1.8.0/src/crc32c.c#L1
uint32_t FetchRecordConverterImpl::computeCrc32c(const unsigned char* data, const size_t len) {
  uint32_t crc = 0xFFFFFFFF;
  for (size_t i = 0; i < len; i++) {
    char ch = data[i];
    for (size_t j = 0; j < 8; j++) {
      uint32_t b = (ch ^ crc) & 1;
      crc >>= 1;
      if (b) {
        crc = crc ^ 0x82F63B78;
      }
      ch >>= 1;
    }
  }
  return ~crc;
}

uint32_t FetchRecordConverterImpl::computeCrc32cForTest(const unsigned char* data,
                                                        const size_t len) {
  return computeCrc32c(data, len);
}

Bytes FetchRecordConverterImpl::renderCrc32c(const unsigned char* data, const size_t len) const {
  uint32_t crc = htobe32(computeCrc32c(data, len));
  Bytes result;
  unsigned char* raw = reinterpret_cast<unsigned char*>(&crc);
  result.insert(result.end(), raw, raw + sizeof(crc));
  return result;
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
