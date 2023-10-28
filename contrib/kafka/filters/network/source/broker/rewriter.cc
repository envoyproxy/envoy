#include "contrib/kafka/filters/network/source/broker/rewriter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

ResponseRewriter::ResponseRewriter(const BrokerFilterConfig& config) : config_{config} {};

void ResponseRewriter::onMessage(AbstractResponseSharedPtr response) {
  if (config_.force_response_rewrite_) {
    responses_to_rewrite_.push_back(response);
  }
}

void ResponseRewriter::onFailedParse(ResponseMetadataSharedPtr) {}

void ResponseRewriter::rewrite(Buffer::Instance& buffer) {
  if (!config_.force_response_rewrite_) {
    // Data received from upstream is going to be sent downstream without any changes.
    return;
  }

  // At this stage we have access to responses, and can do something to them.
  buffer.drain(buffer.length());
  ResponseEncoder encoder{buffer};
  ENVOY_LOG(trace, "emitting {} stored responses", responses_to_rewrite_.size());
  for (auto response : responses_to_rewrite_) {
    encoder.encode(*response);
  }
  responses_to_rewrite_.erase(responses_to_rewrite_.begin(), responses_to_rewrite_.end());
}

size_t ResponseRewriter::getStoredResponseCountForTest() const {
  return responses_to_rewrite_.size();
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
