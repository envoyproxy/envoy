#pragma once

#include "envoy/network/filter.h"

#include "source/common/common/logger.h"

#include "absl/container/flat_hash_set.h"
#include "contrib/kafka/filters/network/source/broker/filter_config.h"
#include "contrib/kafka/filters/network/source/external/requests.h"
#include "contrib/kafka/filters/network/source/request_codec.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace Kafka {
namespace Broker {

class RequestHandler : public RequestCallback {
public:
  ~RequestHandler() override = default;

  virtual void setReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) PURE;
};

using RequestHandlerSharedPtr = std::shared_ptr<RequestHandler>;

class RequestHandlerImpl : public RequestHandler, private Logger::Loggable<Logger::Id::kafka> {
public:
  RequestHandlerImpl(const BrokerFilterConfigSharedPtr& config);

  // RequestCallback
  void onMessage(AbstractRequestSharedPtr request) override;
  void onFailedParse(RequestParseFailureSharedPtr parse_failure) override;

  // RequestHandler
  void setReadFilterCallbacks(Network::ReadFilterCallbacks& callbacks) override;

  static bool apiKeyAllowed(const int16_t api_key,
                            const absl::flat_hash_set<int16_t>& api_keys_allowed,
                            const absl::flat_hash_set<int16_t>& api_keys_denied);

private:
  Network::ReadFilterCallbacks* callbacks_;

  absl::flat_hash_set<int16_t> api_keys_allowed_;
  absl::flat_hash_set<int16_t> api_keys_denied_;
};

} // namespace Broker
} // namespace Kafka
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
