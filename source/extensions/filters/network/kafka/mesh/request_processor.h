#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/filters/network/kafka/external/requests.h"
#include "source/extensions/filters/network/kafka/mesh/abstract_command.h"
#include "source/extensions/filters/network/kafka/request_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

/**
 * Processes (enriches) incoming requests and passes it back to origin.
 */
class RequestProcessor : public RequestCallback, private Logger::Loggable<Logger::Id::kafka> {
public:
  RequestProcessor() = default;

  // RequestCallback
  void onMessage(AbstractRequestSharedPtr arg) override;
  void onFailedParse(RequestParseFailureSharedPtr) override;
};

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
