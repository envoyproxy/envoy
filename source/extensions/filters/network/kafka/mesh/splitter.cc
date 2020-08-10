#include "extensions/filters/network/kafka/mesh/splitter.h"

#include "extensions/filters/network/kafka/mesh/command_handlers/api_versions.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/metadata.h"
#include "extensions/filters/network/kafka/mesh/command_handlers/produce.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

RequestInFlightFactory::RequestInFlightFactory(
    AbstractRequestListener& origin, const ClusteringConfiguration& clustering_configuration)
    : origin_{origin}, clustering_configuration_{clustering_configuration} {}

AbstractInFlightRequestSharedPtr
RequestInFlightFactory::create(const std::shared_ptr<Request<ProduceRequest>> request) const {
  ENVOY_LOG(warn, "RequestInFlightFactory - create(Produce({})) for cid {}",
            request->request_header_.api_version_, request->request_header_.correlation_id_);
  return std::make_shared<ProduceRequestHolder>(origin_, request);
}

AbstractInFlightRequestSharedPtr
RequestInFlightFactory::create(const std::shared_ptr<Request<MetadataRequest>> request) const {
  ENVOY_LOG(warn, "RequestInFlightFactory - create(Metadata) for cid {}",
            request->request_header_.correlation_id_);
  return std::make_shared<MetadataRequestHolder>(origin_, clustering_configuration_, request);
}

AbstractInFlightRequestSharedPtr
RequestInFlightFactory::create(const std::shared_ptr<Request<ApiVersionsRequest>> request) const {
  ENVOY_LOG(warn, "RequestInFlightFactory - create(ApiVersions) for cid {}",
            request->request_header_.correlation_id_);
  return std::make_shared<ApiVersionsRequestHolder>(origin_, request);
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
