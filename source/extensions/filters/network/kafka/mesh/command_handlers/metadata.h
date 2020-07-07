#pragma once

#include "extensions/filters/network/kafka/external/requests.h"
#include "extensions/filters/network/kafka/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MetadataRequestHolder : public AbstractInFlightRequest {
public:
  MetadataRequestHolder(AbstractRequestListener& filter,
                        const ClusteringConfiguration& clustering_configuration,
                        const std::shared_ptr<Request<MetadataRequest>> request);

  void invoke(UpstreamKafkaFacade&) override;

  bool finished() const override;

  AbstractResponseSharedPtr computeAnswer() const override;

private:
  // Configuration used to provide data for response.
  const ClusteringConfiguration& clustering_configuration_;

  // Original request.
  const std::shared_ptr<Request<MetadataRequest>> request_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
