#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/outbound_record.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Dependency injection class responsible for extracting records out of produce request's contents.
 */
class RecordExtractor {
public:
  virtual ~RecordExtractor() = default;

  virtual std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const PURE;
};

/**
 * Proper implementation of record extractor, capable of parsing V2 record set.
 * Reference: https://kafka.apache.org/24/documentation/#messageformat
 */
class RecordExtractorImpl : public RecordExtractor {
public:
  // RecordExtractor
  std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const override;

  // Helper function to get the data (such as key, value) out of given input, as most of the
  // interesting fields in records are kept as variable-encoded length and following bytes.
  static absl::string_view extractByteArray(absl::string_view& input);

private:
  std::vector<OutboundRecord> extractPartitionRecords(const std::string& topic,
                                                      const int32_t partition,
                                                      const Bytes& records) const;

  std::vector<OutboundRecord> processRecordBatch(const std::string& topic, const int32_t partition,
                                                 absl::string_view data) const;

  OutboundRecord extractRecord(const std::string& topic, const int32_t partition,
                               absl::string_view& data) const;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
