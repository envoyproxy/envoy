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
 * Just a placeholder for now.
 */
class PlaceholderRecordExtractor : public RecordExtractor {
public:
  std::vector<OutboundRecord>
  extractRecords(const std::vector<TopicProduceData>& data) const override;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
