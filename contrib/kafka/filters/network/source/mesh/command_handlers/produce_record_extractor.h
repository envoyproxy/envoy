#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce_outbound_record.h"

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
  std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const override;

  static absl::string_view extractElement(absl::string_view& input);

private:
  std::vector<OutboundRecord> extractPartitionRecords(const std::string& topic,
                                                      const int32_t partition,
                                                      const Bytes& records) const;

  // Impl note: I'm sorry for the long name.
  std::vector<OutboundRecord> extractRecordsOutOfBatchWithMagicEqualTo2(const std::string& topic,
                                                                        const int32_t partition,
                                                                        absl::string_view sv) const;

  OutboundRecord extractRecord(const std::string& topic, const int32_t partition,
                               absl::string_view& data) const;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
