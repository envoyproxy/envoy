#include "contrib/kafka/filters/network/source/mesh/abstract_command.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Mesh {

void BaseInFlightRequest::abandon() {
  ENVOY_LOG(trace, "Abandoning request");
  filter_active_ = false;
}

void BaseInFlightRequest::notifyFilter() {
  if (filter_active_) {
    ENVOY_LOG(trace, "Notifying filter for request");
    filter_.onRequestReadyForAnswer();
  } else {
    ENVOY_LOG(trace, "Request has been finished, but we are not doing anything, because filter has "
                     "been already destroyed");
  }
}

} // namespace Mesh
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
