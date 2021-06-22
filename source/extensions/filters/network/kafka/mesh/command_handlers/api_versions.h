#pragma once

#include "source/extensions/filters/network/kafka/external/requests.h"
#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class ApiVersionsRequestHolder : public BaseInFlightRequest {
public:
  ApiVersionsRequestHolder(AbstractRequestListener& filter,
                           const std::shared_ptr<Request<ApiVersionsRequest>> request);

  void invoke(UpstreamKafkaFacade&) override;

  bool finished() const override;

  AbstractResponseSharedPtr computeAnswer() const override;

private:
  // Original request.
  const std::shared_ptr<Request<ApiVersionsRequest>> request_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
