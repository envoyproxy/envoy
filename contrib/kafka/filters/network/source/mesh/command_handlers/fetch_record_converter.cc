#include "contrib/kafka/filters/network/source/mesh/command_handlers/fetch_record_converter.h"

#include "source/common/buffer/buffer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

std::vector<FetchableTopicResponse> FetchResponsePayloadProcessor::transform(
    const std::map<KafkaPartition, std::vector<InboundRecordSharedPtr>>&) const {

  return {};
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
