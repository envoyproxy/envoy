#pragma once

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/subscription.h"

#include "common/common/assert.h"
#include "common/common/logger.h"
#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Config {

class GrpcXdsContext {
public:
  void addSubscription(const std::vector<std::string>& resources, const std::string& type_url,
                       const LocalInfo::LocalInfo& local_info, SubscriptionCallbacks& callbacks,
                       Event::Dispatcher& dispatcher, std::chrono::milliseconds init_fetch_timeout,
                       SubscriptionStats stats);

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resource_versions_.
  void updateSubscription(const std::vector<std::string>& resources, const std::string& type_url);

  void removeSubscription(const std::string& type_url);

  void pause(const std::string& type_url);

  void resume(const std::string& type_url);

  // Returns whether the request was actually sent (and so can leave the queue).
  virtual void sendDiscoveryRequest(const RequestQueueItem& queue_item) PURE;

  virtual void handleResponse(std::unique_ptr<ResponseProto>&& message) PURE;
  virtual void handleStreamEstablished() PURE;
  virtual void handleEstablishmentFailure() PURE;
};

} // namespace Config
} // namespace Envoy
