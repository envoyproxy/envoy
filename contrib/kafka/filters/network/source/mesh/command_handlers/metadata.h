#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"
#include "contrib/kafka/filters/network/source/mesh/upstream_config.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class MetadataRequestHolder : public BaseInFlightRequest {
public:
  MetadataRequestHolder(AbstractRequestListener& filter,
                        const UpstreamKafkaConfiguration& configuration,
                        const std::shared_ptr<Request<MetadataRequest>> request);

  void startProcessing() override;

  bool finished() const override;

  AbstractResponseSharedPtr computeAnswer() const override;

private:
  // Configuration used to provide data for response.
  const UpstreamKafkaConfiguration& configuration_;

  // Original request.
  const std::shared_ptr<Request<MetadataRequest>> request_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
