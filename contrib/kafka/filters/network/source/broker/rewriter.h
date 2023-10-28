#pragma once

#include <vector>

#include "envoy/buffer/buffer.h"

#include "source/common/common/logger.h"

#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/response_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class ResponseRewriter : public ResponseCallback, private Logger::Loggable<Logger::Id::kafka> {
public:
  ResponseRewriter(const BrokerFilterConfig& config);

  // ResponseCallback
  void onMessage(AbstractResponseSharedPtr response) override;
  void onFailedParse(ResponseMetadataSharedPtr parse_failure) override;

  void rewrite(Buffer::Instance& buffer);

  size_t getStoredResponseCountForTest() const;

private:
  const BrokerFilterConfig config_;
  std::vector<AbstractResponseSharedPtr> responses_to_rewrite_;
};

using ResponseRewriterSharedPtr = std::shared_ptr<ResponseRewriter>;

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
