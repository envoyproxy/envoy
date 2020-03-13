#pragma once

#include "envoy/common/exception.h"
#include "envoy/common/pure.h"
#include "envoy/config/subscription.h"
#include "envoy/stats/stats_macros.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

/**
 * All control plane related stats. @see stats_macros.h
 */
#define ALL_CONTROL_PLANE_STATS(COUNTER, GAUGE)                                                    \
  COUNTER(rate_limit_enforced)                                                                     \
  GAUGE(connected_state, NeverImport)                                                              \
  GAUGE(pending_requests, Accumulate)

/**
 * Struct definition for all control plane stats. @see stats_macros.h
 */
struct ControlPlaneStats {
  ALL_CONTROL_PLANE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

/**
 * Handle on a muxed gRPC subscription. The subscription is canceled on destruction.
 */
class GrpcMuxWatch {
public:
  virtual ~GrpcMuxWatch() = default;

  /**
   * Updates the set of resources that the watch is interested in.
   * @param resources set of resource names to watch for
   */
  virtual void update(const std::set<std::string>& resources) PURE;
};

using GrpcMuxWatchPtr = std::unique_ptr<GrpcMuxWatch>;

/**
 * Manage one or more gRPC subscriptions on a single stream to management server. This can be used
 * for a single xDS API, e.g. EDS, or to combined multiple xDS APIs for ADS.
 */
class GrpcMux {
public:
  virtual ~GrpcMux() = default;

  /**
   * Initiate stream with management server.
   */
  virtual void start() PURE;

  /**
   * Pause discovery requests for a given API type. This is useful when we're processing an update
   * for LDS or CDS and don't want a flood of updates for RDS or EDS respectively. Discovery
   * requests may later be resumed with resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   */
  virtual void pause(const std::string& type_url) PURE;

  /**
   * Resume discovery requests for a given API type. This will send a discovery request if one would
   * have been sent during the pause.
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   */
  virtual void resume(const std::string& type_url) PURE;

  /**
   * Retrieves the current pause state as set by pause()/resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster
   * @return bool whether the API is paused.
   */
  virtual bool paused(const std::string& type_url) const PURE;

  /**
   * Start a configuration subscription asynchronously for some API type and resources.
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster.
   * @param resources set of resource names to watch for. If this is empty, then all
   *                  resources for type_url will result in callbacks.
   * @param callbacks the callbacks to be notified of configuration updates. These must be valid
   *                  until GrpcMuxWatch is destroyed.
   * @return GrpcMuxWatchPtr a handle to cancel the subscription with. E.g. when a cluster goes
   * away, its EDS updates should be cancelled by destroying the GrpcMuxWatchPtr.
   */
  virtual GrpcMuxWatchPtr addWatch(const std::string& type_url,
                                   const std::set<std::string>& resources,
                                   SubscriptionCallbacks& callbacks) PURE;
};

using GrpcMuxPtr = std::unique_ptr<GrpcMux>;
using GrpcMuxSharedPtr = std::shared_ptr<GrpcMux>;

/**
 * A grouping of callbacks that a GrpcMux should provide to its GrpcStream.
 */
template <class ResponseProto> class GrpcStreamCallbacks {
public:
  virtual ~GrpcStreamCallbacks() = default;

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
