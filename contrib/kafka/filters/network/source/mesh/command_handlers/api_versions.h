#pragma once

#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Api version requests are the first requests sent by Kafka clients to brokers.
 * We send our customized response to fail clients that might be trying to accomplish something more
 * than this filter supports.
 */
class ApiVersionsRequestHolder : public BaseInFlightRequest {
public:
  ApiVersionsRequestHolder(AbstractRequestListener& filter, const RequestHeader request_header);

  void startProcessing() override;

  bool finished() const override;

  AbstractResponseSharedPtr computeAnswer() const override;

private:
  // Original request header.
  const RequestHeader request_header_;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
