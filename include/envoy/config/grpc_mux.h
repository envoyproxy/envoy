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

struct Watch;

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
   * type.googleapis.com/envoy.api.v2.Cluster
   */
  virtual void pause(const std::string& type_url) PURE;

  /**
   * Resume discovery requests for a given API type. This will send a discovery request if one would
   * have been sent during the pause.
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   */
  virtual void resume(const std::string& type_url) PURE;

  /**
   * Registers a GrpcSubscription with the GrpcMux. 'watch' may be null (meaning this is an add),
   * or it may be the Watch* previously returned by this function (which makes it an update).
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   * @param watch the Watch* to be updated, or nullptr to add one.
   * @param resources the set of resource names for 'watch' to start out interested in. If empty,
   *                  'watch' is treated as interested in *all* resources (of type type_url).
   * @param callbacks the callbacks that receive updates for 'resources' when they arrive.
   * @param init_fetch_timeout how long to wait for this new subscription's first update. Ignored
   *                           unless the addOrUpdateWatch() call is the first for 'type_url'.
   * @return Watch* the opaque watch token added or updated, to be used in future addOrUpdateWatch
   *                calls.
   */
  virtual Watch* addOrUpdateWatch(const std::string& type_url, Watch* watch,
                                  const std::set<std::string>& resources,
                                  SubscriptionCallbacks& callbacks,
                                  std::chrono::milliseconds init_fetch_timeout) PURE;

  /**
   * Cleanup of a Watch* added by addOrUpdateWatch(). Receiving a Watch* from addOrUpdateWatch()
   * makes you responsible for eventually invoking this cleanup.
   * @param type_url type URL corresponding to xDS API e.g. type.googleapis.com/envoy.api.v2.Cluster
   * @param watch the watch to be cleaned up.
   */
  virtual void removeWatch(const std::string& type_url, Watch* watch) PURE;

  /**
   * Retrieves the current pause state as set by pause()/resume().
   * @param type_url type URL corresponding to xDS API, e.g.
   * type.googleapis.com/envoy.api.v2.Cluster
   * @return bool whether the API is paused.
   */
  virtual bool paused(const std::string& type_url) const PURE;

  /**
   * Passes through to all multiplexed SubscriptionStates. To be called when something
   * definitive happens with the initial fetch: either an update is successfully received,
   * or some sort of error happened.*/
  virtual void disableInitFetchTimeoutTimer() PURE;
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
