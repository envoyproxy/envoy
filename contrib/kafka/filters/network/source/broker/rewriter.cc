#include "contrib/kafka/filters/network/source/broker/rewriter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// ResponseRewriterImpl.

void ResponseRewriterImpl::onMessage(AbstractResponseSharedPtr response) {
  responses_to_rewrite_.push_back(response);
}

void ResponseRewriterImpl::onFailedParse(ResponseMetadataSharedPtr) {}

void ResponseRewriterImpl::rewrite(Buffer::Instance& buffer) {
  // At this stage we have access to responses, and can do something to them.
  buffer.drain(buffer.length());
  ResponseEncoder encoder{buffer};
  ENVOY_LOG(trace, "emitting {} stored responses", responses_to_rewrite_.size());
  for (auto response : responses_to_rewrite_) {
    encoder.encode(*response);
  }
  responses_to_rewrite_.erase(responses_to_rewrite_.begin(), responses_to_rewrite_.end());
}

size_t ResponseRewriterImpl::getStoredResponseCountForTest() const {
  return responses_to_rewrite_.size();
}

// DoNothingRewriter.

void DoNothingRewriter::onMessage(AbstractResponseSharedPtr) {}

void DoNothingRewriter::onFailedParse(ResponseMetadataSharedPtr) {}

void DoNothingRewriter::rewrite(Buffer::Instance&) {}

// Factory method.

ResponseRewriterSharedPtr createRewriter(const BrokerFilterConfig& config) {
  if (config.force_response_rewrite_) {
    return std::make_shared<ResponseRewriterImpl>();
  } else {
    return std::make_shared<DoNothingRewriter>();
  }
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
