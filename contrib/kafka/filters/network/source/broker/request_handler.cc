#include "contrib/kafka/filters/network/source/broker/request_handler.h"

#include "envoy/network/connection.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// RequestHandlerImpl.

RequestHandlerImpl::RequestHandlerImpl(const BrokerFilterConfigSharedPtr& config)
    : api_keys_allowed_{config->apiKeysAllowed()}, api_keys_denied_{config->apiKeysDenied()} {}

void RequestHandlerImpl::onMessage(AbstractRequestSharedPtr request) {
  const auto api_key = request->apiKey();
  const auto allowed = apiKeyAllowed(api_key, api_keys_allowed_, api_keys_denied_);
  if (!allowed) {
    auto& connection = callbacks_->connection();
    ENVOY_CONN_LOG(debug, "Closing connection for request {}", connection, api_key);
    connection.close(Network::ConnectionCloseType::FlushWrite, "denied (API key)");
  }
}

void RequestHandlerImpl::onFailedParse(RequestParseFailureSharedPtr) {}

void RequestHandlerImpl::setReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) {
  this->callbacks_ = &callbacks;
}

bool RequestHandlerImpl::apiKeyAllowed(const int16_t api_key,
                                       const absl::flat_hash_set<int16_t>& allowed,
                                       const absl::flat_hash_set<int16_t>& denied) {
  // If allowlist is configured, first check whether the request is allowed at all.
  if (!allowed.empty()) {
    if (!allowed.contains(api_key)) {
      return false;
    }
  }
  // Check whether request is denied.
  return !denied.contains(api_key);
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
