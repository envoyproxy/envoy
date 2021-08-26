#include "contrib/kafka/filters/network/source/mesh/command_handlers/produce_record_extractor.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

std::vector<OutboundRecord>
PlaceholderRecordExtractor::extractRecords(const std::vector<TopicProduceData>&) const {
  return {};
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
