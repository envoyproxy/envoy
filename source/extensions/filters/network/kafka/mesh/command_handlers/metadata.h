#pragma once

#include "source/extensions/filters/network/kafka/external/requests.h"
#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"

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

  void invoke(UpstreamKafkaFacade&) override;

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
