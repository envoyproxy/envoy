#pragma once

#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/external/responses.h"
#include "contrib/kafka/filters/network/source/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

/**
 * Responsible for modifying any outbound requests.
 */
class ResponseRewriter : public ResponseCallback {
public:
  virtual ~ResponseRewriter() = default;

  /**
   * Performs any desired payload changes.
   * @param buffer buffer with the original data from upstream
   */
  virtual void process(Buffer::Instance& buffer) PURE;
};

using ResponseRewriterSharedPtr = std::shared_ptr<ResponseRewriter>;

/**
 * Uses captured response objects instead of original data.
 * Entry point for any response payload changes.
 */
class ResponseRewriterImpl : public ResponseRewriter, private Logger::Loggable<Logger::Id::kafka> {
public:
  ResponseRewriterImpl(const BrokerFilterConfig& config);

  // ResponseCallback
  void onMessage(AbstractResponseSharedPtr response) override;
  void onFailedParse(ResponseMetadataSharedPtr parse_failure) override;

  // ResponseRewriter
  void process(Buffer::Instance& buffer) override;

  /**
   * Mutates response according to config.
   */
  void updateMetadataBrokerAddresses(MetadataResponse& response) const;

  /**
   * Mutates response according to config.
   */
  void updateFindCoordinatorBrokerAddresses(FindCoordinatorResponse& response) const;

  size_t getStoredResponseCountForTest() const;

private:
  // Helper function to update various response structures.
  template <typename T> void maybeUpdateHostAndPort(T& arg) const {
    const absl::optional<HostAndPort> hostAndPort = config_.findBrokerAddressOverride(arg.node_id_);
    if (hostAndPort) {
      ENVOY_LOG(trace, "Changing broker [{}] from {}:{} to {}:{}", arg.node_id_, arg.host_,
                arg.port_, hostAndPort->first, hostAndPort->second);
      arg.host_ = hostAndPort->first;
      arg.port_ = hostAndPort->second;
    }
  }

  const BrokerFilterConfig& config_;
  std::vector<AbstractResponseSharedPtr> responses_to_rewrite_;
};

/**
 * Does nothing, letting the data from upstream pass without any changes.
 * It allows us to avoid the unnecessary deserialization-then-serialization steps.
 */
class DoNothingRewriter : public ResponseRewriter {
public:
  // ResponseCallback
  void onMessage(AbstractResponseSharedPtr response) override;
  void onFailedParse(ResponseMetadataSharedPtr parse_failure) override;

  // ResponseRewriter
  void process(Buffer::Instance& buffer) override;
};

/**
 * Factory method that creates a rewriter depending on configuration.
 */
ResponseRewriterSharedPtr createRewriter(const BrokerFilterConfig& config);

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
