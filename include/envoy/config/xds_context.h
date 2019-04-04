#pragma once

#include "envoy/config/grpc_mux.h" // TODO TODO remove
#include "envoy/config/subscription.h"
#include "envoy/event/dispatcher.h"
#include "envoy/local_info/local_info.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class XdsGrpcContext {
public:
  virtual ~XdsGrpcContext() {}
  virtual void addSubscription(const std::vector<std::string>& resources,
                               const std::string& type_url, SubscriptionCallbacks& callbacks,
                               SubscriptionStats& stats,
                               std::chrono::milliseconds init_fetch_timeout) PURE;

  // Enqueues and attempts to send a discovery request, (un)subscribing to resources missing from /
  // added to the passed 'resources' argument, relative to resource_versions_.
  virtual void updateResources(const std::vector<std::string>& resources,
                               const std::string& type_url) PURE;

  virtual void removeSubscription(const std::string& type_url) PURE;

  virtual void pause(const std::string& type_url) PURE;
  virtual void resume(const std::string& type_url) PURE;

  virtual void drainRequests() PURE;

  /**
   * For the gRPC stream handler to prompt the context to take appropriate action in response to the
   * gRPC stream having been successfully established.
   */
  virtual void handleStreamEstablished() PURE;

  /**
   * For the gRPC stream handler to prompt the context to take appropriate action in response to
   * failure to establish the gRPC stream.
   */
  virtual void handleEstablishmentFailure() PURE;

  // TODO TODO remove
  virtual GrpcMuxWatchPtr subscribe(const std::string& type_url,
                                    const std::vector<std::string>& resources,
                                    GrpcMuxCallbacks& callbacks) PURE;
  // TODO TODO remove
  virtual void start() PURE;
};

} // namespace Config
} // namespace Envoy
