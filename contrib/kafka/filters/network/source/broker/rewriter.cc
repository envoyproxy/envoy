#include "contrib/kafka/filters/network/source/broker/rewriter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// ResponseRewriterImpl.

ResponseRewriterImpl::ResponseRewriterImpl(const BrokerFilterConfigSharedPtr& config)
    : config_{config} {};

void ResponseRewriterImpl::onMessage(AbstractResponseSharedPtr response) {
  responses_to_rewrite_.push_back(response);
}

void ResponseRewriterImpl::onFailedParse(ResponseMetadataSharedPtr) {}

constexpr int16_t METADATA_API_KEY = 3;
constexpr int16_t FIND_COORDINATOR_API_KEY = 10;
constexpr int16_t DESCRIBE_CLUSTER_API_KEY = 60;

template <typename T> static T& extractResponseData(AbstractResponseSharedPtr& arg) {
  using TSharedPtr = std::shared_ptr<Response<T>>;
  TSharedPtr cast = std::dynamic_pointer_cast<typename TSharedPtr::element_type>(arg);
  if (nullptr == cast) {
    throw new EnvoyException("bug: response class not matching response API key");
  } else {
    return cast->data_;
  }
}

void ResponseRewriterImpl::process(Buffer::Instance& buffer) {
  buffer.drain(buffer.length());
  ResponseEncoder encoder{buffer};
  ENVOY_LOG(trace, "emitting {} stored responses", responses_to_rewrite_.size());
  for (auto response : responses_to_rewrite_) {
    switch (response->apiKey()) {
    case METADATA_API_KEY: {
      auto& mr = extractResponseData<MetadataResponse>(response);
      updateMetadataBrokerAddresses(mr);
      break;
    }
    case FIND_COORDINATOR_API_KEY: {
      auto& fcr = extractResponseData<FindCoordinatorResponse>(response);
      updateFindCoordinatorBrokerAddresses(fcr);
      break;
    }
    case DESCRIBE_CLUSTER_API_KEY: {
      auto& dcr = extractResponseData<DescribeClusterResponse>(response);
      updateDescribeClusterBrokerAddresses(dcr);
      break;
    }
    }
    encoder.encode(*response);
  }
  responses_to_rewrite_.erase(responses_to_rewrite_.begin(), responses_to_rewrite_.end());
}

void ResponseRewriterImpl::updateMetadataBrokerAddresses(MetadataResponse& response) const {
  for (MetadataResponseBroker& broker : response.brokers_) {
    maybeUpdateHostAndPort(broker, &MetadataResponseBroker::node_id_);
  }
}

void ResponseRewriterImpl::updateFindCoordinatorBrokerAddresses(
    FindCoordinatorResponse& response) const {
  maybeUpdateHostAndPort(response, &FindCoordinatorResponse::node_id_);
  for (Coordinator& coordinator : response.coordinators_) {
    maybeUpdateHostAndPort(coordinator, &Coordinator::node_id_);
  }
}

void ResponseRewriterImpl::updateDescribeClusterBrokerAddresses(
    DescribeClusterResponse& response) const {
  for (DescribeClusterBroker& broker : response.brokers_) {
    maybeUpdateHostAndPort(broker, &DescribeClusterBroker::broker_id_);
  }
}

size_t ResponseRewriterImpl::getStoredResponseCountForTest() const {
  return responses_to_rewrite_.size();
}

// DoNothingRewriter.

void DoNothingRewriter::onMessage(AbstractResponseSharedPtr) {}

void DoNothingRewriter::onFailedParse(ResponseMetadataSharedPtr) {}

void DoNothingRewriter::process(Buffer::Instance&) {}

// Factory method.

ResponseRewriterSharedPtr createRewriter(const BrokerFilterConfigSharedPtr& config) {
  if (config->needsResponseRewrite()) {
    return std::make_shared<ResponseRewriterImpl>(config);
  } else {
    return std::make_shared<DoNothingRewriter>();
  }
}

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
