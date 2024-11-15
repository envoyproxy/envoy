#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

class ListOffsetsRequestHolder : public BaseInFlightRequest {
public:
  ListOffsetsRequestHolder(AbstractRequestListener& filter,
                           const std::shared_ptr<Request<ListOffsetsRequest>> request);

  void startProcessing() override;

  bool finished() const override;

  AbstractResponseSharedPtr computeAnswer() const override;

private:
  // Original request.
  const std::shared_ptr<Request<ListOffsetsRequest>> request_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
