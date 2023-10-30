#include "contrib/kafka/filters/network/source/broker/rewriter.h"

#include "contrib/kafka/filters/network/source/external/responses.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

// ResponseRewriterImpl.

ResponseRewriterImpl::ResponseRewriterImpl(const BrokerFilterConfig& config) : config_{config} {};

void ResponseRewriterImpl::onMessage(AbstractResponseSharedPtr response) {
  responses_to_rewrite_.push_back(response);
}

void ResponseRewriterImpl::onFailedParse(ResponseMetadataSharedPtr) {}

constexpr int16_t METADATA_API_KEY = 3;
constexpr int16_t FIND_COORDINATOR_API_KEY = 10;

void ResponseRewriterImpl::process(Buffer::Instance& buffer) {
  buffer.drain(buffer.length());
  ResponseEncoder encoder{buffer};
  ENVOY_LOG(trace, "emitting {} stored responses", responses_to_rewrite_.size());
  for (auto response : responses_to_rewrite_) {
    switch (response->apiKey()) {
    case METADATA_API_KEY:
      updateMetadataBrokerAddresses(response);
      break;
    case FIND_COORDINATOR_API_KEY:
      updateFindCoordinatorBrokerAddresses(response);
      break;
    }
    encoder.encode(*response);
  }
  responses_to_rewrite_.erase(responses_to_rewrite_.begin(), responses_to_rewrite_.end());
}

void ResponseRewriterImpl::updateMetadataBrokerAddresses(
    AbstractResponseSharedPtr& response) const {

  using MetadataResponseSharedPtr = std::shared_ptr<Response<MetadataResponse>>;
  MetadataResponseSharedPtr cast = std::dynamic_pointer_cast<Response<MetadataResponse>>(response);
  if (nullptr == cast) {
    throw new EnvoyException("bug: response class not matching response API key");
  }
  MetadataResponse& data = cast->data_;
  std::vector<MetadataResponseBroker>& brokers = data.brokers_;
  for (MetadataResponseBroker& broker : brokers) {
    const absl::optional<HostAndPort> hostAndPort =
        config_.findBrokerAddressOverride(broker.node_id_);
    if (hostAndPort) {
      ENVOY_LOG(info, "Changing broker [{}] from {}:{} to {}:{}", broker.node_id_, broker.host_,
                broker.port_, hostAndPort->first, hostAndPort->second);
      broker.host_ = hostAndPort->first;
      broker.port_ = hostAndPort->second;
    }
  }
}

void ResponseRewriterImpl::updateFindCoordinatorBrokerAddresses(
    AbstractResponseSharedPtr& response) const {

  using FCSharedPtr = std::shared_ptr<Response<FindCoordinatorResponse>>;
  FCSharedPtr cast = std::dynamic_pointer_cast<Response<FindCoordinatorResponse>>(response);
  if (nullptr == cast) {
    throw new EnvoyException("bug: response class not matching response API key");
  }
  FindCoordinatorResponse& data = cast->data_;

  {
    const absl::optional<HostAndPort> hostAndPort =
        config_.findBrokerAddressOverride(data.node_id_);
    if (hostAndPort) {
      ENVOY_LOG(info, "Changing broker [{}] from {}:{} to {}:{}", data.node_id_, data.host_,
                data.port_, hostAndPort->first, hostAndPort->second);
      data.host_ = hostAndPort->first;
      data.port_ = hostAndPort->second;
    }
  }

  std::vector<Coordinator>& coordinators = data.coordinators_;
  for (Coordinator& coordinator : coordinators) {
    const absl::optional<HostAndPort> hostAndPort =
        config_.findBrokerAddressOverride(coordinator.node_id_);
    if (hostAndPort) {
      ENVOY_LOG(info, "Changing broker [{}] from {}:{} to {}:{}", coordinator.node_id_,
                coordinator.host_, coordinator.port_, hostAndPort->first, hostAndPort->second);
      coordinator.host_ = hostAndPort->first;
      coordinator.port_ = hostAndPort->second;
    }
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

ResponseRewriterSharedPtr createRewriter(const BrokerFilterConfig& config) {
  if (config.needsResponseRewrite()) {
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
