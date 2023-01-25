#pragma once

#include <memory>
#include <vector>

#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/kafka_types.h"
#include "contrib/kafka/filters/network/source/mesh/inbound_record.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Dependency injection class responsible for converting received records into serializable form
 * that we can put into Fetch responses.
 */
class FetchRecordConverter {
public:
  std::vector<FetchableTopicResponse>
  convert(const std::map<KafkaPartition, std::vector<InboundRecordSharedPtr>>& arg) const;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
