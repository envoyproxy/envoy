#pragma once

#include "envoy/common/pure.h"
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
  /**
   * Starts a configuration subscription asynchronously for some API type and resources. If the gRPC
   * stream to the management server is not already up, starts it.
   * @param resources vector of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until this XdsGrpcContext is destroyed.
   * @param stats reference to a stats object, which should be owned by the SubscriptionImpl, for
   * the XdsGrpcContext to record stats specific to this one subscription.
   * @param init_fetch_timeout how long the first fetch has to complete before onConfigUpdateFailed
   * will be called.
   */
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

  // TODO TODO remove
  virtual GrpcMuxWatchPtr subscribe(const std::string& type_url,
                                    const std::vector<std::string>& resources,
                                    GrpcMuxCallbacks& callbacks) PURE;
};

/**
 * A grouping of callbacks that an XdsGrpcContext should provide to its GrpcStream.
 */
template <class ResponseProto> class GrpcStreamCallbacks {
public:
  virtual ~GrpcStreamCallbacks() {}

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to the
   * gRPC stream having been successfully established.
   */
  virtual void onStreamEstablished() PURE;

  /**
   * For the GrpcStream to prompt the context to take appropriate action in response to
   * failure to establish the gRPC stream.
   */
  virtual void onEstablishmentFailure() PURE;

  /**
   * For the GrpcStream to pass received protos to the context.
   */
  virtual void onDiscoveryResponse(std::unique_ptr<ResponseProto>&& message) PURE;

  /**
   * For the GrpcStream to call when its rate limiting logic allows more requests to be sent.
   */
  virtual void onWriteable() PURE;
};

} // namespace Config
} // namespace Envoy
